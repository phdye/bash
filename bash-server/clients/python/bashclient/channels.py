"""Channel helper classes for bash-server v2 protocol."""

import asyncio
import base64
from typing import Any, Callable, Coroutine, Dict, List, Optional

from .errors import AuthError, ServerError, TimeoutError
from .protocol import b64decode, b64encode, make_msg
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

Callback = Callable[..., Any]
AsyncCallback = Callable[..., Coroutine[Any, Any, Any]]


class ControlChannel:
    """Channel 0: auth, ping, configure, disconnect."""

    def __init__(self, send_fn: Any, recv_fn: Any) -> None:
        self._send = send_fn
        self._recv = recv_fn

    async def auth(self, token: str) -> Dict[str, Any]:
        await self._send(make_msg(CHAN_CONTROL, "auth", token=token))
        resp = await self._recv(CHAN_CONTROL)
        if resp.get("type") == "error":
            raise AuthError(resp.get("message", "authentication failed"))
        if resp.get("type") != "auth_ok":
            raise AuthError(f"unexpected auth response: {resp.get('type')}")
        return resp

    async def ping(self) -> None:
        await self._send(make_msg(CHAN_CONTROL, "ping"))
        resp = await self._recv(CHAN_CONTROL)
        if resp.get("type") != "pong":
            raise ServerError(f"expected pong, got {resp.get('type')}")

    async def configure(self, **kwargs: Any) -> Dict[str, Any]:
        await self._send(make_msg(CHAN_CONTROL, "configure", **kwargs))
        return await self._recv(CHAN_CONTROL)

    async def disconnect(self) -> None:
        await self._send(make_msg(CHAN_CONTROL, "disconnect"))
        try:
            await self._recv(CHAN_CONTROL)
        except Exception:
            pass  # server may close before responding


class CommandChannel:
    """Channel 1: command evaluation."""

    def __init__(self, send_fn: Any, recv_fn: Any) -> None:
        self._send = send_fn
        self._recv = recv_fn

    async def eval(
        self, command: str, id: Optional[str] = None, timeout: float = 30.0
    ) -> EvalResult:
        msg = make_msg(CHAN_COMMAND, "eval", command=command)
        if id is not None:
            msg["id"] = id
        await self._send(msg)

        stdout = ""
        stderr = ""
        exit_code = -1

        # Collect stdout, stderr, complete messages
        for _ in range(3):
            resp = await asyncio.wait_for(self._recv(CHAN_COMMAND), timeout=timeout)
            t = resp.get("type")
            if t == "stdout":
                data = resp.get("data", "")
                if resp.get("encoding") == "base64":
                    data = b64decode(data)
                stdout = data
            elif t == "stderr":
                data = resp.get("data", "")
                if resp.get("encoding") == "base64":
                    data = b64decode(data)
                stderr = data
            elif t == "complete":
                exit_code = resp.get("exit_code", -1)
            elif t == "error":
                raise ServerError(resp.get("message", "eval error"), CHAN_COMMAND)

        return EvalResult(stdout=stdout, stderr=stderr, exit_code=exit_code)


