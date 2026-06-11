"""Real BLE/NUS transport for PRE Buddy server.

The server is the BLE **central**: it scans for an ESP32 robot
advertising the Nordic UART Service (see ``shared/protocol/uuids.md``),
connects, subscribes to TX notifications, and writes lines to RX. The
public surface intentionally mirrors :class:`MockBleSession` so the
event pump and ``BuddyServer`` don't need to know which one they're
talking to.

Why an abstract backend
-----------------------

``bleak`` is platform-specific (CoreBluetooth / Bluez / WinRT under the
hood) and impossible to unit-test without an actual radio. We split the
transport into:

- :class:`BleNusTransport` — pure framing/queue logic, no BLE imports.
- :class:`BleBackend` — abstract sync interface the transport calls.
- :class:`BleakNusBackend` — real implementation built on ``bleak``.
- :class:`FakeBleBackend` — in-memory backend used by the tests.

A test can drive the full transport without any third-party deps; the
``bleak`` import only happens inside :class:`BleakNusBackend`.
"""

from __future__ import annotations

import asyncio
import threading
import time
from collections import deque
from concurrent.futures import TimeoutError as FuturesTimeoutError
from typing import Any, Deque, Optional, Protocol

from .transport import TransportError
from .uuids import NUS_RX_CHAR_UUID, NUS_SERVICE_UUID, NUS_TX_CHAR_UUID


class BleBackend(Protocol):
    """Sync surface the transport drives.

    Implementations may do work on a background thread / event loop, but
    must present a blocking, synchronous API here. Each method should be
    safe to call multiple times in sequence; ``connect`` is idempotent.
    """

    def connect(self, *, timeout_s: float) -> None: ...
    def disconnect(self) -> None: ...
    def is_connected(self) -> bool: ...
    def write_rx(self, payload: bytes) -> None: ...
    def pop_tx_line(self) -> Optional[str]:
        """Return the next decoded line from TX, or ``None`` if none queued."""


class BleNusTransport:
    """Drop-in replacement for :class:`MockBleSession` against real BLE.

    Same shape: ``open``, ``close``, ``send_line``, ``recv_line``,
    ``inject_inbound`` (here a no-op, kept so callers can be generic).

    Framing / MTU contract (matters once we stream audio):

    - TX (peripheral -> central, notifications): the firmware fragments
      each line into ATT_MTU-3 byte runs and terminates it with ``\\n``.
      The backend reassembles by newline (see
      :meth:`BleakNusBackend._on_notify`), so a single logical line may
      span many notifications — which is exactly what ~1 KB base64 audio
      frames need. Small control events still arrive in one notification.
    - RX (central -> peripheral, writes): we send one line per GATT write
      with no trailing newline; the peripheral reassembles the write and
      appends its own newline, so "one write == one line" holds. A write
      larger than the MTU rides a single reassembled GATT op, so control
      events are unaffected (audio *output* will move to write-without-
      response fragmentation when that path lands).
    """

    def __init__(self, backend: BleBackend, *, connect_timeout_s: float = 10.0) -> None:
        self._backend = backend
        self._connect_timeout_s = connect_timeout_s
        self.sent_lines: list[str] = []
        # Backwards compatibility with MockBleSession's eager inbound queue.
        self._injected: Deque[str] = deque()

    # ── lifecycle ──────────────────────────────────────────────────────

    @property
    def connected(self) -> bool:
        return self._backend.is_connected()

    def open(self) -> None:
        if self._backend.is_connected():
            return
        self._backend.connect(timeout_s=self._connect_timeout_s)
        if not self._backend.is_connected():
            raise TransportError("BLE backend reported not connected after connect()")

    def close(self) -> None:
        self._backend.disconnect()

    # ── data path ──────────────────────────────────────────────────────

    def send_line(self, line: str) -> None:
        if not self._backend.is_connected():
            raise TransportError("cannot send while disconnected")
        if "\n" in line:
            raise ValueError("send_line: payload must not contain newlines")
        # Terminate the line with '\n' so the peripheral's framer can
        # reassemble it from however many MTU-sized GATT writes the backend
        # splits it into (audio output frames exceed one write).
        self._backend.write_rx((line + "\n").encode("utf-8"))
        self.sent_lines.append(line)

    def recv_line(self) -> Optional[str]:
        if not self._backend.is_connected():
            raise TransportError("cannot receive while disconnected")
        if self._injected:
            return self._injected.popleft()
        return self._backend.pop_tx_line()

    def inject_inbound(self, line: str) -> None:
        """Compatibility hook with :class:`MockBleSession`.

        Tests that drive both transports through the same scenarios can
        keep using ``inject_inbound`` here. Real BLE notifications still
        flow through the backend; injected lines are read out first.
        """
        self._injected.append(line)


