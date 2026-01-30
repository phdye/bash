package org.gnu.bash.client.examples;

import org.gnu.bash.client.BashClient;
import org.gnu.bash.client.types.EvalResult;

public class Eval {
    public static void main(String[] args) throws Exception {
        String path = args.length > 0 ? args[0] : "/tmp/bash-server/sock";
        String token = args.length > 1 ? args[1] : "a".repeat(64);

        try (BashClient client = BashClient.connect(path)) {
            client.auth(token);
            EvalResult result = client.eval("echo 'Hello from bash-server!'");
            System.out.println("stdout: " + result.getStdout());
            System.out.println("stderr: " + result.getStderr());
            System.out.println("exit_code: " + result.getExitCode());
        }
    }
}
