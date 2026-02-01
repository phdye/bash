package org.gnu.bash.client.examples;

import org.gnu.bash.client.BashClient;
import org.gnu.bash.client.types.PtyInfo;

public class PtySession {
    public static void main(String[] args) throws Exception {
        String path = args.length > 0 ? args[0] : "/tmp/bash-server/sock";
        String token = args.length > 1 ? args[1] : "a".repeat(64);

        try (BashClient client = BashClient.connect(path)) {
            client.auth(token);
            client.pty.on("output", data -> System.out.print(data));
            client.pty.on("exit", code -> System.out.println("\n[PTY exited: " + code + "]"));
            PtyInfo info = client.pty.spawn(24, 80, null, true);
            System.out.println("PTY: pid=" + info.getPid());
            client.pty.writeInput("echo 'Hello PTY!'\n");
            Thread.sleep(500);
            client.pty.writeInput("exit\n");
            Thread.sleep(2000);
            client.pty.close();
        }
    }
}
