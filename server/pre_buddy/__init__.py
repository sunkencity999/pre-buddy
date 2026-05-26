"""PRE Buddy server package.

Loaded by ``pre buddy serve`` (a subcommand under the main ``pre`` CLI) and
also exposes a small standalone ``pre-buddy`` script for development.

See ``../shared/protocol/events.md`` for the canonical wire protocol.
"""

from .events import (
    Character,
    EventKind,
    Event,
    WakeWordData,
    BgAgentChangeData,
    ConfidenceWarningData,
    ErrorData,
    CharacterSetData,
)
from .serializer import dumps, loads, dump_many, load_many

__all__ = [
    "Character",
    "EventKind",
    "Event",
    "WakeWordData",
    "BgAgentChangeData",
    "ConfidenceWarningData",
    "ErrorData",
    "CharacterSetData",
    "dumps",
    "loads",
    "dump_many",
    "load_many",
]

__version__ = "0.1.0"
