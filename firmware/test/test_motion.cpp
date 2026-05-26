#include "pre_buddy/motion.h"
#include "test_harness.h"

using namespace pre_buddy;

PRE_TEST(motion_y_axis_clamps_to_safety_range) {
    MotionEngine eng;
    auto low  = eng.clamp({0.0f, -10.0f, 500});
    auto high = eng.clamp({0.0f, 999.0f, 500});
    auto ok   = eng.clamp({0.0f, 45.0f, 500});

    PRE_CHECK_NEAR(low.head_y_deg,  10.0f, 1e-6);
    PRE_CHECK_NEAR(high.head_y_deg, 80.0f, 1e-6);
    PRE_CHECK_NEAR(ok.head_y_deg,   45.0f, 1e-6);
}

PRE_TEST(motion_clamp_does_not_touch_x_axis) {
    MotionEngine eng;
    auto out = eng.clamp({180.0f, 50.0f, 500});
    PRE_CHECK_NEAR(out.head_x_deg, 180.0f, 1e-6);
}

PRE_TEST(motion_rate_limiter_first_call_is_passthrough) {
    MotionEngine eng;
    auto out = eng.rate_limit({90.0f, 45.0f, 100});
    PRE_CHECK_EQ(out.duration_ms, 100u);
    PRE_CHECK_NEAR(out.head_x_deg, 90.0f, 1e-6);
}

PRE_TEST(motion_rate_limiter_stretches_duration_for_big_jumps) {
    // Default cap: 180°/s. 180° jump must take >= 1000 ms.
    MotionEngine eng;
    (void)eng.rate_limit({0.0f, 45.0f, 50});       // seed last_x
    auto out = eng.rate_limit({180.0f, 45.0f, 100});
    PRE_CHECK(out.duration_ms >= 1000u);
}

PRE_TEST(motion_rate_limiter_keeps_small_moves_snappy) {
    MotionEngine eng;
    (void)eng.rate_limit({0.0f, 45.0f, 50});
    // 10° jump at 180°/s would need ~55ms; 200ms budget is plenty.
    auto out = eng.rate_limit({10.0f, 45.0f, 200});
    PRE_CHECK_EQ(out.duration_ms, 200u);
}

PRE_TEST(motion_sanitize_does_both) {
    MotionEngine eng;
    (void)eng.sanitize({0.0f, 45.0f, 50});
    auto out = eng.sanitize({200.0f, 999.0f, 50});
    PRE_CHECK_NEAR(out.head_y_deg, 80.0f, 1e-6);   // clamped
    PRE_CHECK(out.duration_ms >= 1000u);           // rate limited (200° travel)
}
