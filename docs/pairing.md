# Pairing

> 🚧 Placeholder. Pairing UX mirrors Anthropic's passkey-on-device flow
> (DESIGN.md §8 / §11 / decision row 5).

Pairing flow (planned):

1. Device powers on, advertises NUS service.
2. PRE web GUI shows a "Pair PRE Buddy" panel matching Anthropic's layout.
3. Device generates a 6-digit passkey, displays it on the 2" IPS screen.
4. User confirms the passkey on the GUI.
5. Bond stored on both sides; device transitions to first-boot character-select.

This document will be fleshed out once the BLE NUS peripheral (S0 in the
build plan) lands.
