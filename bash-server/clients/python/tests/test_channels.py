"""Tests for channel request/response via mock transport."""

import asyncio
import pytest

from bashclient.client import BashClient
from bashclient.errors import AuthError, ServerError
from bashclient.types import EvalResult


pytestmark = pytest.mark.asyncio


class TestAuth:
    async def test_auth_success(self, mock_client_and_server):
        client, server = mock_client_and_server
        await client.auth("a" * 64)
        assert client.is_authenticated

    async def test_auth_failure(self, mock_client_and_server):
        client, server = mock_client_and_server
        with pytest.raises(AuthError):
            await client.auth("wrong_token")


class TestPing:
    async def test_ping(self, mock_client_and_server):
        client, server = mock_client_and_server
        await client.auth("a" * 64)
        await client.ping()


class TestEval:
    async def test_eval_returns_result(self, mock_client_and_server):
        client, server = mock_client_and_server
        await client.auth("a" * 64)
        result = await client.eval("echo hello")
        assert isinstance(result, EvalResult)
        assert "mock: echo hello" in result.stdout
        assert result.exit_code == 0


class TestState:
    async def test_get_var(self, mock_client_and_server):
        client, server = mock_client_and_server
        await client.auth("a" * 64)
        info = await client.state.get_var("PATH")
        assert info.name == "PATH"
        assert info.value == "mock_PATH"

    async def test_set_var(self, mock_client_and_server):
        client, server = mock_client_and_server
        await client.auth("a" * 64)
        await client.state.set_var("FOO", "bar")

    async def test_unset_var(self, mock_client_and_server):
        client, server = mock_client_and_server
        await client.auth("a" * 64)
        await client.state.unset_var("FOO")

    async def test_get_func(self, mock_client_and_server):
        client, server = mock_client_and_server
        await client.auth("a" * 64)
        info = await client.state.get_func("myfunc")
        assert info.name == "myfunc"
        assert "mock" in info.definition

    async def test_get_alias(self, mock_client_and_server):
        client, server = mock_client_and_server
        await client.auth("a" * 64)
        info = await client.state.get_alias("ll")
        assert info.name == "ll"

    async def test_inspect(self, mock_client_and_server):
        client, server = mock_client_and_server
        await client.auth("a" * 64)
        items = await client.state.inspect("vars")
        assert len(items) > 0
