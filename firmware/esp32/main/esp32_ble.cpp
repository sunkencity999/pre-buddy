// SPDX-License-Identifier: TBD
// ESP32-S3 IBleTransport — Nordic UART Service peripheral over NimBLE.
//
// Advertises the NUS service (UUIDs from pre_buddy/hal/uuids.h) under the
// device name, accepts newline-framed JSON on the RX characteristic, and
// pushes lines back to the central via TX notify.
//
// The NimBLE host runs on its own task; the RX callback reassembles fragmented
// writes into '\n'-terminated lines and pushes each into a FreeRTOS message
// buffer (g_rx_msgbuf). The RobotLoop drains it via has_incoming()/
// pop_incoming() from the main task. The message buffer is the single-writer
// (host task) / single-reader (main task) boundary, so no extra lock is needed.

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/message_buffer.h"
#include "freertos/task.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_att.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

// NimBLE's C headers define min()/max() as function-like macros, which
// clobber the C++ standard library's std::min/std::max templates the moment
// a libstdc++ header is parsed (esp32_ble.h -> line_framer.h -> <array>).
// Kill the macros before any C++ header below.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <cstring>

#include "esp32_ble.h"
#include "pre_buddy/hal/uuids.h"

namespace pre_buddy::esp32 {
namespace {

constexpr const char* TAG = "pre-buddy-ble";

Esp32BleTransport* g_instance = nullptr;

// Inbound (central → device) line queue. The RX GATT callback (NimBLE host
// task) reassembles fragmented writes into '\n'-terminated lines in g_rx_accum
// and pushes each completed line into g_rx_msgbuf; the main loop drains it via
// pop_incoming. A queue (not the old single-line framer) is what lets fast
// write-WITHOUT-response bursts land without dropping lines — the main loop no
// longer has to pop each line before the next one completes. Single writer
// (RX task) + single reader (main task) = the message buffer needs no extra
// lock. Sized to hold a burst of audio-output frame lines (~1.8 KB each).
constexpr std::size_t kRxLineMax = 2048;
constexpr std::size_t kRxQueueBytes = 32768;
MessageBufferHandle_t g_rx_msgbuf = nullptr;
char g_rx_accum[kRxLineMax];
std::size_t g_rx_accum_len = 0;
bool g_rx_overflow = false;  // current line exceeded kRxLineMax → drop it

uint16_t g_tx_val_handle = 0;
volatile uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
volatile bool g_connected = false;
uint8_t g_own_addr_type = 0;
char g_device_name[32] = "pre-buddy";

// Ask for the largest MTU we'll ever need. The negotiated value is
// min(this, central's preference); leaving it at NimBLE's 23-byte default
// caps every notification at 20 bytes and forces slow long-writes, which is
// fine for control events but far too slow for streaming audio. 512 lets a
// ~20 ms PCM16 frame cross in ~2 notifications instead of dozens.
constexpr uint16_t kPreferredMtu = 512;

// NUS 128-bit UUIDs, little-endian byte order (reverse of the dashed string
// form in uuids.h). 6E40000x-B5A3-F393-E0A9-E50E24DCCA9E.
const ble_uuid128_t kSvcUuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
const ble_uuid128_t kRxUuid = BLE_UUID128_INIT(  // central -> peripheral (write)
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);
const ble_uuid128_t kTxUuid = BLE_UUID128_INIT(  // peripheral -> central (notify)
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

void start_advertising();

// Push one completed line (no trailing '\n') into the RX queue. Drops it if
// the queue is momentarily full — only possible if the main loop stalls far
// longer than a burst, which it doesn't during reception.
void rx_push_line(const char* data, std::size_t len) {
    if (g_rx_msgbuf == nullptr || len == 0) return;
    if (xMessageBufferSend(g_rx_msgbuf, data, len, 0) != len) {
        ESP_LOGW(TAG, "RX queue full — dropped a %u-byte line", (unsigned)len);
    }
}

// RX: central wrote bytes. Reassemble fragmented writes into '\n'-terminated
// lines and queue each completed line. Runs on the NimBLE host task; g_rx_accum
// is touched only here, so no lock is needed (the queue is the SPSC boundary).
int gatt_rx_cb(uint16_t conn_handle, uint16_t attr_handle,
               struct ble_gatt_access_ctxt* ctxt, void* arg) {
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    // The central terminates every logical line with '\n' and fragments it
    // across as many MTU-sized writes as needed (audio output frames span
    // several). Scan the bytes for newlines, completing a line no matter how
    // the writes fall.
    for (struct os_mbuf* om = ctxt->om; om != nullptr; om = SLIST_NEXT(om, om_next)) {
        const auto* p = reinterpret_cast<const char*>(om->om_data);
        for (uint16_t i = 0; i < om->om_len; ++i) {
            const char c = p[i];
            if (c == '\n') {
                if (!g_rx_overflow) rx_push_line(g_rx_accum, g_rx_accum_len);
                g_rx_accum_len = 0;
                g_rx_overflow = false;
            } else if (g_rx_accum_len < kRxLineMax) {
                g_rx_accum[g_rx_accum_len++] = c;
            } else {
                g_rx_overflow = true;  // line too long; discard until next '\n'
            }
        }
    }
    return 0;
}

// TX is notify-only; reads/writes shouldn't reach here, but NimBLE wants a cb.
int gatt_tx_cb(uint16_t conn_handle, uint16_t attr_handle,
               struct ble_gatt_access_ctxt* ctxt, void* arg) {
    (void)conn_handle;
    (void)attr_handle;
    (void)ctxt;
    (void)arg;
    return 0;
}

// Notify a single (already MTU-sized) byte run on the TX characteristic.
// ble_gatts_notify_custom consumes the mbuf regardless of outcome, so each
// retry allocates a fresh one. The controller's notify buffers fill up under
// sustained streaming (audio); ENOMEM there is transient, so back off and
// retry rather than dropping the chunk. Bounded so a real stall can't wedge
// the caller's task.
bool notify_bytes(uint16_t conn, const uint8_t* data, std::size_t n) {
    // Block until the controller accepts this chunk. Back-pressure (the mbuf
    // pool / ACL buffers filling) is always transient — it clears as the radio
    // drains — so retrying paces us to the link rate without ever dropping a
    // byte. This matters because a dropped chunk truncates a JSON line, and a
    // dropped *newline* makes the central's reassembler merge the next frames
    // into one garbled blob (a whole burst lost, not just one frame). Bail only
    // on disconnect or an absurd ~10 s stall (link genuinely wedged).
    for (int attempt = 0; attempt < 2500; ++attempt) {
        if (!g_connected) return false;
        struct os_mbuf* om = ble_hs_mbuf_from_flat(data, n);
        if (om == nullptr) {  // mbuf pool momentarily exhausted
            vTaskDelay(pdMS_TO_TICKS(4));
            continue;
        }
        int rc = ble_gatts_notify_custom(conn, g_tx_val_handle, om);
        if (rc == 0) return true;
        if (rc == BLE_HS_ENOMEM) {  // controller TX buffers full; let them drain
            vTaskDelay(pdMS_TO_TICKS(4));
            continue;
        }
        return false;  // hard error (disconnected, etc.)
    }
    return false;
}

const struct ble_gatt_chr_def kNusChrs[] = {
    {
        .uuid = &kRxUuid.u,
        .access_cb = gatt_rx_cb,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {
        .uuid = &kTxUuid.u,
        .access_cb = gatt_tx_cb,
        .flags = BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &g_tx_val_handle,
    },
    {0},
};

const struct ble_gatt_svc_def kNusSvcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &kSvcUuid.u,
        .characteristics = kNusChrs,
    },
    {0},
};

int gap_event(struct ble_gap_event* event, void* arg) {
    (void)arg;
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                g_conn_handle = event->connect.conn_handle;
                g_connected = true;
                ESP_LOGI(TAG, "central connected (handle %d)", g_conn_handle);
                // Push for audio-grade throughput. The central (macOS) clamps
                // these to its own policy, so treat failures as advisory.
                struct ble_gap_upd_params up;
                std::memset(&up, 0, sizeof(up));
                up.itvl_min = 12;            // 15 ms  (units of 1.25 ms)
                up.itvl_max = 24;            // 30 ms
                up.latency = 0;
                up.supervision_timeout = 400;  // 4 s   (units of 10 ms)
                int urc = ble_gap_update_params(event->connect.conn_handle, &up);
                if (urc != 0) ESP_LOGW(TAG, "update_params rc=%d", urc);
                // Longer link-layer PDUs so a full MTU rides in one packet.
                ble_gap_set_data_len(event->connect.conn_handle, 251, 2120);
            } else {
                ESP_LOGW(TAG, "connect failed (status %d); re-advertising",
                         event->connect.status);
                start_advertising();
            }
            return 0;
        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "ATT MTU now %d", event->mtu.value);
            return 0;
        case BLE_GAP_EVENT_CONN_UPDATE:
            ESP_LOGI(TAG, "conn params updated (status %d)", event->conn_update.status);
            return 0;
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "central disconnected (reason %d); re-advertising",
                     event->disconnect.reason);
            g_connected = false;
            g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            start_advertising();
            return 0;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            start_advertising();
            return 0;
        default:
            return 0;
    }
}

