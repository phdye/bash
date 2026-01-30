import { execSync } from "child_process";
import { BashClient } from "../src/client";

const hasBashServer = (() => {
  try {
    execSync("which bash-server", { stdio: "ignore" });
    return true;
  } catch {
    return false;
  }
})();

const describeIf = hasBashServer ? describe : describe.skip;

describeIf("Integration (requires bash-server)", () => {
  it("stdio connect + auth + eval", async () => {
    const token = "t".repeat(64);
    const client = await BashClient.connectStdio(
      "bash-server", "--stdio", "--token", token
    );
    try {
      await client.auth(token);
      const result = await client.eval("echo integration_test");
      expect(result.stdout).toContain("integration_test");
      expect(result.exit_code).toBe(0);
    } finally {
      await client.close();
    }
  });
});
