#!/usr/bin/env python3
"""Example: subscribe to command observation events."""

import asyncio
import sys
from bashclient import BashClient, PreCommandEvent, PostCommandEvent


async def main():
    socket_path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/bash-server/sock"
    token = sys.argv[2] if len(sys.argv) > 2 else "a" * 64

    async with await BashClient.connect(socket_path) as client:
        await client.auth(token)

        def on_pre(event: PreCommandEvent):
            print(f"[PRE]  seq={event.seq} cmd={event.command!r} cwd={event.cwd}")

        def on_post(event: PostCommandEvent):
            print(f"[POST] seq={event.seq} cmd={event.command!r} "
                  f"exit={event.exit_status} dur={event.duration_ms}ms")

        client.observe.on("pre_command", on_pre)
        client.observe.on("post_command", on_post)

        await client.observe.subscribe(level=1)

        # Run some commands to generate events
        await client.eval("echo one")
        await client.eval("echo two")
        await client.eval("false")

        await client.observe.unsubscribe()


if __name__ == "__main__":
    asyncio.run(main())