# ──────────────────────────────────────────────────────────────────────
# Test backend — in-memory, synchronous, no BLE.
# ──────────────────────────────────────────────────────────────────────

class FakeBleBackend:
    """In-memory backend that mimics the contract of :class:`BleBackend`.

    - ``connect`` flips the connected flag (or raises if armed to fail).
    - ``write_rx`` records writes in ``rx_writes``.
    - ``push_tx`` lets tests simulate the peripheral sending a line.
    - ``pop_tx_line`` returns the next queued TX line.
    """

    def __init__(self, *, fail_on_connect: bool = False) -> None:
        self._connected = False
        self._fail_on_connect = fail_on_connect
        self.rx_writes: list[bytes] = []
        self.connect_calls = 0
        self.disconnect_calls = 0
        self._tx_queue: Deque[str] = deque()

    def connect(self, *, timeout_s: float) -> None:
        self.connect_calls += 1
        if self._fail_on_connect:
            raise TransportError("simulated connect failure")
        self._connected = True

    def disconnect(self) -> None:
        self.disconnect_calls += 1
        self._connected = False

    def is_connected(self) -> bool:
        return self._connected

    def write_rx(self, payload: bytes) -> None:
        if not self._connected:
            raise TransportError("write_rx while disconnected")
        self.rx_writes.append(payload)

    def push_tx(self, line: str) -> None:
        """Simulate a notification from the peripheral."""
        self._tx_queue.append(line)

    def pop_tx_line(self) -> Optional[str]:
        if not self._tx_queue:
            return None
        return self._tx_queue.popleft()


# ──────────────────────────────────────────────────────────────────────
# Real backend — bleak, async on a dedicated thread.
# ──────────────────────────────────────────────────────────────────────

