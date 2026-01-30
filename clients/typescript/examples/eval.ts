import { BashClient } from "../src";

async function main() {
  const socketPath = process.argv[2] ?? "/tmp/bash-server-1000/sock";
  const token = process.argv[3] ?? "a".repeat(64);

  const client = await BashClient.connect(socketPath);
  try {
    await client.auth(token);
    const result = await client.eval("echo 'Hello from bash-server!'");
    console.log(`stdout: ${result.stdout}`);
    console.log(`stderr: ${result.stderr}`);
    console.log(`exit_code: ${result.exit_code}`);
  } finally {
    await client.close();
  }
}

main().catch(console.error);
