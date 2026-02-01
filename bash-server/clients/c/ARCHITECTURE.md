# libbashclient Architecture

This document describes the internal architecture of the libbashclient C
library. It is intended for developers who want to understand, modify, or
extend the library internals.

---

## Table of Contents

1. [Overview](#overview)
2. [Source Structure](#source-structure)
3. [Public API Surface](#public-api-surface)
4. [Internal Data Structures](#internal-data-structures)
   - [bc_client (Opaque Handle)](#bc_client-opaque-handle)
   - [Transport State](#transport-state)
   - [Protocol State](#protocol-state)
   - [Callback Registry](#callback-registry)
   - [Read Buffer](#read-buffer)
5. [Transport Layer](#transport-layer)
   - [Transport Interface](#transport-interface)
   - [Unix Socket Transport](#unix-socket-transport)
   - [Stdio Transport](#stdio-transport)
   - [File Descriptor Transport](#file-descriptor-transport)
   - [Named Pipe Transport](#named-pipe-transport)
6. [Protocol Layer](#protocol-layer)
   - [NDJSON Framing](#ndjson-framing)
   - [Binary v2 Framing](#binary-v2-framing)
   - [JSON Parsing](#json-parsing)
   - [JSON Generation](#json-generation)
   - [Protocol Auto-Detection](#protocol-auto-detection)
7. [Channel Dispatch](#channel-dispatch)
   - [Request-Response Flow](#request-response-flow)
   - [Server-Push Events](#server-push-events)
   - [Channel Routing Table](#channel-routing-table)
8. [Callback Mechanism](#callback-mechanism)
   - [Registration](#registration)
   - [Dispatch](#dispatch)
   - [Reentrancy](#reentrancy)
9. [Memory Ownership Model](#memory-ownership-model)
   - [Allocation Strategy](#allocation-strategy)
   - [Ownership Transfer Rules](#ownership-transfer-rules)
   - [String Interning](#string-interning)
   - [Result Struct Layout](#result-struct-layout)
10. [Error Handling Internals](#error-handling-internals)
11. [Testing Architecture](#testing-architecture)
    - [Test Framework](#test-framework)
    - [Mock Server](#mock-server)
    - [Test Categories](#test-categories)
12. [Build System](#build-system)
13. [Design Decisions](#design-decisions)

---

## Overview

libbashclient is a synchronous, blocking C99 client library for
communicating with bash-server over its v2 protocol. The library is
structured in three layers:

```
  +-----------------------------------------+
  |           Public API (bashclient.h)      |
  |  bc_connect, bc_eval, bc_var_get, ...    |
  +-----------------------------------------+
  |         Channel Logic (bashclient.c)     |
  |  Request construction, response parsing  |
  |  Callback dispatch, result construction  |
  +-----------------------------------------+
  |         Protocol (protocol.c)            |
  |  NDJSON/binary framing, JSON parse/gen   |
  +-----------------------------------------+
  |         Transport (transport.c)          |
  |  Socket, stdio, fd, named pipe I/O       |
  +-----------------------------------------+
```

Each layer has a clean interface to the layer below it. The transport
layer deals in raw bytes, the protocol layer deals in JSON frames, and
the channel logic deals in typed C structures.

---

## Source Structure

```
bash-server/clients/c/
  include/
    bashclient.h          Public API header
                            - All public types (bc_client_t, bc_eval_result_t, ...)
                            - All public functions (bc_connect, bc_eval, ...)
                            - Error code constants (BC_OK, BC_ERR_*)
                            - Version macros (BC_API_VERSION, BC_VERSION_STRING)
                            - Opaque forward declarations
  src/
    internal.h            Private declarations
                            - struct bc_client definition
                            - Transport vtable (bc_transport_ops)
                            - Protocol frame types
                            - Internal helper prototypes
                            - Buffer management types
    bashclient.c          Core client implementation (~2000 lines)
                            - bc_connect() / bc_connect_opts() / bc_close()
                            - Channel 0: control (ping, configure, disconnect)
                            - Channel 1: command (eval, eval_stream, eval_parsed)
                            - Channel 2: state (var/func/alias/trap get/set/unset, inspect)
                            - Channel 3: observe (set_level, callbacks)
                            - Channel 4: debug (enable/disable, breakpoints, stepping, AST)
                            - Channel 5: PTY (spawn, read, write, resize, close, strip_ansi)
                            - bc_poll() event dispatcher
                            - Error state management
                            - Result struct constructors and free functions
    protocol.c            Wire protocol implementation (~800 lines)
                            - NDJSON frame read/write
                            - Binary v2 frame read/write (6-byte header)
                            - JSON parser (minimal, no external dependency)
                            - JSON generator (string builder)
                            - Protocol version auto-detection
                            - Base64 encoding (for v1 fallback)
    transport.c           Transport abstraction (~600 lines)
                            - Transport vtable dispatch
                            - Unix socket: connect, read, write, close, get_fd
                            - Stdio: fork/exec, pipe setup, read, write, close
                            - File descriptor: wrap existing fds
                            - Named pipe: CreateFile (via Cygwin), read, write
                            - Timeout enforcement on all I/O
                            - Non-blocking I/O setup
  tests/
    test_connect.c        Connection and auth tests
    test_eval.c           Command evaluation tests
    test_state.c          State channel tests
    test_observe.c        Observer callback tests
    test_debug.c          Debug channel tests
    test_pty.c            PTY channel tests
    test_memory.c         Memory leak / double-free tests
    mock_server.c         In-process mock bash-server
    mock_server.h         Mock server declarations
    test_harness.h        Test macros (ASSERT_*, RUN_TEST, ...)
  examples/
    eval.c                Basic evaluation example
    observe.c             Observer example
    debugger.c            Interactive debugger
    pty.c                 PTY terminal emulator
  Makefile                Build rules for lib, tests, examples
  libbashclient.pc.in    pkg-config template
```

### File Size Budget

| File            | Approximate Lines | Purpose                    |
|-----------------|-------------------|----------------------------|
| bashclient.h    | 400               | All public declarations    |
| internal.h      | 200               | Private structures         |
| bashclient.c    | 2000              | All channel logic          |
| protocol.c      | 800               | Framing and JSON           |
| transport.c     | 600               | I/O abstraction            |
| **Total**       | **4000**          |                            |

The entire library is under 4000 lines of C, plus the 400-line public
header, making it practical for a single developer to understand fully.

---

## Public API Surface

The public header `bashclient.h` exposes:

### Types

| Type                         | Kind        | Notes                        |
|------------------------------|-------------|------------------------------|
| `bc_client_t`                | Opaque ptr  | Client handle                |
| `bc_opts_t`                  | Struct      | Connection options           |
| `bc_eval_result_t`           | Struct      | Eval output + exit code      |
| `bc_var_info_t`              | Struct      | Variable with attributes     |
| `bc_pre_command_event_t`     | Struct      | Pre-command callback data    |
| `bc_post_command_event_t`    | Struct      | Post-command callback data   |
| `bc_breakpoint_t`            | Struct      | Breakpoint descriptor        |
| `bc_break_hit_event_t`       | Struct      | Breakpoint hit callback data |
| `bc_debug_status_t`          | Struct      | Debugger state               |
| `bc_pty_info_t`              | Struct      | PTY session info             |

### Constants

| Constant              | Value | Notes                    |
|-----------------------|-------|--------------------------|
| `BC_OK`               | 0     | Success                  |
| `BC_ERR_AUTH`         | -1    | Authentication failed    |
| `BC_ERR_PROTOCOL`     | -2    | Protocol error           |
| `BC_ERR_TIMEOUT`      | -3    | Operation timed out      |
| `BC_ERR_TRANSPORT`    | -4    | Transport error          |
| `BC_ERR_SERVER`       | -5    | Server error             |
| `BC_ERR_NOMEM`        | -6    | Out of memory            |
| `BC_ERR_PARAM`        | -7    | Invalid parameter        |
| `BC_API_VERSION`      | 1     | API version number       |
| `BC_TRANSPORT_SOCKET` | 0     | Unix socket transport    |
| `BC_TRANSPORT_STDIO`  | 1     | Stdio transport          |
| `BC_TRANSPORT_FD`     | 2     | File descriptor transport|
| `BC_TRANSPORT_PIPE`   | 3     | Named pipe transport     |
| `BC_WIRE_BINARY`      | 0     | Binary v2 framing        |
| `BC_WIRE_NDJSON`      | 1     | NDJSON framing           |

### Function Count by Channel

| Channel   | Functions | Notes                                   |
|-----------|-----------|-----------------------------------------|
| Connect   | 9         | connect, connect_opts, close, ping, etc.|
| Command   | 2         | eval, eval_stream                       |
| State     | 12        | var/func/alias/trap get/set/unset + inspect |
| Observe   | 5         | set_level, on_pre/post, poll            |
| Debug     | 15        | enable/disable, break*, step*, inspect  |
| PTY       | 7         | spawn, read, write, resize, close, etc. |
| Internal  | 4         | free, version, last_error, get_fd       |
| **Total** | **54**    |                                         |

---

## Internal Data Structures

### bc_client (Opaque Handle)

The `bc_client_t` type is a typedef for `struct bc_client`, defined in
`internal.h`:

```c
struct bc_client {
    /* Transport */
    bc_transport_t    transport;    /* Transport state */
    int               protocol_ver; /* Negotiated protocol version (1 or 2) */
    int               wire_format;  /* BC_WIRE_BINARY or BC_WIRE_NDJSON */

    /* Authentication */
    char             *auth_token;   /* Token string (heap-allocated) */
    int               authenticated; /* 1 after successful AUTH */

    /* Timeouts (milliseconds) */
    int               connect_timeout_ms;
    int               read_timeout_ms;
    int               write_timeout_ms;

    /* Error state */
    int               last_error;      /* BC_OK or BC_ERR_* */
    char              last_error_msg[256]; /* Human-readable message */

    /* Read buffer */
    bc_buffer_t       rbuf;            /* Buffered incoming data */

    /* Callbacks */
    bc_callback_t     callbacks[BC_MAX_CALLBACKS];

    /* Request sequence counter */
    int               next_seq;

    /* Debug state */
    int               debug_enabled;

    /* Observe state */
    int               observe_level;

    /* PTY state */
    int               pty_active;
    int               pty_strip_ansi;
};
```

The struct is approximately 1 KB in size. It is allocated with `malloc()`
in `bc_connect()` and freed in `bc_close()`.

### Transport State

```c
typedef struct {
    int               type;        /* BC_TRANSPORT_* */
    int               fd_read;     /* Read file descriptor */
    int               fd_write;    /* Write file descriptor (may == fd_read) */
    pid_t             child_pid;   /* For stdio transport: child PID */
    bc_transport_ops *ops;         /* Virtual function table */
    void             *priv;        /* Transport-specific private data */
} bc_transport_t;
```

The `ops` field points to a static vtable:

```c
typedef struct {
    int  (*connect)(bc_transport_t *t, const bc_opts_t *opts);
    int  (*read)(bc_transport_t *t, void *buf, size_t len, int timeout_ms);
    int  (*write)(bc_transport_t *t, const void *buf, size_t len, int timeout_ms);
    void (*close)(bc_transport_t *t);
    int  (*get_fd)(bc_transport_t *t);
} bc_transport_ops;
```

### Protocol State

Protocol framing is stateless -- each frame is self-contained. The
protocol layer functions operate on the transport directly:

```c
/* Read one JSON frame from the transport */
int proto_read_frame(bc_transport_t *t, bc_buffer_t *buf,
                     char **json_out, int wire_format, int timeout_ms);

/* Write one JSON frame to the transport */
int proto_write_frame(bc_transport_t *t, const char *json,
                      size_t len, int wire_format, int timeout_ms);
```

### Callback Registry

```c
#define BC_MAX_CALLBACKS 8

typedef enum {
    BC_CB_PRE_COMMAND = 0,
    BC_CB_POST_COMMAND,
    BC_CB_BREAK_HIT,
    BC_CB_EVAL_STDOUT,
    BC_CB_EVAL_STDERR,
    BC_CB_EVAL_COMPLETE,
    /* ... */
} bc_callback_type;

typedef struct {
    bc_callback_type  type;
    void            (*fn)(const void *event, void *userdata);
    void             *userdata;
} bc_callback_t;
```

Callbacks are stored in a fixed-size array. Registration replaces any
existing callback of the same type. Passing `NULL` as the function
pointer removes the callback.

### Read Buffer

```c
typedef struct {
    char   *data;      /* Heap-allocated buffer */
    size_t  len;       /* Bytes of valid data */
    size_t  cap;       /* Allocated capacity */
    size_t  pos;       /* Current read position */
} bc_buffer_t;
```

The read buffer accumulates data from the transport until a complete
frame can be parsed. It grows geometrically (doubling) up to a maximum
of 16 MB. After a frame is consumed, remaining data is shifted to the
front (compacted) when `pos > cap / 2`.

---

## Transport Layer

### Transport Interface

All transport types implement the same vtable interface. The transport
layer is entirely contained in `transport.c`. The rest of the library
never performs raw I/O -- it always goes through the transport ops.

```
bc_connect()
  |
  v
transport_create()
  |
  +-- socket_connect()   [Unix domain socket]
  +-- stdio_connect()    [Fork + exec bash-server --stdio]
  +-- fd_connect()       [Wrap existing file descriptors]
  +-- pipe_connect()     [Windows Named Pipe via Cygwin]
```

### Unix Socket Transport

The most common transport. Connection flow:

1. Resolve socket path (options -> env -> config file -> default)
2. Create `AF_UNIX` socket
3. Set `SO_RCVTIMEO` and `SO_SNDTIMEO` based on timeout options
4. `connect()` to the server's socket path
5. Read auth token from token file (if not provided explicitly)
6. Return the connected socket fd

```c
static int socket_connect(bc_transport_t *t, const bc_opts_t *opts)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    /* Set timeouts */
    struct timeval tv;
    tv.tv_sec = opts->connect_timeout_ms / 1000;
    tv.tv_usec = (opts->connect_timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        return BC_ERR_TRANSPORT;

    t->fd_read = t->fd_write = fd;
    return BC_OK;
}
```

### Stdio Transport

For `--stdio` mode. The library forks and execs bash-server:

1. Create two pipes: `stdin_pipe` (lib writes, server reads) and
   `stdout_pipe` (server writes, lib reads)
2. `fork()`
3. Child: redirect stdin/stdout to pipes, `exec("bash-server", "--stdio")`
4. Parent: store pipe fds and child PID in transport

The child process is terminated with `SIGTERM` when `bc_close()` is
called, followed by `waitpid()` to reap it.

### File Descriptor Transport

The simplest transport. The caller provides pre-opened read and write
file descriptors:

```c
static int fd_connect(bc_transport_t *t, const bc_opts_t *opts)
{
    t->fd_read = opts->fd_read;
    t->fd_write = opts->fd_write;
    return BC_OK;
}
```

The library does NOT close these fds on `bc_close()` since it does not
own them. The caller is responsible for closing them.

### Named Pipe Transport

Cygwin-specific. Uses Windows Named Pipes accessed through Cygwin's
POSIX compatibility layer:

1. Open the named pipe with `open()` (Cygwin translates `\\.\pipe\...`
   paths to Win32 API calls)
2. The pipe is bidirectional, so `fd_read == fd_write`
3. Timeout enforcement uses `poll()` before each read/write

---

## Protocol Layer

### NDJSON Framing

Newline-Delimited JSON. Each frame is a single line of JSON terminated
by `\n` (0x0A):

```
{"channel":1,"type":"eval","command":"echo hello"}\n
```

Reading:
1. Accumulate bytes in the read buffer
2. Scan for `\n`
3. Extract everything before `\n` as the JSON frame
4. Compact the buffer

Writing:
1. Serialize the JSON object
2. Append `\n`
3. Write the entire line to the transport

NDJSON is the default wire format because it is human-readable and easy
to debug with tools like `socat` and `jq`.

### Binary v2 Framing

Length-prefixed frames with a 6-byte header:

```
Byte 0:    Channel ID (0-5)
Byte 1:    Flags (reserved, must be 0)
Bytes 2-5: Payload length (uint32, big-endian)
Bytes 6+:  JSON payload (UTF-8, no trailing newline)
```

Reading:
1. Read exactly 6 bytes (the header)
2. Extract channel and length
3. Read exactly `length` bytes (the payload)
4. Parse payload as JSON

Writing:
1. Serialize the JSON payload
2. Write 6-byte header with channel and payload length
3. Write the payload

Binary framing is more efficient for high-throughput scenarios because it
avoids scanning for newlines.

### JSON Parsing

The library includes a minimal JSON parser in `protocol.c`. It supports
the subset of JSON used by the bash-server protocol:

- Objects with string keys
- String values (with escape handling)
- Integer values
- Boolean values (`true`/`false`)
- Null values
- Arrays of strings or objects

The parser is recursive-descent and operates on a `const char *` input.
It does not build a DOM tree. Instead, callers use accessor functions:

```c
/* Find a string value by key */
const char *json_get_str(const char *json, const char *key,
                         char *buf, size_t buflen);

/* Find an integer value by key */
int json_get_int(const char *json, const char *key, int *out);

/* Find a boolean value by key */
int json_get_bool(const char *json, const char *key, int *out);

/* Iterate array elements */
int json_array_iter(const char *json, const char *key,
                    json_iter_fn fn, void *userdata);
```

This streaming approach avoids allocating a full parse tree, keeping
memory usage predictable.

### JSON Generation

JSON output is built using a simple string builder:

```c
typedef struct {
    char   *buf;
    size_t  len;
    size_t  cap;
} json_builder_t;

void jb_init(json_builder_t *jb);
void jb_object_start(json_builder_t *jb);
void jb_object_end(json_builder_t *jb);
void jb_key_str(json_builder_t *jb, const char *key, const char *val);
void jb_key_int(json_builder_t *jb, const char *key, int val);
void jb_key_bool(json_builder_t *jb, const char *key, int val);
char *jb_finish(json_builder_t *jb);  /* Returns owned string */
void jb_free(json_builder_t *jb);
```

Example usage inside the library:

```c
static char *build_eval_request(const char *command, int seq)
{
    json_builder_t jb;
    jb_init(&jb);
    jb_object_start(&jb);
    jb_key_str(&jb, "type", "eval");
    jb_key_str(&jb, "command", command);
    jb_key_int(&jb, "seq", seq);
    jb_object_end(&jb);
    return jb_finish(&jb);
}
```

### Protocol Auto-Detection

When connecting, the library can auto-detect the server's protocol
version by examining the first byte of the server's response:

| First Byte     | Detection                   |
|----------------|-----------------------------|
| `{` (0x7B)     | NDJSON (v2)                 |
| `\n` (0x0A)    | NDJSON (v2, empty keepalive)|
| 0x00 - 0x05    | Binary v2 (channel ID)      |
| Printable ASCII| v1 text protocol            |

The library defaults to requesting v2 NDJSON but falls back to v1 if
the server only supports it.

---

## Channel Dispatch

### Request-Response Flow

Most API calls follow a synchronous request-response pattern:

```
bc_eval(c, "echo hello")
  |
  v
build_eval_request()          -- Construct JSON
  |
  v
proto_write_frame()           -- Send frame via transport
  |
  v
wait_for_response(channel=1)  -- Read frames until matching response
  |
  v
parse_eval_response()         -- Extract stdout, stderr, exit_code
  |
  v
bc_eval_result_t *            -- Return to caller
```

The `wait_for_response()` function reads frames in a loop. If a frame
arrives for a different channel (e.g., an OBSERVE event while waiting
for a COMMAND response), it is either dispatched to a callback immediately
or buffered for later `bc_poll()` dispatch.

### Server-Push Events

Events from OBSERVE and DEBUG channels arrive asynchronously. They are
processed during:

1. `bc_poll()` -- explicit polling by the caller
2. `wait_for_response()` -- while waiting for a synchronous response

The dispatch order is:

```
Frame arrives
  |
  v
Channel routing
  |
  +-- Channel 0 (CONTROL): process internally
  +-- Channel 1 (COMMAND): if waiting for response, deliver; else buffer
  +-- Channel 2 (STATE): if waiting for response, deliver; else buffer
  +-- Channel 3 (OBSERVE): dispatch to registered callback
  +-- Channel 4 (DEBUG): dispatch to registered callback
  +-- Channel 5 (PTY): buffer for bc_pty_read()
```

### Channel Routing Table

```c
typedef int (*channel_handler_fn)(struct bc_client *c, const char *json);

static const channel_handler_fn channel_handlers[6] = {
    [0] = handle_control,
    [1] = handle_command,
    [2] = handle_state,
    [3] = handle_observe,
    [4] = handle_debug,
    [5] = handle_pty,
};
```

Each handler parses the JSON and either returns a result (for
request-response) or invokes a callback (for events).

---

## Callback Mechanism

### Registration

Callbacks are registered by storing a function pointer and userdata in the
client's callback array:

```c
void bc_on_pre_command(bc_client_t *c,
                       void (*fn)(const bc_pre_command_event_t *, void *),
                       void *userdata)
{
    c->callbacks[BC_CB_PRE_COMMAND].fn = (void (*)(const void *, void *))fn;
    c->callbacks[BC_CB_PRE_COMMAND].userdata = userdata;
}
```

Passing `fn = NULL` removes the callback.

### Dispatch

When an event frame arrives and a matching callback is registered:

1. Parse the JSON into the appropriate event struct (stack-allocated)
2. Copy string fields from the JSON into heap-allocated strings
3. Call the callback function with the event struct and userdata
4. Free the heap-allocated strings after the callback returns

```c
static int handle_observe_pre(struct bc_client *c, const char *json)
{
    bc_pre_command_event_t ev = {0};
    /* Parse JSON into ev fields */
    json_get_int(json, "seq", &ev.seq);
    /* ... */

    if (c->callbacks[BC_CB_PRE_COMMAND].fn) {
        c->callbacks[BC_CB_PRE_COMMAND].fn(&ev,
            c->callbacks[BC_CB_PRE_COMMAND].userdata);
    }

    /* Free strings allocated during parsing */
    free(ev.command);
    free(ev.cwd);
    return BC_OK;
}
```

The event struct is only valid for the duration of the callback. If the
callback needs to retain any data, it must copy it.

### Reentrancy

Callbacks must NOT call back into the library on the same client handle.
Doing so would cause protocol corruption because the library may be in
the middle of reading a frame sequence. The following is NOT supported:

```c
/* BAD: re-entering the library from a callback */
static void on_pre(const bc_pre_command_event_t *ev, void *userdata)
{
    bc_client_t *c = (bc_client_t *)userdata;
    bc_eval(c, "echo in callback");  /* UNDEFINED BEHAVIOR */
}
```

If you need to trigger operations from callbacks, set a flag and perform
the operation after `bc_poll()` returns.

---

## Memory Ownership Model

### Allocation Strategy

The library uses standard `malloc()` / `realloc()` / `free()` for all
heap allocations. The `bc_free()` function is a thin wrapper:

```c
void bc_free(void *ptr)
{
    free(ptr);
}
```

The reason `bc_free()` exists is forward-compatibility: if the library
ever uses a custom allocator (arena, pool), callers using `bc_free()` will
automatically use the correct deallocator.

### Ownership Transfer Rules

```
Library allocates     -> Caller must free
  bc_connect()             bc_close()
  bc_eval()                bc_eval_result_free()
  bc_var_get()             bc_free()
  bc_debug_status()        bc_debug_status_free()
  bc_pty_spawn()           bc_pty_info_free()

Caller allocates      -> Caller must free
  Strings passed to bc_eval(), bc_var_set(), etc.
  The library copies them internally.

Library owns          -> Caller must NOT free
  bc_last_error_msg()      Valid until next operation
  bc_version()             Static string
```

### String Interning

The library does NOT intern strings. Every `bc_var_get()` call allocates
a new string even if the value has not changed. This simplifies the
implementation but means callers should avoid calling state-query functions
in tight loops without freeing the results.

### Result Struct Layout

All result structs follow the same pattern:

```c
/* All heap-allocated fields are at the beginning */
struct bc_eval_result {
    char  *stdout_data;    /* Heap-allocated, may be "" but never NULL */
    char  *stderr_data;    /* Heap-allocated, may be "" but never NULL */
    int    exit_code;      /* Value type, no cleanup needed */
};
```

The free function frees each pointer field, then the struct itself:

```c
void bc_eval_result_free(bc_eval_result_t *r)
{
    if (!r) return;
    free(r->stdout_data);
    free(r->stderr_data);
    free(r);
}
```

Passing NULL to any free function is safe (no-op).

---

## Error Handling Internals

Errors are stored per-client in `last_error` and `last_error_msg`. Every
public API function that can fail updates these fields before returning:

```c
static int set_error(struct bc_client *c, int code, const char *fmt, ...)
{
    if (c) {
        c->last_error = code;
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(c->last_error_msg, sizeof(c->last_error_msg), fmt, ap);
        va_end(ap);
    } else {
        /* Global error state for bc_connect() failures */
        g_last_error = code;
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(g_last_error_msg, sizeof(g_last_error_msg), fmt, ap);
        va_end(ap);
    }
    return code;
}
```

The global error state (for `bc_connect()` failures) is stored in
file-scoped static variables. This is the one non-thread-safe aspect of
the library even for independent handles.

---

## Testing Architecture

### Test Framework

Tests use a minimal custom framework defined in `test_harness.h`:

```c
#define ASSERT_EQ(a, b)    do { if ((a) != (b)) { \
    fprintf(stderr, "FAIL %s:%d: %s != %s (%d != %d)\n", \
            __FILE__, __LINE__, #a, #b, (int)(a), (int)(b)); \
    return 1; } } while(0)

#define ASSERT_STR_EQ(a, b) do { if (strcmp((a), (b)) != 0) { \
    fprintf(stderr, "FAIL %s:%d: \"%s\" != \"%s\"\n", \
            __FILE__, __LINE__, (a), (b)); \
    return 1; } } while(0)

#define ASSERT_NOT_NULL(p) do { if (!(p)) { \
    fprintf(stderr, "FAIL %s:%d: %s is NULL\n", \
            __FILE__, __LINE__, #p); \
    return 1; } } while(0)

#define RUN_TEST(fn) do { \
    printf("  %-40s", #fn); \
    if (fn() == 0) { printf("PASS\n"); passed++; } \
    else { printf("FAIL\n"); failed++; } \
    total++; } while(0)
```

### Mock Server

Tests that do not need a real bash-server use `mock_server.c`, which
implements a minimal in-process server:

```c
/* Start a mock server on a Unix socket */
int mock_server_start(const char *socket_path);

/* Set canned responses for specific requests */
void mock_server_expect(int channel, const char *type,
                        const char *response_json);

/* Wait for the mock to process a request */
int mock_server_wait(int timeout_ms);

/* Stop the mock server */
void mock_server_stop(void);
```

The mock server runs in a child process (forked) and communicates over
the same Unix socket protocol as the real server. This allows testing
the full library stack (transport + protocol + channel logic) without
a real bash process.

### Test Categories

| Category     | Tests | Needs Server | Description                    |
|--------------|-------|--------------|--------------------------------|
| test_connect | 5     | Mock         | Socket, auth, timeout, reconnect |
| test_eval    | 8     | Mock         | Eval, stream, parsed, errors   |
| test_state   | 12    | Mock         | All 4 namespaces, inspect      |
| test_observe | 6     | Mock         | Level set, callbacks, events   |
| test_debug   | 9     | Mock         | Break, step, AST, conditions   |
| test_pty     | 5     | Mock         | Spawn, I/O, resize, close      |
| test_memory  | 4     | Mock         | Leak checks, double-free safety|

All tests use `alarm(10)` for a 10-second watchdog timer. If a test
hangs, it is killed by SIGALRM.

---

## Build System

The Makefile supports the following variables and targets:

```makefile
# Variables
CC       = cc
CFLAGS   = -std=c99 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L
LDFLAGS  =
PREFIX   = /usr/local
LIBDIR   = $(PREFIX)/lib
INCDIR   = $(PREFIX)/include

# Shared library
SONAME   = libbashclient.so.0
REALNAME = libbashclient.so.0.1.0

# Objects
OBJS     = src/bashclient.o src/protocol.o src/transport.o
```

The Makefile detects the platform (`uname -s`) and adjusts shared library
naming and flags accordingly (`.so` on Linux, `.dylib` on macOS,
`.dll` on Cygwin).

---

## Design Decisions

### Why synchronous/blocking?

A synchronous API is simpler to use correctly in C. Async APIs in C
require either callbacks-everywhere (complicating error handling and
memory management) or coroutine libraries (adding dependencies). The
blocking API with `bc_poll()` for events provides a good balance.

### Why no external JSON library?

Eliminating external dependencies makes the library trivial to build
on any POSIX system. The protocol uses a small JSON subset, so a
minimal parser is sufficient. The parser is ~300 lines of C.

### Why a fixed callback array instead of a linked list?

There are only 8 callback types. A fixed array avoids heap allocation
for callback management and provides O(1) lookup and replacement.

### Why not thread-safe?

Thread safety would require mutexes around all state access, adding
complexity and overhead. The expected usage pattern is one client per
thread, which makes external locking unnecessary. The library documents
this restriction clearly.

### Why bc_free() instead of plain free()?

Forward-compatibility. If a future version uses a custom allocator
(e.g., pool allocator for reduced fragmentation), `bc_free()` will
route to the correct deallocator. It costs nothing today (just a
wrapper) and prevents hard-to-diagnose issues later.

### Why copy strings passed by the caller?

Defensive programming. The library does not know the lifetime of
caller-provided strings. Copying ensures the library's internal state
is always valid regardless of what the caller does with the original
string after the call. The overhead is negligible for the typical
use case (short shell commands and variable names).
