# bashclient Java Examples

## Example Programs

This directory contains runnable example programs demonstrating the
bashclient Java library. Each example is a standalone Java file with a
`main` method.

---

## Table of Contents

| Example | File | Demonstrates |
|---------|------|-------------|
| [Basic Evaluation](#evaljava) | `Eval.java` | Connect, auth, eval, try-with-resources |
| [Observer Pattern](#observejava) | `Observe.java` | Pre/post command events, callbacks |
| [Interactive Debugger](#debuggerjava) | `Debugger.java` | Breakpoints, stepping, AST inspection |
| [PTY Session](#ptysessionjava) | `PtySession.java` | PTY spawn, I/O, resize, signal |

---

## Prerequisites

Before running any example:

1. **Build the library**:

```bash
cd /path/to/bash/bash-server/clients/java
mvn package
```

2. **Start bash-server**:

```bash
./bash-server/bash-server --name examples --no-peercred
```

3. **Get the token and socket path**:

```bash
TOKEN=$(cat ~/.config/bash-server/examples/token)
SOCKET=/tmp/bash-server-$(id -u)/sock
echo "Socket: $SOCKET"
echo "Token: $TOKEN"
```

---

## Building and Running

### Using Maven exec:plugin

```bash
# Run any example
mvn exec:java -Dexec.mainClass="org.gnu.bash.client.examples.Eval" \
    -Dexec.args="$SOCKET $TOKEN"
```

### Using javac and java

```bash
# Set classpath
CP=target/bashclient-0.1.0.jar:target/dependency/*

# Compile an example
javac -cp "$CP" -d target/examples examples/Eval.java

# Run it
java -cp "target/examples:$CP" org.gnu.bash.client.examples.Eval \
    "$SOCKET" "$TOKEN"
```

### Using a fat JAR

If you built with the `shade` or `assembly` plugin:

```bash
java -jar target/bashclient-0.1.0-jar-with-dependencies.jar \
    org.gnu.bash.client.examples.Eval "$SOCKET" "$TOKEN"
```

---

## Eval.java

**Basic command evaluation with try-with-resources.**

Demonstrates the core workflow: connect to a bash-server, authenticate,
evaluate commands, and print results.

### What it does

1. Connects to bash-server via Unix socket
2. Authenticates with the provided token
3. Runs several commands: `echo`, `date`, `uname`, a pipeline, and a
   failing command
4. Prints stdout, stderr, and exit code for each
5. Automatically closes the connection via try-with-resources

### Usage

```bash
java -cp "$CP" org.gnu.bash.client.examples.Eval <socket_path> <token>
```

### Expected output

```
=== bashclient Java - Eval Example ===

Connected to /tmp/bash-server-1000/sock
Authenticated successfully.

--- Command: echo 'Hello from bash-server!' ---
stdout: Hello from bash-server!
stderr:
exit code: 0

--- Command: date +%Y-%m-%d ---
stdout: 2026-01-30
stderr:
exit code: 0

--- Command: uname -a ---
stdout: CYGWIN_NT-10.0-26200 hostname 3.6.6-1.x86_64 ...
stderr:
exit code: 0

--- Command: echo hello | tr a-z A-Z ---
stdout: HELLO
stderr:
exit code: 0

--- Command: ls /nonexistent ---
stdout:
stderr: ls: cannot access '/nonexistent': No such file or directory
exit code: 2

Connection closed.
```

---

## Observe.java

**Observer pattern for command events.**

Demonstrates subscribing to pre-command and post-command events to monitor
all commands executed in the session.

### What it does

1. Connects and authenticates
2. Configures observe level to `"full"` for timing information
3. Registers pre-command and post-command callbacks that print event details
4. Runs several commands to generate events
5. Shows how to remove listeners when done

### Usage

```bash
java -cp "$CP" org.gnu.bash.client.examples.Observe <socket_path> <token>
```

### Expected output

```
=== bashclient Java - Observe Example ===

Connected and authenticated.
Observe level set to 'full'.

[PRE]  Command: echo first
[POST] Command: echo first | exit=0 | 2ms
stdout: first

[PRE]  Command: sleep 1
[POST] Command: sleep 1 | exit=0 | 1003ms
stdout:

[PRE]  Command: false
[POST] Command: false | exit=1 | 1ms
stdout:

--- Event Summary ---
Total events captured: 6 (3 pre, 3 post)
Listeners removed.
Connection closed.
```

---

## Debugger.java

**Interactive debugger with breakpoints and AST inspection.**

Demonstrates the debug channel: setting breakpoints, stepping through
commands, and inspecting the AST of the current command.

### What it does

1. Connects and authenticates
2. Sets a function breakpoint on `"greet"`
3. Defines a `greet` function in the session
4. Registers a break-hit callback that prints location and AST
5. Calls the function, triggering the breakpoint
6. Steps through three commands, inspecting each
7. Continues execution to complete

### Usage

```bash
java -cp "$CP" org.gnu.bash.client.examples.Debugger <socket_path> <token>
```

### Expected output

```
=== bashclient Java - Debugger Example ===

Connected and authenticated.
Breakpoint #1 set on function 'greet'.
Function 'greet' defined.

Calling greet...

[BREAK] Hit breakpoint #1
  Command: echo "Hello, World!"
  File: (interactive)
  Line: 1

[AST]
{
  "type" : "simple",
  "words" : [ "echo", "Hello, World!" ]
}

Stepping...

[BREAK] Next command
  Command: echo "Goodbye!"

Continuing execution...

greet() output:
stdout: Hello, World!
Goodbye!

Breakpoint removed.
Connection closed.
```

---

## PtySession.java

**Interactive PTY session with terminal emulation.**

Demonstrates spawning a PTY, sending input, receiving output, resizing
the terminal, and handling signals.

### What it does

1. Connects and authenticates
2. Spawns a PTY with 80x24 dimensions
3. Registers an output callback to display terminal output
4. Sends several commands to the PTY
5. Demonstrates terminal resize (80x24 to 120x40)
6. Sends Ctrl+C to interrupt a long-running command
7. Closes the PTY session

### Usage

```bash
java -cp "$CP" org.gnu.bash.client.examples.PtySession <socket_path> <token>
```

### Expected output

```
=== bashclient Java - PTY Session Example ===

Connected and authenticated.
PTY spawned: PID=12345, 80x24

[PTY] $ 
Sending: echo 'PTY is working'
[PTY] echo 'PTY is working'
[PTY] PTY is working
[PTY] $ 

Sending: pwd
[PTY] pwd
[PTY] /home/user
[PTY] $ 

Resizing to 120x40...
Resize complete.

Sending: sleep 30
[PTY] sleep 30
Sending Ctrl+C...
[PTY] ^C
[PTY] $ 

PTY session closed.
Connection closed.
```

---

## Writing Your Own Examples

To create a new example:

1. Create a new `.java` file in this directory
2. Use the package `org.gnu.bash.client.examples`
3. Include a `main(String[] args)` method
4. Accept `socketPath` and `token` as command-line arguments
5. Use try-with-resources for the `BashClient`

Template:

```java
package org.gnu.bash.client.examples;

import org.gnu.bash.client.BashClient;
import org.gnu.bash.client.BashClientException;
import org.gnu.bash.client.types.EvalResult;

public class MyExample {
    public static void main(String[] args) throws Exception {
        if (args.length < 2) {
            System.err.println("Usage: MyExample <socket_path> <token>");
            System.exit(1);
        }

        String socketPath = args[0];
        String token = args[1];

        try (BashClient client = BashClient.connect(socketPath)) {
            client.auth(token);

            // Your example code here
            EvalResult result = client.eval("echo 'My example works!'");
            System.out.println(result.getStdout());

        } catch (BashClientException e) {
            System.err.println("Error: " + e.getMessage());
            e.printStackTrace();
        }
    }
}
```

---

## See Also

- [Guide](../GUIDE.md) -- comprehensive usage documentation with recipes
- [API Reference](../API.md) -- complete method documentation
- [Troubleshooting](../TROUBLESHOOTING.md) -- common issues and solutions