class StateChannel:
    """Channel 2: variable, function, alias, and trap management."""

    def __init__(self, send_fn: Any, recv_fn: Any) -> None:
        self._send = send_fn
        self._recv = recv_fn

    async def _request(self, msg: Dict[str, Any]) -> Dict[str, Any]:
        await self._send(msg)
        resp = await self._recv(CHAN_STATE)
        if resp.get("type") == "error":
            raise ServerError(resp.get("message", "state error"), CHAN_STATE)
        return resp

    # --- Variables ---

    async def get_var(self, name: str) -> VarInfo:
        resp = await self._request(
            make_msg(CHAN_STATE, "get", target="var", name=name)
        )
        return VarInfo(
            name=resp.get("name", name),
            value=resp.get("value", ""),
            attributes=resp.get("attributes", []),
        )

    async def set_var(
        self,
        name: str,
        value: str,
        attributes: Optional[List[str]] = None,
    ) -> None:
        msg = make_msg(CHAN_STATE, "set", target="var", name=name, value=value)
        if attributes:
            msg["attributes"] = attributes
        await self._request(msg)

    async def unset_var(self, name: str) -> None:
        await self._request(make_msg(CHAN_STATE, "unset", target="var", name=name))

    # --- Functions ---

    async def get_func(self, name: str) -> FuncInfo:
        resp = await self._request(
            make_msg(CHAN_STATE, "get", target="function", name=name)
        )
        return FuncInfo(
            name=resp.get("name", name),
            definition=resp.get("value", ""),
        )

    async def unset_func(self, name: str) -> None:
        await self._request(
            make_msg(CHAN_STATE, "unset", target="function", name=name)
        )

    # --- Aliases ---

    async def get_alias(self, name: str) -> AliasInfo:
        resp = await self._request(
            make_msg(CHAN_STATE, "get", target="alias", name=name)
        )
        return AliasInfo(
            name=resp.get("name", name),
            value=resp.get("value", ""),
        )

    async def set_alias(self, name: str, value: str) -> None:
        await self._request(
            make_msg(CHAN_STATE, "set", target="alias", name=name, value=value)
        )

    async def unset_alias(self, name: str) -> None:
        await self._request(
            make_msg(CHAN_STATE, "unset", target="alias", name=name)
        )

    # --- Traps ---

    async def set_trap(self, signal: str, command: str) -> None:
        await self._request(
            make_msg(CHAN_STATE, "set", target="trap", name=signal, value=command)
        )

    async def unset_trap(self, signal: str) -> None:
        await self._request(
            make_msg(CHAN_STATE, "unset", target="trap", name=signal)
        )

    # --- Inspect ---

    async def inspect(self, query: str) -> List[Dict[str, Any]]:
        resp = await self._request(
            make_msg(CHAN_STATE, "inspect", query=query)
        )
        return resp.get("data", [])


class ObserveChannel:
    """Channel 3: command observation events."""

    def __init__(self, send_fn: Any, recv_fn: Any) -> None:
        self._send = send_fn
        self._recv = recv_fn
        self._callbacks: Dict[str, List[Callback]] = {}
        self._listening = False

    async def subscribe(self, level: int = 1) -> None:
        await self._send(make_msg(CHAN_OBSERVE, "subscribe", level=level))
        resp = await self._recv(CHAN_OBSERVE)
        if resp.get("type") == "error":
            raise ServerError(resp.get("message", "subscribe error"), CHAN_OBSERVE)

    async def unsubscribe(self) -> None:
        await self._send(make_msg(CHAN_OBSERVE, "unsubscribe"))
        resp = await self._recv(CHAN_OBSERVE)
        if resp.get("type") == "error":
            raise ServerError(resp.get("message", "unsubscribe error"), CHAN_OBSERVE)

    def on(self, event: str, callback: Callback) -> None:
        self._callbacks.setdefault(event, []).append(callback)

    def off(self, event: str, callback: Optional[Callback] = None) -> None:
        if callback is None:
            self._callbacks.pop(event, None)
        elif event in self._callbacks:
            self._callbacks[event] = [
                cb for cb in self._callbacks[event] if cb is not callback
            ]

    async def dispatch(self, msg: Dict[str, Any]) -> None:
        """Dispatch a server-push message to registered callbacks."""
        t = msg.get("type", "")
        data = msg.get("data", {})
        if t == "pre_command":
            event = PreCommandEvent(
                seq=msg.get("seq", 0),
                timestamp=msg.get("timestamp", 0),
                command=data.get("command", ""),
                cwd=data.get("cwd", ""),
                line_number=data.get("line_number", 0),
                is_subshell=data.get("is_subshell", False),
                is_async=data.get("is_async", False),
            )
        elif t == "post_command":
            event = PostCommandEvent(
                seq=msg.get("seq", 0),
                timestamp=msg.get("timestamp", 0),
                command=data.get("command", ""),
                exit_status=data.get("exit_status", 0),
                signal_number=data.get("signal_number", 0),
                duration_ms=data.get("duration_ms", 0),
            )
        else:
            event = msg

        for cb in self._callbacks.get(t, []):
            if asyncio.iscoroutinefunction(cb):
                await cb(event)
            else:
                cb(event)


