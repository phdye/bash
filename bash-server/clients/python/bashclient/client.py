"""BashClient: async client for bash-server v2 NDJSON protocol."""

import asyncio
from typing import Any, Dict, Optional

from .channels import (
    CommandChannel,
    ControlChannel,
    DebugChannel,
    ObserveChannel,
    PtyChannel,
    StateChannel,
)
from .errors import ProtocolError, TimeoutError, TransportError
from .protocol import decode_frame, encode_frame, get_channel, get_type
from .transport import (
    FdTransport,
    NamedPipeTransport,
    StdioTransport,
    Transport,
    UnixSocketTransport,
)
from .types import CHAN_COMMAND, CHAN_CONTROL, CHAN_DEBUG, CHAN_OBSERVE, CHAN_PTY, CHAN_STATE, EvalResult


class BashClient:
    """Async client for bash-server v2 NDJSON protocol.

    Usage::

        async with BashClient.connect("/tmp/bash-server-1000/sock") as client:
            await client.auth(token)
            result = await client.eval("echo hello")
            print(result.stdout)

    Or for stdio transport::

        async with BashClient.connect_stdio("bash-server", "--stdio") as client:
            ...
    """

    def __init__(self, transport: Transport) -> None:
        self._transport = transport
        self._channel_queues: Dict[int, asyncio.Queue] = {
            i: asyncio.Queue() for i in range(6)
        }
        self._reader_task: Optional[asyncio.Task] = None
        self._authenticated = False

        # Initialize channels
        self.control = ControlChannel(self._send, self._recv)
        self.command = CommandChannel(self._send, self._recv)
        self.state = StateChannel(self._send, self._recv)
        self.observe = ObserveChannel(self._send, self._recv)
        self.debug = DebugChannel(self._send, self._recv)
        self.pty = PtyChannel(self._send, self._recv)

    @classmethod
    async def connect(cls, socket_path: str) -> "BashClient":
        """Connect via Unix domain socket."""
        transport = UnixSocketTransport()
        await transport.connect(socket_path)
        client = cls(transport)
        client._start_reader()
        return client

    @classmethod
    async def connect_stdio(cls, *args: str) -> "BashClient":
        """Connect via subprocess stdin/stdout (--stdio mode)."""
        transport = StdioTransport()
        await transport.connect_process(*args)
        client = cls(transport)
        client._start_reader()
        return client

    @classmethod
    async def connect_fd(cls, fd: int) -> "BashClient":
        """Connect via inherited file descriptor."""
        transport = FdTransport()
        await transport.connect(fd)
        client = cls(transport)
        client._start_reader()
        return client

    @classmethod
    async def connect_named_pipe(cls, pipe_name: str) -> "BashClient":
        """Connect via Windows Named Pipe."""
        transport = NamedPipeTransport()
        await transport.connect(pipe_name)
        client = cls(transport)
        client._start_reader()
        return client

    @classmethod
    async def from_transport(cls, transport: Transport) -> "BashClient":
        """Create client from an existing transport."""
        client = cls(transport)
        client._start_reader()
        return client

    def _start_reader(self) -> None:
        """Start the background message reader loop."""
        self._reader_task = asyncio.ensure_future(self._reader_loop())

    async def _reader_loop(self) -> None:
        """Read messages and dispatch to channel queues."""
        try:
            while self._transport.is_open:
                try:
                    line = await self._transport.read_line()
                except TransportError:
                    break
                try:
                    msg = decode_frame(line)
                except ProtocolError:
                    continue

                ch = get_channel(msg)
                msg_type = msg.get("type", "")

                # Server-push messages go to dispatch
                if ch == CHAN_OBSERVE and msg_type in ("pre_command", "post_command"):
                    await self.observe.dispatch(msg)
                elif ch == CHAN_DEBUG and msg_type == "break_hit":
                    await self.debug.dispatch(msg)
                elif ch == CHAN_PTY and msg_type in ("output", "exit"):
                    await self.pty.dispatch(msg)
                else:
                    # Request/response messages go to channel queue
                    if 0 <= ch <= 5:
                        await self._channel_queues[ch].put(msg)
        except asyncio.CancelledError:
            pass
        except Exception:
            pass

    async def _send(self, msg: Dict[str, Any]) -> None:
        """Send a message to the server."""
        data = encode_frame(msg)
        await self._transport.write(data)

    async def _recv(self, channel: int, timeout: float = 30.0) -> Dict[str, Any]:
        """Receive the next message on a specific channel."""
        try:
            return await asyncio.wait_for(
                self._channel_queues[channel].get(), timeout=timeout
            )
        except asyncio.TimeoutError:
            raise TimeoutError(
                f"timeout waiting for response on channel {channel}"
            )

    async def auth(self, token: str) -> None:
        """Authenticate with the server."""
        await self.control.auth(token)
        self._authenticated = True

    async def eval(self, command: str, timeout: float = 30.0) -> EvalResult:
        """Evaluate a command (convenience wrapper)."""
        return await self.command.eval(command, timeout=timeout)

    async def ping(self) -> None:
        """Ping the server."""
        await self.control.ping()

    async def close(self) -> None:
        """Close the connection."""
        # Send disconnect while reader is still active (it routes the response)
        try:
            await asyncio.wait_for(self.control.disconnect(), timeout=2.0)
        except Exception:
            pass
        # Now cancel the reader
        if self._reader_task:
            self._reader_task.cancel()
            try:
                await self._reader_task
            except asyncio.CancelledError:
                pass
        await self._transport.close()

    @property
    def is_connected(self) -> bool:
        return self._transport.is_open

    @property
    def is_authenticated(self) -> bool:
        return self._authenticated

    async def __aenter__(self) -> "BashClient":
        return self

    async def __aexit__(self, *exc: Any) -> None:
        await self.close()
