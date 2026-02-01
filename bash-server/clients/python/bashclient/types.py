"""Message type definitions for bash-server v2 NDJSON protocol."""

from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional


# Channel IDs
CHAN_CONTROL = 0
CHAN_COMMAND = 1
CHAN_STATE = 2
CHAN_OBSERVE = 3
CHAN_DEBUG = 4
CHAN_PTY = 5
CHAN_MAX = 5

# Protocol limits
FRAME_MAX_PAYLOAD = 1024 * 1024  # 1MB
TOKEN_HEXLEN = 64

# Observe levels
OBSERVE_LEVEL_OFF = 0
OBSERVE_LEVEL_COMMAND = 1
OBSERVE_LEVEL_MAX = 1


@dataclass
class EvalResult:
    """Result of a command evaluation."""
    stdout: str
    stderr: str
    exit_code: int


@dataclass
class VarInfo:
    """Variable information."""
    name: str
    value: str
    attributes: List[str] = field(default_factory=list)


@dataclass
class FuncInfo:
    """Function information."""
    name: str
    definition: str


@dataclass
class AliasInfo:
    """Alias information."""
    name: str
    value: str


@dataclass
class TrapInfo:
    """Trap information."""
    signal: str
    command: str


@dataclass
class PreCommandEvent:
    """Pre-command observation event."""
    seq: int
    timestamp: int
    command: str
    cwd: str
    line_number: int = 0
    is_subshell: bool = False
    is_async: bool = False


@dataclass
class PostCommandEvent:
    """Post-command observation event."""
    seq: int
    timestamp: int
    command: str
    exit_status: int
    signal_number: int = 0
    duration_ms: int = 0


@dataclass
class Breakpoint:
    """Debug breakpoint."""
    id: int
    kind: str  # "command", "line", "function"
    enabled: bool = True
    hit_count: int = 0
    pattern: Optional[str] = None
    line: Optional[int] = None
    condition: Optional[str] = None


@dataclass
class BreakHitEvent:
    """Debugger break hit event."""
    line: int
    command: str
    depth: int = 0


@dataclass
class DebugStatus:
    """Debugger status."""
    active: bool
    mode: str  # "run", "step", "next", "finish"
    breakpoints: int
    depth: int = 0


@dataclass
class PtyInfo:
    """PTY session info."""
    rows: int
    cols: int
    pid: int
    strip_ansi: bool = False