void start_advertising() {
    struct ble_gap_adv_params adv_params;
    std::memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    // Advertising packet: flags + complete name (the 128-bit UUID is 16B and
    // won't co-fit with the name in 31 bytes, so it goes in the scan rsp).
    struct ble_hs_adv_fields fields;
    std::memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = reinterpret_cast<uint8_t*>(g_device_name);
    fields.name_len = static_cast<uint8_t>(std::strlen(g_device_name));
    fields.name_is_complete = 1;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) ESP_LOGE(TAG, "adv_set_fields rc=%d", rc);

    struct ble_hs_adv_fields rsp;
    std::memset(&rsp, 0, sizeof(rsp));
    rsp.uuids128 = const_cast<ble_uuid128_t*>(&kSvcUuid);
    rsp.num_uuids128 = 1;
    rsp.uuids128_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc != 0) ESP_LOGE(TAG, "adv_rsp_set_fields rc=%d", rc);

    rc = ble_gap_adv_start(g_own_addr_type, nullptr, BLE_HS_FOREVER, &adv_params,
                           gap_event, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_start rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "advertising as '%s'", g_device_name);
    }
}

void on_sync() {
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) ESP_LOGE(TAG, "ensure_addr rc=%d", rc);
    rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "infer_auto rc=%d", rc);
        return;
    }
    start_advertising();
}

