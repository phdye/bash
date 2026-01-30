#!/usr/bin/env python3
"""Example: set breakpoints, step through commands, inspect AST."""

import asyncio
import sys
from bashclient import BashClient, BreakHitEvent


async def main():
    socket_path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/bash-server/sock"
    token = sys.argv[2] if len(sys.argv) > 2 else "a" * 64

    async with await BashClient.connect(socket_path) as client:
        await client.auth(token)

        # Enable debugger
        await client.debug.enable()

        # Set a breakpoint on 'echo' commands
        bp_id = await client.debug.add_breakpoint(kind="command", pattern="echo")
        print(f"Breakpoint set: id={bp_id}")

        # List breakpoints
        breakpoints = await client.debug.list_breakpoints()
        for bp in breakpoints:
            print(f"  BP#{bp.id}: kind={bp.kind} enabled={bp.enabled}")

        # Register break handler
        async def on_break(event: BreakHitEvent):
            print(f"[BREAK] line={event.line} cmd={event.command!r}")
            ast = await client.debug.inspect_ast()
            print(f"  AST: {ast}")
            await client.debug.continue_()

        client.debug.on("break_hit", on_break)

        # Run a command that will hit the breakpoint
        result = await client.eval("echo breakpoint_test")
        print(f"Result: {result.stdout}")

        # Cleanup
        await client.debug.remove_breakpoint(bp_id)
        await client.debug.disable()


if __name__ == "__main__":
    asyncio.run(main())
