# libbashclient Examples

Working examples demonstrating how to use the libbashclient C library.
Each example is a self-contained program that can be compiled and run
independently.

---

## Prerequisites

- libbashclient installed (see [../INSTALL.md](../INSTALL.md))
- A running bash-server instance
- GCC or Clang with C99 support

Start a bash-server for testing:

```bash
bash-server --name examples &
```

---

## Examples

### eval.c -- Basic Command Evaluation

Demonstrates the fundamental workflow: connect to bash-server, evaluate
shell commands, and process the results.

**Features covered:**
- `bc_connect()` with auto-discovery
- `bc_eval()` for synchronous evaluation
- `bc_eval_result_t` fields (stdout, stderr, exit code)
- `bc_eval_result_free()` for cleanup
- `bc_close()` for disconnection
- Error handling with `bc_last_error_msg()`

**Build:**

```bash
gcc -std=c99 -o eval eval.c -lbashclient
```

**Run:**

```bash
./eval
```

**Expected output:**

```
Connected to bash-server.
--- Simple command ---
stdout: Hello from bash-server!
exit code: 0

--- Multi-line script ---
stdout: 1
2
3
exit code: 0

--- Command with stderr ---
stderr: ls: cannot access '/nonexistent': No such file or directory
exit code: 2

--- Variable persistence ---
stdout: 42
exit code: 0
```

**Key takeaway:** The server maintains state between evaluations -- variables
set in one `bc_eval()` call are available in the next.

---

### observe.c -- Command Execution Observer

A monitoring tool that subscribes to command execution events and logs
them. Demonstrates the OBSERVE channel and the polling/callback pattern.

**Features covered:**
- `bc_observe_set_level()` to enable event streaming
- `bc_on_pre_command()` callback registration
- `bc_on_post_command()` callback registration
- `bc_poll()` event loop
- `bc_pre_command_event_t` fields (seq, command, cwd, line, subshell, async)
- `bc_post_command_event_t` fields (seq, command, exit_status, duration_ms)
- Signal handling for graceful shutdown
- Callback userdata pattern

**Build:**

```bash
gcc -std=c99 -o observe observe.c -lbashclient
```

**Run:**

```bash
# Terminal 1: start the observer
./observe

# Terminal 2: connect to the same server and run commands
bashclient eval "echo hello"
bashclient eval "ls /tmp"
bashclient eval "sleep 1 && echo done"
```

**Expected output (Terminal 1):**

```
Observer started. Press Ctrl-C to stop.
[PRE]  seq=1 cmd='echo hello' cwd=/home/user line=1
[POST] seq=1 cmd='echo hello' exit=0 duration=1ms
[PRE]  seq=2 cmd='ls /tmp' cwd=/home/user line=1
[POST] seq=2 cmd='ls /tmp' exit=0 duration=3ms
[PRE]  seq=3 cmd='sleep 1' cwd=/home/user line=1
[POST] seq=3 cmd='sleep 1' exit=0 duration=1001ms
[PRE]  seq=4 cmd='echo done' cwd=/home/user line=1
[POST] seq=4 cmd='echo done' exit=0 duration=1ms
^C
Observer stopped. 4 commands observed.
```

**Key takeaway:** Observer events arrive asynchronously and are dispatched
by `bc_poll()`. The observer sees commands from all clients connected to
the same server session.

---

### debugger.c -- Interactive Shell Script Debugger

A command-line debugger for shell scripts that supports breakpoints,
stepping, variable inspection, and AST viewing.

**Features covered:**
- `bc_debug_enable()` / `bc_debug_disable()`
- `bc_debug_break_line()`, `bc_debug_break_function()`, `bc_debug_break_command()`
- `bc_debug_set_condition()` for conditional breakpoints
- `bc_debug_step()`, `bc_debug_step_into()`, `bc_debug_step_out()`
- `bc_debug_continue()`, `bc_debug_skip()`
- `bc_debug_list_breakpoints()` and `bc_breakpoints_free()`
- `bc_debug_inspect_ast()` for AST inspection
- `bc_debug_status()` and `bc_debug_status_free()`
- `bc_on_break_hit()` callback
- `bc_var_get()` for variable inspection during debugging
- `bc_eval_stream()` for non-blocking script execution

