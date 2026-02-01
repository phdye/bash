package org.gnu.bash.client.examples;

import org.gnu.bash.client.BashClient;

public class Observe {
    public static void main(String[] args) throws Exception {
        String path = args.length > 0 ? args[0] : "/tmp/bash-server/sock";
        String token = args.length > 1 ? args[1] : "a".repeat(64);

        try (BashClient client = BashClient.connect(path)) {
            client.auth(token);
            client.observe.on("pre_command", msg -> System.out.println("[PRE] " + msg));
            client.observe.on("post_command", msg -> System.out.println("[POST] " + msg));
            client.observe.subscribe(1);
            client.eval("echo one");
            client.eval("echo two");
            client.observe.unsubscribe();
        }
    }
}
