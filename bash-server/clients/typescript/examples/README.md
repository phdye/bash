# Examples

## bashclient TypeScript/Node.js Examples

Runnable example scripts demonstrating the `bashclient` package. Each example
is a self-contained TypeScript file that can be compiled and run with Node.js.

---

## Prerequisites

1. **bash-server** running (unless using stdio transport):

   ```bash
   bash-server &
   ```

2. **bashclient** installed:

   ```bash
   npm install bashclient
   ```

3. **TypeScript** (for running .ts files directly with ts-node, or compile first):

   ```bash
   npm install -g ts-node typescript
   ```

---

## Running Examples

### With ts-node (Direct Execution)

```bash
ts-node examples/eval.ts
ts-node examples/observe.ts
ts-node examples/debugger.ts
ts-node examples/pty.ts
```

### With Compilation

```bash
npx tsc examples/eval.ts --outDir dist/examples --esModuleInterop
node dist/examples/eval.js
```

### With stdio Transport (No Server Needed)

Examples that use `connectStdio()` spawn their own server:

```bash
ts-node examples/eval.ts --stdio
```

---

## Example Index

### eval.ts -- Basic Command Evaluation

Demonstrates the core workflow: connect to bash-server, execute commands, and
process results.

**Topics covered**:

- `BashClient.connect()` -- connecting via Unix socket.
- `client.command.eval()` -- executing commands.
- `EvalResult` -- accessing stdout, stderr, exit_code.
- Error handling with try/catch and typed errors.
- Connection cleanup with `client.close()` in a finally block.
- Multi-line commands and pipelines.
- Commands with non-zero exit codes.
- State persistence across evaluations (variables, functions).

**Sample output**:

```
=== Basic eval ===
hello world

=== Multi-line command ===
Number: 1
Number: 2
Number: 3

=== Pipeline ===
      42

=== Exit codes ===
'false' exited with code: 1

=== Persistent state ===
greeting=Hello from bash-server
result: 7
```

---

### observe.ts -- Command Observation and Profiling

Demonstrates subscribing to command execution events for monitoring
and profiling.

**Topics covered**:

- `client.observe.setLevel()` -- enabling observation.
- `client.observe.on("pre_command", ...)` -- pre-command events.
- `client.observe.on("post_command", ...)` -- post-command events.
- `PreCommandEvent` fields: seq, timestamp, command, cwd, line_number.
- `PostCommandEvent` fields: exit_status, duration_ms, signal_number.
- Building a command profiler.
- Disabling observation with `OBSERVE_LEVEL_OFF`.
- Removing event handlers with `off()`.

**Sample output**:

```
=== Observation started ===
[PRE  #1] echo hello                    cwd=/home/user  line=1
[POST #1] echo hello                    exit=0  duration=1ms
[PRE  #2] sleep 0.1                     cwd=/home/user  line=1
[POST #2] sleep 0.1                     exit=0  duration=102ms
[PRE  #3] ls /tmp | wc -l              cwd=/home/user  line=1
[POST #3] ls /tmp | wc -l              exit=0  duration=5ms

=== Profile ===
  1ms  echo hello
102ms  sleep 0.1
  5ms  ls /tmp | wc -l
```

---

### debugger.ts -- Breakpoints and Stepping

Demonstrates the debug channel for setting breakpoints, receiving break
hit events, stepping through commands, and inspecting the AST.

**Topics covered**:

- `client.debug.addBreakpoint()` -- command, line, and function breakpoints.
- `client.debug.on("break_hit", ...)` -- handling breakpoint hits.
- `client.debug.listBreakpoints()` -- listing active breakpoints.
- `client.debug.enableBreakpoint()` -- toggling breakpoints.
- `client.debug.continue()` -- resuming execution.
- `client.debug.step()` -- single-stepping.
- `client.debug.next()` -- stepping over functions.
- `client.debug.finish()` -- stepping out of functions.
- `client.debug.skip()` -- skipping commands.
- `client.debug.inspectAst()` -- AST inspection at a breakpoint.
- `client.debug.status()` -- querying debug state.
- `client.debug.removeAllBreakpoints()` -- cleanup.
- Conditional breakpoints with bash expressions.

**Sample output**:

```
=== Setting breakpoints ===
Breakpoint #1: command pattern "echo *"
Breakpoint #2: function pattern "process_*"

=== Running script ===
[BREAK] line 3: echo "Processing: file1.txt"
  AST type: simple
  Continuing...
[BREAK] line 4: echo "Processing: file2.txt"
  Stepping...
[BREAK] line 5: echo "Done"
  Continuing...

=== Debug status ===
Active: false
Breakpoints: 2
Total hits: 3

=== Cleanup ===
All breakpoints removed.
```

---

### pty.ts -- PTY Terminal Interaction

Demonstrates spawning a pseudo-terminal, sending input, receiving output,
resizing, and handling terminal close events.

**Topics covered**:

- `client.pty.spawn()` -- creating a PTY with rows, cols, strip_ansi.
- `client.pty.on("output", ...)` -- receiving terminal output.
- `client.pty.on("close", ...)` -- handling PTY close.
- `client.pty.writeInput()` -- sending commands and special keys.
- `client.pty.resize()` -- changing terminal dimensions.
- `client.pty.signal()` -- sending signals (INT, TERM).
- `client.pty.close()` -- closing the PTY session.
- ANSI stripping for clean text output.
- Running interactive programs (top, vi, etc.).

**Sample output**:

```
=== PTY spawned ===
PID: 12345
Size: 80x24
ANSI stripping: true

=== Running commands ===
$ echo hello
hello
$ pwd
/home/user
$ uname -a
Linux hostname 5.15.0 ...

=== Resize to 120x40 ===
Terminal resized.

=== Sending Ctrl-C ===
^C

=== PTY closed ===
Session ended.
```

---

## Creating Your Own Examples

Use this template as a starting point:

```typescript
import { BashClient, EvalResult } from "bashclient";

async function main() {
  const client = await BashClient.connect();

  try {
    // Your code here
    const result = await client.command.eval("echo 'Hello!'");
    console.log(result.stdout);
  } finally {
    await client.close();
  }
}

main().catch((err) => {
  console.error("Error:", err.message);
  process.exit(1);
});
```

---

## See Also

- [GUIDE.md](../GUIDE.md) -- Comprehensive usage guide with more examples.
- [API.md](../API.md) -- Complete API reference.
- [TROUBLESHOOTING.md](../TROUBLESHOOTING.md) -- Common issues and solutions.
