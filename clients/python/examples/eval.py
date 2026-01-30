#!/usr/bin/env python3
"""Example: connect to bash-server, authenticate, and evaluate a command."""

import asyncio
import sys
from bashclient import BashClient


async def main():
    socket_path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/bash-server/sock"
    token = sys.argv[2] if len(sys.argv) > 2 else "a" * 64

    async with await BashClient.connect(socket_path) as client:
        await client.auth(token)
        result = await client.eval("echo 'Hello from bash-server!'")
        print(f"stdout: {result.stdout}")
        print(f"stderr: {result.stderr}")
        print(f"exit_code: {result.exit_code}")


if __name__ == "__main__":
    asyncio.run(main())
