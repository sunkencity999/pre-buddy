// SPDX-License-Identifier: TBD
// Tests for the audio HAL mocks. The interface contracts are tiny —
// these tests pin them down so that drivers (mock + real) can't drift.

#include "pre_buddy/hal/mock_audio.h"
#include "test_harness.h"

using namespace pre_buddy;
using namespace pre_buddy::hal;

namespace {

struct MicRecorder {
    int frames = 0;
    std::vector<int16_t> last_frame;

    static void cb(const int16_t* frame, std::size_t n, void* user) noexcept {
        auto* self = static_cast<MicRecorder*>(user);
        ++self->frames;
        self->last_frame.assign(frame, frame + n);
    }
};

struct WakeRecorder {
    int fires = 0;
    std::string phrase;
    float confidence = 0.0f;

    static void cb(std::string_view phrase_sv, float conf, void* user) noexcept {
        auto* self = static_cast<WakeRecorder*>(user);
        ++self->fires;
        self->phrase.assign(phrase_sv);
        self->confidence = conf;
    }
};

}  // namespace

// ── mic ────────────────────────────────────────────────────────────


PRE_TEST(mock_mic_start_records_params) {
    MockMicDriver m;
    MicRecorder rec;
    m.start_capture(16000, 320, &MicRecorder::cb, &rec);
    PRE_CHECK(m.is_capturing());
    PRE_CHECK_EQ(m.last_sample_rate_hz, 16000u);
    PRE_CHECK_EQ(m.last_frame_size_samples, 320u);
}

PRE_TEST(mock_mic_inject_invokes_callback) {
    MockMicDriver m;
    MicRecorder rec;
    m.start_capture(16000, 4, &MicRecorder::cb, &rec);

    int16_t samples[4] = {100, 200, 300, 400};
    m.inject_frame(samples, 4);

    PRE_CHECK_EQ(rec.frames, 1);
    PRE_CHECK_EQ(rec.last_frame.size(), 4u);
    PRE_CHECK_EQ(rec.last_frame[2], 300);
}

PRE_TEST(mock_mic_stop_flips_state) {
    MockMicDriver m;
    m.start_capture(16000, 320, nullptr, nullptr);
    m.stop_capture();
    PRE_CHECK(!m.is_capturing());
    PRE_CHECK_EQ(m.stop_calls, 1);
}


// ── speaker ────────────────────────────────────────────────────────


PRE_TEST(mock_speaker_accepts_samples_after_start) {
    MockSpeakerDriver s;
    s.start_playback(16000);
    int16_t buf[3] = {1, 2, 3};
    auto n = s.play_frame(buf, 3);
    PRE_CHECK_EQ(n, 3u);
    PRE_CHECK_EQ(s.samples_received.size(), 3u);
    PRE_CHECK_EQ(s.samples_received[2], 3);
}

PRE_TEST(mock_speaker_drops_frames_when_stopped) {
    MockSpeakerDriver s;
    int16_t buf[2] = {1, 2};
    auto n = s.play_frame(buf, 2);
    PRE_CHECK_EQ(n, 0u);
    PRE_CHECK_EQ(s.samples_received.size(), 0u);
}

PRE_TEST(mock_speaker_honours_accept_limit_for_backpressure_simulation) {
    MockSpeakerDriver s;
    s.start_playback(16000);
    s.accept_limit_per_call = 2;
    int16_t buf[5] = {1, 2, 3, 4, 5};
    auto n = s.play_frame(buf, 5);
    PRE_CHECK_EQ(n, 2u);
    PRE_CHECK_EQ(s.samples_received.size(), 2u);
}


// ── wake-word detector ────────────────────────────────────────────


PRE_TEST(mock_wake_detector_uses_default_when_phrase_unsupported) {
    MockWakeWordDetector w;
    WakeRecorder rec;
    // "computer" not in supported list → falls back to "hey buddy".
    w.start("computer", &WakeRecorder::cb, &rec);
    PRE_CHECK(w.is_running());
    PRE_CHECK(w.active_phrase() == "hey buddy");
    PRE_CHECK(w.requested_phrase == "computer");
}

PRE_TEST(mock_wake_detector_honours_supported_phrase) {
    MockWakeWordDetector w;
    w.supported_phrases = {"hey buddy", "hey pre"};
    WakeRecorder rec;
    w.start("hey pre", &WakeRecorder::cb, &rec);
    PRE_CHECK(w.active_phrase() == "hey pre");
}

PRE_TEST(mock_wake_detector_fire_invokes_callback_with_active_phrase) {
    MockWakeWordDetector w;
    WakeRecorder rec;
    w.start("hey buddy", &WakeRecorder::cb, &rec);
    w.fire(0.87f);
    PRE_CHECK_EQ(rec.fires, 1);
    PRE_CHECK(rec.phrase == "hey buddy");
    PRE_CHECK_NEAR(rec.confidence, 0.87, 0.0001);
}

PRE_TEST(mock_wake_detector_stop_clears_phrase) {
    MockWakeWordDetector w;
    WakeRecorder rec;
    w.start("hey buddy", &WakeRecorder::cb, &rec);
    w.stop();
    PRE_CHECK(!w.is_running());
    PRE_CHECK(w.active_phrase().empty());
}