class BleakNusBackend:
    """Real BLE central built on ``bleak``.

    Bleak is async, but the transport surface is blocking. We solve this
    by spawning an internal asyncio loop on a dedicated thread and
    proxying ``connect``/``disconnect``/``write_rx`` to it via
    ``run_coroutine_threadsafe``. Notifications land in a thread-safe
    ``deque`` that the sync ``pop_tx_line`` drains.

    The ``bleak`` import is performed lazily so the module is importable
    in environments without it (CI without the ``[transport]`` extra).
    """

    def __init__(self, *, address: Optional[str] = None, name: Optional[str] = None) -> None:
        if not address and not name:
            raise ValueError("BleakNusBackend requires either address= or name=")
        self._address = address
        self._name = name
        self._client: Any = None
        self._loop: Optional[asyncio.AbstractEventLoop] = None
        self._thread: Optional[threading.Thread] = None
        self._tx_lock = threading.Lock()
        self._tx_queue: Deque[str] = deque()
        self._tx_buffer = bytearray()
        self._wwr_logged = False  # one-shot log of the chosen write mode
        self._frag_counter = 0    # spans send_rx calls for the hybrid-ack barrier

    def _cb_peripheral(self) -> Any:
        """The underlying CoreBluetooth CBPeripheral, used for
        write-without-response flow control. Returns None on non-macOS or if
        bleak's internals differ (caller then uses reliable response=True)."""
        obj: Any = self._client
        try:
            for attr in ("_backend", "_delegate", "peripheral"):
                obj = getattr(obj, attr)
            return obj if hasattr(obj, "canSendWriteWithoutResponse") else None
        except Exception:
            return None

    # ── loop plumbing ──────────────────────────────────────────────────

    def _ensure_loop(self) -> asyncio.AbstractEventLoop:
        if self._loop is not None and self._loop.is_running():
            return self._loop
        loop = asyncio.new_event_loop()

        def _run() -> None:
            asyncio.set_event_loop(loop)
            loop.run_forever()

        thread = threading.Thread(target=_run, name="bleak-nus-loop", daemon=True)
        thread.start()
        self._loop = loop
        self._thread = thread
        # Wait briefly for the loop to start serving callbacks.
        deadline = time.monotonic() + 1.0
        while not loop.is_running() and time.monotonic() < deadline:
            time.sleep(0.01)
        return loop

    def _call(self, coro: Any, timeout_s: float) -> Any:
        loop = self._ensure_loop()
        fut = asyncio.run_coroutine_threadsafe(coro, loop)
        try:
            return fut.result(timeout=timeout_s)
        except FuturesTimeoutError:
            # Cancel the orphaned coroutine. If we let it keep running, the NEXT
            # write_gatt_char starts concurrently and collides with it on
            # bleak's per-characteristic future map (KeyError on the char
            # handle), corrupting the delegate's state for the rest of the run.
            fut.cancel()
            raise

    # ── BleBackend surface ────────────────────────────────────────────

    def is_connected(self) -> bool:
        client = self._client
        return bool(client and getattr(client, "is_connected", False))

    def connect(self, *, timeout_s: float) -> None:
        if self.is_connected():
            return

        try:
            from bleak import BleakClient, BleakScanner  # type: ignore[import-not-found]
        except ImportError as exc:  # pragma: no cover - import path
            raise RuntimeError(
                "BleakNusBackend requires the 'bleak' package. "
                "Install with: pip install 'pre-buddy[transport]'"
            ) from exc

        async def _connect_async() -> None:
            target = self._address
            if not target:
                device = await BleakScanner.find_device_by_name(self._name, timeout=timeout_s)
                if device is None:
                    raise TransportError(f"no BLE device named {self._name!r} found")
                target = device.address

            client = BleakClient(target, services=[NUS_SERVICE_UUID])
            await client.connect(timeout=timeout_s)

            def _on_notify(_handle: Any, data: bytearray) -> None:
                # A logical line is fragmented across notifications when it
                # exceeds ATT_MTU-3; the firmware terminates every line with
                # '\n', so we accumulate and split strictly on newline. (No
                # "looks like complete JSON" heuristic — under load a chunk
                # boundary can land right after a '}' mid-line and that would
                # mis-split a frame.)
                self._tx_buffer.extend(data)
                while b"\n" in self._tx_buffer:
                    line, _, rest = self._tx_buffer.partition(b"\n")
                    self._tx_buffer = bytearray(rest)
                    with self._tx_lock:
                        self._tx_queue.append(line.decode("utf-8", errors="replace"))

            await client.start_notify(NUS_TX_CHAR_UUID, _on_notify)
            self._client = client

        self._call(_connect_async(), timeout_s + 1.0)

    def disconnect(self) -> None:
        client = self._client
        if not client:
            self._stop_loop()
            return

        async def _disconnect_async() -> None:
            try:
                await client.stop_notify(NUS_TX_CHAR_UUID)
            except Exception:
                pass
            try:
                await client.disconnect()
            except Exception:
                pass

        try:
            self._call(_disconnect_async(), timeout_s=5.0)
        finally:
            self._client = None
            self._stop_loop()

    def write_rx(self, payload: bytes) -> None:
        client = self._client
        if not client:
            raise TransportError("write_rx while disconnected")

        async def _write_async() -> None:
            # HYBRID-ACK fragmentation. Pure write-with-response is reliable but
            # ACK-stalls a full connection interval PER fragment (~3 KB/s). Pure
            # write-without-response is fast but bleak fires it and forgets, so
            # it overflows CoreBluetooth's queue / the device and silently drops.
            # So: send WITHOUT response, but every Nth fragment WITH response as
            # a flow-control barrier. The acked write blocks until the device has
            # drained the burst before it, bounding the in-flight depth (no
            # overflow) while paying the ACK cost only 1/N of the time (~Nx
            # faster). The device's multi-line RX queue absorbs each burst.
            ACK_EVERY = 8
            mtu = getattr(client, "mtu_size", 23) or 23
            chunk = max(20, mtu - 3)
            peripheral = self._cb_peripheral()  # for canSendWriteWithoutResponse
            if not self._wwr_logged:
                self._wwr_logged = True
                import sys
                paced = "canSend-paced" if peripheral is not None else "unpaced"
                print(f"[ble] output writes: hybrid-ack (1 ack / {ACK_EVERY}, {paced}), mtu={mtu}",
                      file=sys.stderr, flush=True)
            for i in range(0, len(payload), chunk):
                self._frag_counter += 1
                acked = (self._frag_counter % ACK_EVERY == 0)
                # Before an unacked write, wait until CoreBluetooth says it can
                # take one (its WWR queue is shallow — firing blind drops). Acked
                # writes block on the ATT response anyway, so they don't need it.
                if not acked and peripheral is not None:
                    spins = 0
                    while not peripheral.canSendWriteWithoutResponse() and spins < 2000:
                        await asyncio.sleep(0.001)
                        spins += 1
                await client.write_gatt_char(
                    NUS_RX_CHAR_UUID, payload[i:i + chunk], response=acked)

        # Per-line deadline. A frame's fragments stream at link rate with only
        # occasional ACK barriers; 4 s is generous. If a barrier write stalls
        # longer the device isn't draining, so time out, cancel the coroutine
        # (see _call), and let the loop drop this one frame rather than wedge.
        self._call(_write_async(), timeout_s=4.0)

    def pop_tx_line(self) -> Optional[str]:
        with self._tx_lock:
            if not self._tx_queue:
                return None
            return self._tx_queue.popleft()

    # ── shutdown ──────────────────────────────────────────────────────

    def _stop_loop(self) -> None:
        loop = self._loop
        if loop and loop.is_running():
            loop.call_soon_threadsafe(loop.stop)
        thread = self._thread
        if thread and thread.is_alive():
            thread.join(timeout=2.0)
        self._loop = None
        self._thread = None
