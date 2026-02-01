package org.gnu.bash.client.examples;

import org.gnu.bash.client.BashClient;

public class Debugger {
    public static void main(String[] args) throws Exception {
        String path = args.length > 0 ? args[0] : "/tmp/bash-server/sock";
        String token = args.length > 1 ? args[1] : "a".repeat(64);

        try (BashClient client = BashClient.connect(path)) {
            client.auth(token);
            client.debug.enable();
            int bpId = client.debug.addBreakpoint("command", "echo", -1, null);
            System.out.println("Breakpoint set: id=" + bpId);
            client.debug.on("break_hit", ev -> System.out.println("[BREAK] " + ev));
            client.eval("echo breakpoint_test");
            client.debug.removeBreakpoint(bpId);
            client.debug.disable();
        }
    }
}