class DebugChannel:
    """Channel 4: debugging (breakpoints, stepping, AST)."""

    def __init__(self, send_fn: Any, recv_fn: Any) -> None:
        self._send = send_fn
        self._recv = recv_fn
        self._callbacks: Dict[str, List[Callback]] = {}

    async def _request(self, msg: Dict[str, Any]) -> Dict[str, Any]:
        await self._send(msg)
        resp = await self._recv(CHAN_DEBUG)
        if resp.get("type") == "error":
            raise ServerError(resp.get("message", "debug error"), CHAN_DEBUG)
        return resp

    async def enable(self) -> None:
        await self._request(make_msg(CHAN_DEBUG, "enable"))

    async def disable(self) -> None:
        await self._request(make_msg(CHAN_DEBUG, "disable"))

    async def status(self) -> DebugStatus:
        resp = await self._request(make_msg(CHAN_DEBUG, "status"))
        return DebugStatus(
            active=resp.get("active", False),
            mode=resp.get("mode", "run"),
            breakpoints=resp.get("breakpoints", 0),
            depth=resp.get("depth", 0),
        )

    async def add_breakpoint(
        self,
        kind: str,
        pattern: Optional[str] = None,
        line: Optional[int] = None,
        condition: Optional[str] = None,
    ) -> int:
        msg = make_msg(CHAN_DEBUG, "break", kind=kind)
        if pattern is not None:
            msg["pattern"] = pattern
        if line is not None:
            msg["line"] = line
        if condition is not None:
            msg["condition"] = condition
        resp = await self._request(msg)
        return resp.get("id", -1)

    async def remove_breakpoint(self, bp_id: int) -> bool:
        resp = await self._request(make_msg(CHAN_DEBUG, "delete", id=bp_id))
        return resp.get("found", False)

    async def enable_breakpoint(self, bp_id: int) -> bool:
        resp = await self._request(make_msg(CHAN_DEBUG, "enable_bp", id=bp_id))
        return resp.get("found", False)

    async def disable_breakpoint(self, bp_id: int) -> bool:
        resp = await self._request(make_msg(CHAN_DEBUG, "disable_bp", id=bp_id))
        return resp.get("found", False)

    async def list_breakpoints(self) -> List[Breakpoint]:
        resp = await self._request(make_msg(CHAN_DEBUG, "list"))
        result = []
        for bp in resp.get("data", []):
            result.append(
                Breakpoint(
                    id=bp.get("id", 0),
                    kind=bp.get("type", ""),
                    enabled=bp.get("enabled", True),
                    hit_count=bp.get("hit_count", 0),
                    pattern=bp.get("pattern"),
                    line=bp.get("line"),
                    condition=bp.get("condition"),
                )
            )
        return result

    async def continue_(self) -> None:
        await self._request(make_msg(CHAN_DEBUG, "continue"))

    async def step(self) -> None:
        await self._request(make_msg(CHAN_DEBUG, "step"))

    async def next(self) -> None:
        await self._request(make_msg(CHAN_DEBUG, "next"))

    async def finish(self) -> None:
        await self._request(make_msg(CHAN_DEBUG, "finish"))

    async def skip(self) -> None:
        await self._request(make_msg(CHAN_DEBUG, "skip"))

    async def inspect_ast(self) -> Dict[str, Any]:
        resp = await self._request(make_msg(CHAN_DEBUG, "inspect_ast"))
        return resp.get("data", {})

    def on(self, event: str, callback: Callback) -> None:
        self._callbacks.setdefault(event, []).append(callback)

    def off(self, event: str, callback: Optional[Callback] = None) -> None:
        if callback is None:
            self._callbacks.pop(event, None)
        elif event in self._callbacks:
            self._callbacks[event] = [
                cb for cb in self._callbacks[event] if cb is not callback
            ]

    async def dispatch(self, msg: Dict[str, Any]) -> None:
        t = msg.get("type", "")
        if t == "break_hit":
            event = BreakHitEvent(
                line=msg.get("line", 0),
                command=msg.get("command", ""),
                depth=msg.get("depth", 0),
            )
        else:
            event = msg

        for cb in self._callbacks.get(t, []):
            if asyncio.iscoroutinefunction(cb):
                await cb(event)
            else:
                cb(event)


