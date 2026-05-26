// SPDX-License-Identifier: TBD
// PRE Buddy — robot loop: glue between protocol events and the HAL.
//
// RobotLoop owns three references (servo / LED / display) and the
// current character. ``dispatch(Event)`` runs the existing
// map_event() to get an EmbodimentCommand, clamps it via MotionEngine
// for safety, and pushes the result to the HAL.
//
// Intentionally minimal: no time-based behavior yet (blink, idle return-
// to-center) — that lives in a separate timer in firmware/esp32 once
// the board lands. Anything host-testable belongs here.

#pragma once

#include "pre_buddy/character.h"
#include "pre_buddy/hal/i_display.h"
#include "pre_buddy/hal/i_led.h"
#include "pre_buddy/hal/i_servo.h"
#include "pre_buddy/motion.h"
#include "pre_buddy/protocol.h"

namespace pre_buddy {

class RobotLoop {
   public:
    RobotLoop(Character initial_character,
              hal::IServoDriver& servo,
              hal::ILedDriver& led,
              hal::IDisplayDriver& display) noexcept
        : character_(initial_character),
          servo_(servo),
          led_(led),
          display_(display),
          motion_(MotionEngine{}) {}

    Character character() const noexcept { return character_; }

    // Drive every output that maps to ``ev`` for the current character.
    // Returns the EmbodimentCommand that was executed (useful for tests
    // and for logging on the device).
    EmbodimentCommand dispatch(const Event& ev) noexcept {
        EmbodimentCommand cmd = map_event(ev, character_);
        led_.set_color(cmd.led);

        if (cmd.has_motion) {
            cmd.motion = motion_.clamp(cmd.motion);
            servo_.move(cmd.motion);
        }

        // CharacterSet is the only event that mutates RobotLoop state.
        if (ev.kind == EventKind::CharacterSet) {
            character_ = ev.character;
            display_.show_character(character_);
        }

        return cmd;
    }

    // Snap to a safe resting pose and the idle LED. Called on boot, on
    // pairing complete, and after recovering from error states.
    void reset_to_idle() noexcept {
        const CharacterProfile prof = profile_for(character_);
        servo_.rest();
        led_.set_color(prof.idle_color);
        display_.show_character(character_);
    }

   private:
    Character character_;
    hal::IServoDriver& servo_;
    hal::ILedDriver& led_;
    hal::IDisplayDriver& display_;
    MotionEngine motion_;
};

}  // namespace pre_buddy
