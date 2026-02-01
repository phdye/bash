"""bashclient - Python client for bash-server v2 NDJSON protocol."""

from .client import BashClient
from .errors import (
    AuthError,
    BashClientError,
    ProtocolError,
    ServerError,
    TimeoutError,
    TransportError,
)
from .types import (
    CHAN_COMMAND,
    CHAN_CONTROL,
    CHAN_DEBUG,
    CHAN_OBSERVE,
    CHAN_PTY,
    CHAN_STATE,
    AliasInfo,
    BreakHitEvent,
    Breakpoint,
    DebugStatus,
    EvalResult,
    FuncInfo,
    PostCommandEvent,
    PreCommandEvent,
    PtyInfo,
    TrapInfo,
    VarInfo,
)

__all__ = [
    "BashClient",
    "AuthError",
    "BashClientError",
    "ProtocolError",
    "ServerError",
    "TimeoutError",
    "TransportError",
    "EvalResult",
    "VarInfo",
    "FuncInfo",
    "AliasInfo",
    "TrapInfo",
    "PreCommandEvent",
    "PostCommandEvent",
    "Breakpoint",
    "BreakHitEvent",
    "DebugStatus",
    "PtyInfo",
    "CHAN_CONTROL",
    "CHAN_COMMAND",
    "CHAN_STATE",
    "CHAN_OBSERVE",
    "CHAN_DEBUG",
    "CHAN_PTY",
]

__version__ = "0.1.0"