**Build:**

```bash
gcc -std=c99 -o debugger debugger.c -lbashclient
```

**Run:**

Create a test script (`test.sh`):

```bash
#!/bin/bash
x=1
for i in 1 2 3; do
    x=$((x + i))
    echo "x=$x"
done
echo "Final: $x"
```

Then debug it:

```bash
./debugger test.sh
```

**Example session:**

```
==> Stopped at line 1: x=1 (depth 0)
(debug) s
==> Stopped at line 2: for i in 1 2 3; do (depth 0)
(debug) b 4
Breakpoint 1 at line 4
(debug) c
==> Stopped at line 4: echo "x=$x" (depth 0)
(debug) p x
x = 2
(debug) p i
i = 1
(debug) ast
{"type":"simple","words":[{"word":"echo"},{"word":"x=$x"}],"redirects":[]}
(debug) c
x=2
==> Stopped at line 4: echo "x=$x" (depth 0)
(debug) p x
x = 5
(debug) c
x=5
==> Stopped at line 4: echo "x=$x" (depth 0)
(debug) p x
x = 8
(debug) c
x=8
Final: 8
Script completed.
(debug) q
```

**Key takeaway:** The debugger demonstrates how to combine the DEBUG,
STATE, and COMMAND channels for a complete interactive debugging
experience.

---

### pty.c -- PTY Terminal Emulator

A minimal terminal emulator that connects to a bash-server PTY session.
Puts the local terminal in raw mode and relays I/O between the local
terminal and the remote PTY.

**Features covered:**
- `bc_pty_spawn()` with terminal dimensions
- `bc_pty_read()` with timeouts
- `bc_pty_write()` for input relay
- `bc_pty_resize()` on SIGWINCH
- `bc_pty_set_strip_ansi()` (optional)
- `bc_pty_close()` for cleanup
- `bc_pty_info_t` fields (rows, cols, pid)
- `bc_get_fd()` for poll/select integration
- POSIX terminal handling (termios raw mode)

**Build:**

```bash
gcc -std=c99 -o pty pty.c -lbashclient
```

**Run:**

```bash
./pty
```

**Expected behavior:**

You get a fully interactive bash shell running inside the bash-server
PTY. The terminal supports:
- Full line editing (readline, arrow keys)
- Color output
- Terminal resizing (drag window edges)
- Job control (Ctrl-Z, fg, bg)
- Signal forwarding

Press **Ctrl-]** (right bracket) to disconnect.

```
Connected to PTY (80x24, pid=12345). Press Ctrl-] to exit.
user@host:~$ ls --color
Desktop  Documents  Downloads
user@host:~$ echo "This runs inside bash-server"
This runs inside bash-server
user@host:~$ ^]
Disconnected.
```

**Key takeaway:** The PTY channel provides a full terminal experience,
suitable for building terminal emulators, SSH-like tools, or IDE
integrated terminals.

---

## Building All Examples

```bash
# From the examples/ directory
make

# Or from the library root
make examples
```

This builds all four example programs.

## Cleaning Up

```bash
make clean
```

---

## Writing Your Own Programs

Use any example as a starting point. The general pattern is:

```c
#include <bashclient.h>
#include <stdio.h>

int main(void)
{
    /* 1. Connect */
    bc_client_t *c = bc_connect(NULL, NULL);
    if (!c) {
        fprintf(stderr, "%s\n", bc_last_error_msg(NULL));
        return 1;
    }

    /* 2. Do work */
    /* ... your code here ... */

    /* 3. Clean up */
    bc_close(c);
    return 0;
}
```

Compile with:

```bash
gcc -std=c99 -Wall -o myprogram myprogram.c -lbashclient
```

For more details, see the [Usage Guide](../GUIDE.md) and
[API Reference](../API.md).
