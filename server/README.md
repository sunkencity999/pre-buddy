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
- `pre-buddy simulate --playback server/examples/alerts_scenario.jsonl --character sentinel --severity loud --format csv --out /tmp/alerts.csv`
- `pre-buddy simulate --playback server/examples/daily_flow_scenario.jsonl --character sprout --format json --out /tmp/daily.json`

Golden behavior snapshots:
- `server/tests/golden/*.json`
- CI-enforced via `server/tests/test_golden_scenarios.py`

See the repository root `DESIGN.md` for full product context.
