# pre_buddy server package

Python package providing the `pre-buddy` CLI and server-side PRE Buddy support.

Current scope:
- event envelope/types for `pre.*` namespace
- JSON-lines serializer/deserializer
- mock BLE session + event pump runtime
- software robot simulator (`simulate`) for scenario playback

Quick commands:
- `pre-buddy serve --demo` (run mock server and print outbound JSON-lines)
- `pre-buddy simulate --playback server/examples/alerts_scenario.jsonl --character sentinel`

See the repository root `DESIGN.md` for full product context.
