"""NDJSON protocol implementation for bash-server v2."""

import base64
import json
from typing import Any, Dict, Optional

from .errors import ProtocolError
from .types import FRAME_MAX_PAYLOAD


def encode_frame(msg: Dict[str, Any]) -> bytes:
    """Encode a message dict as an NDJSON line."""
    try:
        return json.dumps(msg, separators=(",", ":")).encode("utf-8") + b"\n"
    except (TypeError, ValueError) as e:
        raise ProtocolError(f"cannot encode frame: {e}") from e


def decode_frame(line: bytes) -> Dict[str, Any]:
    """Decode an NDJSON line into a message dict."""
    line = line.strip()
    if not line:
        raise ProtocolError("empty frame")
    if len(line) > FRAME_MAX_PAYLOAD:
        raise ProtocolError(
            f"frame exceeds max payload ({len(line)} > {FRAME_MAX_PAYLOAD})"
        )
    try:
        msg = json.loads(line)
    except json.JSONDecodeError as e:
        raise ProtocolError(f"invalid JSON: {e}") from e
    if not isinstance(msg, dict):
        raise ProtocolError(f"expected JSON object, got {type(msg).__name__}")
    return msg


def b64encode(data: str) -> str:
    """Base64-encode a string."""
    return base64.b64encode(data.encode("utf-8")).decode("ascii")


def b64decode(data: str) -> str:
    """Base64-decode a string."""
    try:
        return base64.b64decode(data).decode("utf-8")
    except Exception as e:
        raise ProtocolError(f"base64 decode error: {e}") from e


def make_msg(channel: int, msg_type: str, **kwargs: Any) -> Dict[str, Any]:
    """Build a protocol message dict."""
    msg: Dict[str, Any] = {"ch": channel, "type": msg_type}
    msg.update(kwargs)
    return msg


def get_channel(msg: Dict[str, Any]) -> int:
    """Extract channel ID from message, defaulting to 0 (CONTROL)."""
    return msg.get("ch", 0)


def get_type(msg: Dict[str, Any]) -> str:
    """Extract message type."""
    t = msg.get("type")
    if t is None:
        raise ProtocolError("message has no 'type' field")
    return str(t)
