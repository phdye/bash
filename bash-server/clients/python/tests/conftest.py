"""Test fixtures: mock server, socketpair-based transports."""

import asyncio
import json
import base64
from typing import Any, AsyncGenerator, Dict, List, Tuple

import pytest

from bashclient.transport import Transport
from bashclient.client import BashClient
from bashclient.types import CHAN_CONTROL, CHAN_COMMAND, CHAN_STATE


class MockTransport(Transport):
    """In-memory transport for unit tests using asyncio queues."""

    def __init__(self) -> None:
        self._inbox: asyncio.Queue[bytes] = asyncio.Queue()
        self._outbox: asyncio.Queue[bytes] = asyncio.Queue()
        self._open = True

    async def read_line(self) -> bytes:
        if not self._open:
            from bashclient.errors import TransportError
            raise TransportError("closed")
        return await self._inbox.get()

    async def write(self, data: bytes) -> None:
        if not self._open:
            from bashclient.errors import TransportError
            raise TransportError("closed")
        await self._outbox.put(data)

    async def close(self) -> None:
        self._open = False

    @property
    def is_open(self) -> bool:
        return self._open

    async def inject(self, msg: Dict[str, Any]) -> None:
        """Inject a message as if from the server."""
        line = json.dumps(msg, separators=(",", ":")).encode("utf-8") + b"\n"
        await self._inbox.put(line)

    async def read_sent(self) -> Dict[str, Any]:
        """Read a message sent by the client."""
        data = await asyncio.wait_for(self._outbox.get(), timeout=2.0)
        return json.loads(data.strip())


class MockServer:
    """Simple mock server that auto-responds to common requests."""

    def __init__(self, transport: MockTransport, token: str = "a" * 64) -> None:
        self._transport = transport
        self._token = token
        self._running = False
        self._task = None

    async def start(self) -> None:
        self._running = True
        self._task = asyncio.ensure_future(self._loop())

    async def stop(self) -> None:
        self._running = False
        if self._task:
            self._task.cancel()
            try:
                await self._task
            except asyncio.CancelledError:
                pass

    async def _loop(self) -> None:
        try:
            while self._running:
                data = await asyncio.wait_for(self._transport._outbox.get(), timeout=5.0)
                msg = json.loads(data.strip())
                ch = msg.get("ch", 0)
                t = msg.get("type", "")

                if ch == CHAN_CONTROL and t == "auth":
                    if msg.get("token") == self._token:
                        await self._transport.inject(
                            {"ch": 0, "type": "auth_ok",
                             "capabilities": ["state", "command", "observe", "debug"]}
                        )
                    else:
                        await self._transport.inject(
                            {"ch": 0, "type": "error", "message": "invalid token"}
                        )
                elif ch == CHAN_CONTROL and t == "ping":
                    await self._transport.inject({"ch": 0, "type": "pong"})
                elif ch == CHAN_CONTROL and t == "disconnect":
                    await self._transport.inject({"ch": 0, "type": "disconnect_ok"})
                    self._running = False
                elif ch == CHAN_COMMAND and t == "eval":
                    cmd = msg.get("command", "")
                    stdout = f"mock: {cmd}\n"
                    req_id = msg.get("id")
                    stdout_msg = {
                        "ch": 1, "type": "stdout",
                        "data": base64.b64encode(stdout.encode()).decode(),
                        "encoding": "base64",
                    }
                    stderr_msg = {
                        "ch": 1, "type": "stderr",
                        "data": base64.b64encode(b"").decode(),
                        "encoding": "base64",
                    }
                    complete_msg = {"ch": 1, "type": "complete", "exit_code": 0}
                    if req_id:
                        stdout_msg["id"] = req_id
                        stderr_msg["id"] = req_id
                        complete_msg["id"] = req_id
                    await self._transport.inject(stdout_msg)
                    await self._transport.inject(stderr_msg)
                    await self._transport.inject(complete_msg)
                elif ch == CHAN_STATE and t == "get":
                    target = msg.get("target", "")
                    name = msg.get("name", "")
                    if target == "var":
                        await self._transport.inject({
                            "ch": 2, "type": "value", "target": "var",
                            "name": name, "value": f"mock_{name}",
                            "attributes": [],
                        })
                    elif target == "function":
                        await self._transport.inject({
                            "ch": 2, "type": "value", "target": "function",
                            "name": name, "value": f"{name} () {{ echo mock; }}",
                        })
                    elif target == "alias":
                        await self._transport.inject({
                            "ch": 2, "type": "value", "target": "alias",
                            "name": name, "value": f"mock_{name}",
                        })
                    else:
                        await self._transport.inject({
                            "ch": 2, "type": "error", "message": "unknown target",
                        })
                elif ch == CHAN_STATE and t in ("set", "unset"):
                    target = msg.get("target", "")
                    name = msg.get("name", "")
                    await self._transport.inject({
                        "ch": 2, "type": f"{t}_ok", "target": target, "name": name,
                    })
                elif ch == CHAN_STATE and t == "inspect":
                    query = msg.get("query", "vars")
                    await self._transport.inject({
                        "ch": 2, "type": "inspect_result", "query": query,
                        "data": [{"name": "MOCK", "value": "1"}],
                    })

        except (asyncio.CancelledError, asyncio.TimeoutError):
            pass


@pytest.fixture
async def mock_transport() -> AsyncGenerator[MockTransport, None]:
    """Create MockTransport inside async context so Queues bind to the test event loop."""
    t = MockTransport()
    yield t
    await t.close()


@pytest.fixture
async def mock_client_and_server(
    mock_transport: MockTransport,
) -> AsyncGenerator[Tuple[BashClient, MockServer], None]:
    """Create a BashClient + MockServer pair connected via MockTransport."""
    server = MockServer(mock_transport)
    await server.start()
    client = await BashClient.from_transport(mock_transport)
    yield client, server
    await client.close()
    await server.stop()
