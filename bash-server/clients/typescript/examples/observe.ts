import { BashClient, PreCommandEvent, PostCommandEvent } from "../src";

async function main() {
  const socketPath = process.argv[2] ?? "/tmp/bash-server-1000/sock";
  const token = process.argv[3] ?? "a".repeat(64);

  const client = await BashClient.connect(socketPath);
  try {
    await client.auth(token);

    client.observe.on("pre_command", (e: PreCommandEvent) => {
      console.log(`[PRE]  seq=${e.seq} cmd=${e.command} cwd=${e.cwd}`);
    });
    client.observe.on("post_command", (e: PostCommandEvent) => {
      console.log(`[POST] seq=${e.seq} cmd=${e.command} exit=${e.exit_status} dur=${e.duration_ms}ms`);
    });

    await client.observe.subscribe(1);
    await client.eval("echo one");
    await client.eval("echo two");
    await client.observe.unsubscribe();
  } finally {
    await client.close();
  }
}

main().catch(console.error);
