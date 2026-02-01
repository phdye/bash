# Troubleshooting libbashclient

Solutions to common problems when building, linking, and using the
libbashclient C library.

---

## Table of Contents

1. [Build Issues](#build-issues)
2. [Linker Errors](#linker-errors)
3. [Connection Failures](#connection-failures)
4. [Authentication Errors](#authentication-errors)
5. [Protocol Errors](#protocol-errors)
6. [Memory Issues](#memory-issues)
7. [Segfaults and Crashes](#segfaults-and-crashes)
8. [Timeout Issues](#timeout-issues)
9. [Platform-Specific Issues](#platform-specific-issues)
10. [Debugging Tips](#debugging-tips)

---

## Build Issues

### "bashclient.h: No such file or directory"

The compiler cannot find the header file.

**Causes:**
- libbashclient is not installed
- Installed to a non-standard prefix

**Solutions:**

```bash
# Check if the header exists
ls /usr/local/include/bashclient.h

# If installed to a custom prefix, tell the compiler
gcc -I/opt/bashclient/include -o myapp myapp.c -L/opt/bashclient/lib -lbashclient

# Or use pkg-config
gcc $(pkg-config --cflags --libs libbashclient) -o myapp myapp.c
```

### "error: unknown type name 'bc_client_t'"

You forgot to include the header:

```c
#include <bashclient.h>  /* Add this */
```

### Compiler warns about implicit function declarations

The header is not included, or you are using a function added in a newer
API version than what is installed:

```c
#include <bashclient.h>

/* Check the installed API version */
#if BC_API_VERSION < 2
#error "This code requires libbashclient API version 2 or later"
#endif
```

### "error: 'for' loop initial declarations are only allowed in C99 mode"

The library requires C99. Add `-std=c99` or `-std=gnu99`:

```bash
gcc -std=c99 -o myapp myapp.c -lbashclient
```

### Make fails with "missing separator"

Ensure you are using tabs (not spaces) for recipe lines in Makefiles.
If you are using the library's Makefile, ensure you have GNU Make:

```bash
make --version    # Must be GNU Make >= 3.81
gmake             # On BSD systems
```

---

## Linker Errors

### "cannot find -lbashclient"

The linker cannot find the library file.

**Solutions:**

```bash
# Check if the library exists
ls /usr/local/lib/libbashclient.*

# Add the library directory to the search path
gcc -o myapp myapp.c -L/usr/local/lib -lbashclient

# Or set the environment variable
export LIBRARY_PATH=/usr/local/lib:$LIBRARY_PATH
gcc -o myapp myapp.c -lbashclient

# Or update ldconfig
echo "/usr/local/lib" | sudo tee /etc/ld.so.conf.d/bashclient.conf
sudo ldconfig
```

### "undefined reference to 'bc_connect'"

The library is not being linked. Ensure `-lbashclient` appears AFTER
your source files:

```bash
# WRONG: library before source
gcc -lbashclient -o myapp myapp.c

# CORRECT: library after source
gcc -o myapp myapp.c -lbashclient
```

### "error while loading shared libraries: libbashclient.so.0"

The runtime linker cannot find the shared library.

**Solutions:**

```bash
# Temporary (current session)
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
./myapp

# Permanent (system-wide)
echo "/usr/local/lib" | sudo tee /etc/ld.so.conf.d/bashclient.conf
sudo ldconfig

# Alternative: link statically
gcc -o myapp myapp.c -Wl,-Bstatic -lbashclient -Wl,-Bdynamic

# Alternative: set rpath at link time
gcc -o myapp myapp.c -lbashclient -Wl,-rpath,/usr/local/lib
```

### Multiple definition errors when linking statically

If linking the static library into a shared library, ensure it was
built with `-fPIC`:

```bash
make static CFLAGS="-O2 -Wall -fPIC"
```

---

## Connection Failures

### "Connect failed: Connection refused"

No bash-server is listening at the expected socket path.

**Checklist:**
1. Is bash-server running? `pgrep -a bash-server`
2. What socket is it using? Check the server's output or config
3. Does the socket file exist? `ls -la /tmp/bash-server-$(id -u)/sock`
4. Are you connecting to the right path?

```bash
# Start a server with an explicit name
bash-server --name mytest &

# Check what socket path it created
ls /tmp/bash-server-$(id -u)/

# Connect explicitly
bc_client_t *c = bc_connect("/tmp/bash-server-1000/sock", NULL);
```

### "Connect failed: Permission denied"

The socket file permissions do not allow your user to connect.

```bash
# Check socket permissions
ls -la /tmp/bash-server-*/sock

# The socket should be owned by the same user as your program
# If running as a different user, check the server's --socket option
```

### "Connect failed: No such file or directory"

The auto-discovery could not find a socket. Either:
- No server is running
- The socket is in a non-standard location

**Solutions:**

```bash
# Set the environment variable
export BASH_SERVER_SOCKET=/path/to/sock
./myapp

# Or pass the path explicitly in code
bc_client_t *c = bc_connect("/path/to/sock", NULL);
```

### "Connect failed: Connection timed out"

The server exists but is not responding within the timeout.

```c
/* Increase the connect timeout */
bc_opts_t opts = BC_OPTS_INIT;
opts.socket_path = "/path/to/sock";
opts.connect_timeout_ms = 30000;  /* 30 seconds */
bc_client_t *c = bc_connect_opts(&opts);
```

### Stdio transport: "Connect failed: exec failed"

The `stdio_command` could not be executed.

```c
bc_opts_t opts = BC_OPTS_INIT;
opts.transport = BC_TRANSPORT_STDIO;
/* Ensure the command is a full path or in PATH */
opts.stdio_command = "/usr/local/bin/bash-server --stdio";
```

---

## Authentication Errors

### "Authentication failed" (BC_ERR_AUTH)

The auth token does not match the server's token.

**Common causes:**
1. Server was restarted (new token generated)
2. Token file is stale or inaccessible
3. Wrong token passed explicitly

**Solutions:**

```bash
# Check if the token file exists and is readable
cat /tmp/bash-server-$(id -u)/sock.token

# If passing token explicitly, verify it matches
# The token is a 64-character hex string

# Restart your program to pick up the new token
```

```c
/* Read the token file manually */
FILE *f = fopen("/tmp/bash-server-1000/sock.token", "r");
char token[256];
if (f && fgets(token, sizeof(token), f)) {
    /* Strip trailing newline */
    token[strcspn(token, "\n")] = '\0';
    bc_client_t *c = bc_connect("/tmp/bash-server-1000/sock", token);
}
if (f) fclose(f);
```

### Auth works from CLI but not from code

If `bashclient` (the CLI tool) can connect but your C program cannot,
the issue is usually file permissions on the token file. Ensure your
program runs as the same user that started bash-server.

---

## Protocol Errors

### "Protocol error: unexpected channel" (BC_ERR_PROTOCOL)

The server sent a frame for an unexpected channel. This usually means
a version mismatch between the library and server.

**Solutions:**
- Update libbashclient to match the server version
- Force a specific protocol version:

```c
bc_opts_t opts = BC_OPTS_INIT;
opts.protocol_version = 2;
opts.wire_format = BC_WIRE_NDJSON;
bc_client_t *c = bc_connect_opts(&opts);
```

### "Protocol error: malformed JSON"

The server sent invalid JSON. This can happen with:
- Network corruption (rare with Unix sockets)
- Server bug
- Mixing binary and NDJSON framing

Enable debug logging to see the raw frames:

```bash
BC_DEBUG=1 ./myapp 2>debug.log
```

---

## Memory Issues

### Valgrind reports memory leaks

Common causes and fixes:

**Leaked bc_eval_result_t:**
```c
/* WRONG: result not freed */
bc_eval(c, "echo test");

/* CORRECT */
bc_eval_result_t *r = bc_eval(c, "echo test");
if (r) bc_eval_result_free(r);
```

**Leaked strings from state functions:**
```c
/* WRONG: string not freed */
printf("HOME=%s\n", bc_var_get(c, "HOME"));

/* CORRECT */
char *val = bc_var_get(c, "HOME");
if (val) {
    printf("HOME=%s\n", val);
    bc_free(val);
}
```

**Leaked client handle:**
```c
/* WRONG: early return without cleanup */
if (error_condition) return 1;
bc_close(c);

/* CORRECT: use goto cleanup */
if (error_condition) goto cleanup;
/* ... */
cleanup:
    bc_close(c);
    return rc;
```

### Running Valgrind

```bash
# Build with debug symbols
make DEBUG=1

# Run under Valgrind
valgrind --leak-check=full --show-leak-kinds=all ./myapp

# With suppression file for known OS/libc issues
valgrind --leak-check=full --suppressions=valgrind.supp ./myapp
```

### "Invalid free" or "mismatched free"

You used `free()` instead of `bc_free()` for a library-allocated string,
or vice versa:

```c
/* WRONG */
char *val = bc_var_get(c, "HOME");
free(val);  /* Should be bc_free(val) */

/* WRONG */
char *mystr = strdup("hello");
bc_free(mystr);  /* Should be free(mystr) */
```

---

## Segfaults and Crashes

### Segfault in bc_eval()

**Common causes:**
1. NULL client handle
2. Client handle was already closed
3. Connection was lost

```c
/* Always check the handle */
if (!c) {
    fprintf(stderr, "Client handle is NULL\n");
    return;
}

/* Check for transport errors after long pauses */
if (bc_ping(c) != BC_OK) {
    fprintf(stderr, "Connection lost, reconnecting\n");
    bc_close(c);
    c = bc_connect(NULL, NULL);
}
```

### Segfault in callback function

Ensure your callback does not access freed memory:

```c
/* WRONG: userdata points to freed stack variable */
void setup(bc_client_t *c) {
    int count = 0;
    bc_on_pre_command(c, my_cb, &count);
    /* count goes out of scope! */
}

/* CORRECT: use heap-allocated or static storage */
static int g_count = 0;
bc_on_pre_command(c, my_cb, &g_count);
```

### Crash when using handle from multiple threads

libbashclient is NOT thread-safe. Each thread needs its own handle:

```c
/* WRONG */
bc_client_t *shared = bc_connect(NULL, NULL);
/* Thread 1: */ bc_eval(shared, "echo 1");
/* Thread 2: */ bc_eval(shared, "echo 2");  /* Race condition */

/* CORRECT */
/* Thread 1: */ bc_client_t *c1 = bc_connect(NULL, NULL);
/* Thread 2: */ bc_client_t *c2 = bc_connect(NULL, NULL);
```

---

## Timeout Issues

### Operations always time out

**Check server responsiveness:**
```c
/* Try a simple ping first */
int rc = bc_ping(c);
if (rc == BC_ERR_TIMEOUT)
    printf("Server is not responding\n");
```

**Increase timeouts:**
```c
bc_opts_t opts = BC_OPTS_INIT;
opts.read_timeout_ms = 120000;   /* 2 minutes */
opts.write_timeout_ms = 30000;   /* 30 seconds */
bc_client_t *c = bc_connect_opts(&opts);
```

**Check if server is overloaded:**
```bash
# Check server process
ps aux | grep bash-server

# Check system resources
top -p $(pgrep bash-server)
```

### bc_poll() never returns events

1. Did you set the observe level?
   ```c
   bc_observe_set_level(c, 2);  /* Must be > 0 */
   ```

2. Did you register callbacks?
   ```c
   bc_on_pre_command(c, my_callback, NULL);
   ```

3. Are commands being executed? Observer events only fire when the
   server executes commands.

4. Is the timeout long enough? Try `bc_poll(c, 5000)` for 5 seconds.

---

## Platform-Specific Issues

### macOS: "dyld: Library not loaded"

```bash
# Set library path
export DYLD_LIBRARY_PATH=/usr/local/lib:$DYLD_LIBRARY_PATH

# Or embed the path in the binary
gcc -o myapp myapp.c -lbashclient -Wl,-rpath,/usr/local/lib

# Or use install_name_tool
install_name_tool -add_rpath /usr/local/lib myapp
```

### macOS: SIP strips DYLD_LIBRARY_PATH

System Integrity Protection removes `DYLD_LIBRARY_PATH` for system
binaries. Install the library to `/usr/local/lib` (which is in the
default search path) or use `-rpath` at link time.

### Cygwin: "cygbashclient-0.dll not found"

On Cygwin, DLLs must be in a directory listed in `PATH`:

```bash
export PATH=/usr/local/bin:/usr/local/lib:$PATH
```

Or copy the DLL next to your executable.

### Cygwin: Unix socket connection fails

Cygwin Unix sockets use an emulation layer. If connections fail:

1. Verify the socket file exists: `ls -la /tmp/bash-server-*/sock`
2. Try the named pipe transport instead:

```c
bc_opts_t opts = BC_OPTS_INIT;
opts.transport = BC_TRANSPORT_PIPE;
opts.pipe_name = "\\\\.\\pipe\\bash-server";
bc_client_t *c = bc_connect_opts(&opts);
```

### FreeBSD: build fails with "make: don't know how to make"

Use GNU Make:

```bash
gmake
gmake check
```

---

## Debugging Tips

### Enable Debug Logging

Set the `BC_DEBUG` environment variable to get verbose logging to stderr:

```bash
BC_DEBUG=1 ./myapp 2>debug.log
```

This logs:
- Every frame sent and received (truncated to 200 bytes)
- Connection and authentication steps
- Callback dispatch events
- Error details

### Inspect Wire Traffic

Use `socat` to observe the raw protocol traffic:

```bash
# Create a proxy socket
socat -v UNIX-LISTEN:/tmp/proxy-sock,fork \
         UNIX-CONNECT:/tmp/bash-server-1000/sock

# Connect your program to the proxy
bc_client_t *c = bc_connect("/tmp/proxy-sock", NULL);
```

### Use GDB

```bash
# Build with debug symbols
make DEBUG=1

# Run under GDB
gdb ./myapp

(gdb) break bc_eval
(gdb) run
(gdb) bt          # Backtrace when it crashes
(gdb) print *c    # Print client struct (if not opaque in debug build)
```

### Check Return Values

The single most effective debugging technique: check every return value.
Add temporary logging:

```c
#define CHECK(expr) do { \
    int _rc = (expr); \
    if (_rc != BC_OK) { \
        fprintf(stderr, "%s:%d: %s = %d (%s)\n", \
                __FILE__, __LINE__, #expr, _rc, bc_last_error_msg(c)); \
    } \
} while(0)

CHECK(bc_var_set(c, "X", "1"));
CHECK(bc_observe_set_level(c, 2));
CHECK(bc_debug_enable(c));
```

### Reproduce with bashclient CLI

The `bashclient` command-line tool uses the same protocol. Try your
operations there first to rule out library issues:

```bash
# Connect and evaluate
bashclient eval "echo hello"

# Interactive mode
bashclient -i

# Verbose mode
bashclient -v eval "echo hello"
```

If `bashclient` works but your code does not, the issue is in your code.
If `bashclient` also fails, the issue is in the server or protocol.
