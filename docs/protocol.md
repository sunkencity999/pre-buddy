# Protocol

The canonical wire-protocol specification lives in
[`../shared/protocol/events.md`](../shared/protocol/events.md). This page is a
human-friendly companion: design notes, change history, and worked examples.

> 🚧 Placeholder. Filled in as the protocol stabilizes. See DESIGN.md §7.

## Why one source of truth?

Both the C++ firmware core (`firmware/core/include/pre_buddy/protocol.h`) and
the Python server package (`server/pre_buddy/events.py`) must agree on every
event name and payload field. The shared spec at `shared/protocol/events.md`
is the contract — update it first, then mirror in both sides, then update
tests on both sides in the same commit.

## Anthropic-compat boundary

We reuse Anthropic's BLE NUS UUIDs and JSON-lines framing. They own anything
without a `pre.` prefix; we own anything with one. No fork; both stacks can
coexist on the same device.