void on_reset(int reason) { ESP_LOGW(TAG, "nimble host reset; reason=%d", reason); }

void host_task(void* param) {
    (void)param;
    nimble_port_run();  // returns only on nimble_port_stop()
    nimble_port_freertos_deinit();
}

}  // namespace

void Esp32BleTransport::start(std::string_view device_name) noexcept {
    g_instance = this;
    if (!device_name.empty()) {
        const std::size_t n = device_name.size() < sizeof(g_device_name) - 1
                                  ? device_name.size()
                                  : sizeof(g_device_name) - 1;
        std::memcpy(g_device_name, device_name.data(), n);
        g_device_name[n] = '\0';
    }

    g_rx_msgbuf = xMessageBufferCreate(kRxQueueBytes);
    if (g_rx_msgbuf == nullptr) ESP_LOGE(TAG, "RX queue alloc failed");
    g_rx_accum_len = 0;
    g_rx_overflow = false;

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", err);
        return;
    }

    ble_svc_gap_init();
    ble_svc_gatt_init();

    if (int mrc = ble_att_set_preferred_mtu(kPreferredMtu); mrc != 0)
        ESP_LOGW(TAG, "set_preferred_mtu rc=%d", mrc);

    int rc = ble_gatts_count_cfg(kNusSvcs);
    if (rc != 0) { ESP_LOGE(TAG, "gatts_count_cfg rc=%d", rc); return; }
    rc = ble_gatts_add_svcs(kNusSvcs);
    if (rc != 0) { ESP_LOGE(TAG, "gatts_add_svcs rc=%d", rc); return; }

    rc = ble_svc_gap_device_name_set(g_device_name);
    if (rc != 0) ESP_LOGW(TAG, "device_name_set rc=%d", rc);

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    nimble_port_freertos_init(host_task);
    ESP_LOGI(TAG, "NimBLE NUS started; device name '%s'", g_device_name);
}

void Esp32BleTransport::stop() noexcept {
    if (g_connected) {
        ble_gap_terminate(g_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    nimble_port_stop();
}

bool Esp32BleTransport::is_connected() const noexcept { return g_connected; }

bool Esp32BleTransport::has_incoming() const noexcept {
    return g_rx_msgbuf != nullptr && !xMessageBufferIsEmpty(g_rx_msgbuf);
}

std::size_t Esp32BleTransport::pop_incoming(char* out_buf, std::size_t max_len) noexcept {
    if (g_rx_msgbuf == nullptr) return 0;
    return xMessageBufferReceive(g_rx_msgbuf, out_buf, max_len, 0);
}

bool Esp32BleTransport::send_line(std::string_view line) noexcept {
    if (!g_connected || g_tx_val_handle == 0) return false;
    const uint16_t conn = g_conn_handle;

    // A notification carries at most ATT_MTU-3 bytes; anything longer is
    // silently truncated by the stack. Split the line into MTU-sized runs
    // and send a trailing '\n' as its own (tiny) run so the central's line
    // framer reassembles regardless of where the splits fall. Audio frames
    // (~950 B of base64 JSON) need this; control events usually fit one run.
    uint16_t mtu = ble_att_mtu(conn);
    if (mtu < 23) mtu = 23;  // not yet exchanged — fall back to the default
    const std::size_t chunk = static_cast<std::size_t>(mtu) - 3;

    const auto* p = reinterpret_cast<const uint8_t*>(line.data());
    std::size_t remaining = line.size();
    while (remaining > 0) {
        const std::size_t take = remaining < chunk ? remaining : chunk;
        if (!notify_bytes(conn, p, take)) return false;
        p += take;
        remaining -= take;
    }
    static const uint8_t kNewline = '\n';
    return notify_bytes(conn, &kNewline, 1);
}

}  // namespace pre_buddy::esp32
