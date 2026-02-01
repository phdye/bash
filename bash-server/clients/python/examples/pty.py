#!/usr/bin/env python3
"""Example: spawn a PTY session and relay I/O."""

import asyncio
import sys
from bashclient import BashClient


async def main():
    socket_path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/bash-server/sock"
    token = sys.argv[2] if len(sys.argv) > 2 else "a" * 64

    async with await BashClient.connect(socket_path) as client:
        await client.auth(token)

        exit_event = asyncio.Event()

        def on_output(data: str):
            sys.stdout.write(data)
            sys.stdout.flush()

        def on_exit(code: int):
            print(f"\n[PTY exited with code {code}]")
            exit_event.set()

        client.pty.on("output", on_output)
        client.pty.on("exit", on_exit)

        # Spawn PTY
        info = await client.pty.spawn(rows=24, cols=80, strip_ansi=True)
        print(f"PTY spawned: pid={info.pid} {info.rows}x{info.cols}")

        # Send a command
        await client.pty.write_input("echo 'Hello from PTY!'\n")
        await asyncio.sleep(0.5)

        # Send exit
        await client.pty.write_input("exit\n")

        # Wait for exit
        try:
            await asyncio.wait_for(exit_event.wait(), timeout=5.0)
        except asyncio.TimeoutError:
            print("[Timeout waiting for PTY exit]")
            await client.pty.close()


if __name__ == "__main__":
    asyncio.run(main())
