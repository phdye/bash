import { BashClient } from "../src";

async function main() {
  const socketPath = process.argv[2] ?? "/tmp/bash-server-1000/sock";
  const token = process.argv[3] ?? "a".repeat(64);

  const client = await BashClient.connect(socketPath);
  try {
    await client.auth(token);

    let exited = false;

    client.pty.on("output", (data: string) => {
      process.stdout.write(data);
    });
    client.pty.on("exit", (code: number) => {
      console.log(`\n[PTY exited with code ${code}]`);
      exited = true;
    });

    const info = await client.pty.spawn({ rows: 24, cols: 80, strip_ansi: true });
    console.log(`PTY spawned: pid=${info.pid} ${info.rows}x${info.cols}`);

    await client.pty.writeInput("echo 'Hello from PTY!'\n");
    await new Promise((r) => setTimeout(r, 500));
    await client.pty.writeInput("exit\n");
    await new Promise((r) => setTimeout(r, 2000));

    if (!exited) await client.pty.close();
  } finally {
    await client.close();
  }
}

main().catch(console.error);
