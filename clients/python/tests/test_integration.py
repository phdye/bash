"""Integration tests requiring a real bash-server binary.

These tests start bash-server --stdio and run real commands.
Skip if bash-server is not available.
"""

import asyncio
import os
import shutil
import pytest

from bashclient.client import BashClient


BASH_SERVER = shutil.which("bash-server")
skip_no_server = pytest.mark.skipif(
    BASH_SERVER is None,
    reason="bash-server not found in PATH",
)

pytestmark = [pytest.mark.asyncio, skip_no_server]


class TestIntegration:
    async def test_stdio_connect_auth_eval(self):
        """Full lifecycle: connect, auth, eval, close."""
        proc = await asyncio.create_subprocess_exec(
            BASH_SERVER, "--stdio", "--token", "test" * 16,
            stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        try:
            from bashclient.transport import StdioTransport
            transport = StdioTransport()
            transport._process = proc
            transport._reader = proc.stdout
            transport._writer_raw = proc.stdin
            transport._open = True

            client = await BashClient.from_transport(transport)
            await client.auth("test" * 16)
            result = await client.eval("echo integration_test")
            assert "integration_test" in result.stdout
            assert result.exit_code == 0
            await client.close()
        finally:
            try:
                proc.terminate()
                await asyncio.wait_for(proc.wait(), timeout=5.0)
            except Exception:
                proc.kill()
