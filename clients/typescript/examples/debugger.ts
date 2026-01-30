import { BashClient, BreakHitEvent } from "../src";

async function main() {
  const socketPath = process.argv[2] ?? "/tmp/bash-server-1000/sock";
  const token = process.argv[3] ?? "a".repeat(64);

  const client = await BashClient.connect(socketPath);
  try {
    await client.auth(token);
    await client.debug.enable();

    const bpId = await client.debug.addBreakpoint({ kind: "command", pattern: "echo" });
    console.log(`Breakpoint set: id=${bpId}`);

    client.debug.on("break_hit", async (e: BreakHitEvent) => {
      console.log(`[BREAK] line=${e.line} cmd=${e.command}`);
      const ast = await client.debug.inspectAst();
      console.log(`  AST:`, JSON.stringify(ast));
      await client.debug.continue_();
    });

    const result = await client.eval("echo breakpoint_test");
    console.log(`Result: ${result.stdout}`);

    await client.debug.removeBreakpoint(bpId);
    await client.debug.disable();
  } finally {
    await client.close();
  }
}

main().catch(console.error);