class PtyChannel:
    """Channel 5: pseudo-terminal I/O."""

    def __init__(self, send_fn: Any, recv_fn: Any) -> None:
        self._send = send_fn
        self._recv = recv_fn
        self._callbacks: Dict[str, List[Callback]] = {}

    async def spawn(
        self,
        rows: int = 24,
        cols: int = 80,
        shell: Optional[str] = None,
        strip_ansi: Optional[bool] = None,
    ) -> PtyInfo:
        msg = make_msg(CHAN_PTY, "spawn", rows=rows, cols=cols)
        if shell is not None:
            msg["shell"] = shell
        if strip_ansi is not None:
            msg["strip_ansi"] = strip_ansi
        await self._send(msg)
        resp = await self._recv(CHAN_PTY)
        if resp.get("type") == "error":
            raise ServerError(resp.get("message", "pty spawn error"), CHAN_PTY)
        return PtyInfo(
            rows=resp.get("rows", rows),
            cols=resp.get("cols", cols),
            pid=resp.get("pid", 0),
            strip_ansi=resp.get("strip_ansi", False),
        )

    async def write_input(self, data: str) -> None:
        encoded = base64.b64encode(data.encode("utf-8")).decode("ascii")
        await self._send(
            make_msg(CHAN_PTY, "input", data=encoded, encoding="base64")
        )

    async def resize(self, rows: int, cols: int) -> None:
        await self._send(make_msg(CHAN_PTY, "resize", rows=rows, cols=cols))
        resp = await self._recv(CHAN_PTY)
        if resp.get("type") == "error":
            raise ServerError(resp.get("message", "resize error"), CHAN_PTY)

    async def signal(self, name: str) -> None:
        await self._send(make_msg(CHAN_PTY, "signal", signal=name))
        resp = await self._recv(CHAN_PTY)
        if resp.get("type") == "error":
            raise ServerError(resp.get("message", "signal error"), CHAN_PTY)

    async def close(self) -> None:
        await self._send(make_msg(CHAN_PTY, "close"))

    def on(self, event: str, callback: Callback) -> None:
        self._callbacks.setdefault(event, []).append(callback)

    def off(self, event: str, callback: Optional[Callback] = None) -> None:
        if callback is None:
            self._callbacks.pop(event, None)
        elif event in self._callbacks:
            self._callbacks[event] = [
                cb for cb in self._callbacks[event] if cb is not callback
            ]

    async def dispatch(self, msg: Dict[str, Any]) -> None:
        t = msg.get("type", "")
        if t == "output":
            data = msg.get("data", "")
            if msg.get("encoding") == "base64":
                data = base64.b64decode(data).decode("utf-8", errors="replace")
            for cb in self._callbacks.get("output", []):
                if asyncio.iscoroutinefunction(cb):
                    await cb(data)
                else:
                    cb(data)
        elif t == "exit":
            exit_code = msg.get("exit_code", -1)
            for cb in self._callbacks.get("exit", []):
                if asyncio.iscoroutinefunction(cb):
                    await cb(exit_code)
                else:
                    cb(exit_code)
        else:
            for cb in self._callbacks.get(t, []):
                if asyncio.iscoroutinefunction(cb):
                    await cb(msg)
                else:
                    cb(msg)
