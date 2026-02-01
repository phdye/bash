# Installing libbashclient

This document covers building and installing the libbashclient C library,
which provides a client interface to bash-server.

---

## Table of Contents

1. [System Requirements](#system-requirements)
2. [Quick Start](#quick-start)
3. [Build from Source](#build-from-source)
4. [Installation](#installation)
5. [Shared vs Static Linking](#shared-vs-static-linking)
6. [Platform Notes](#platform-notes)
7. [pkg-config Integration](#pkg-config-integration)
8. [Verifying the Installation](#verifying-the-installation)
9. [Uninstalling](#uninstalling)
10. [Cross-Compilation](#cross-compilation)
11. [Troubleshooting Build Issues](#troubleshooting-build-issues)

---

## System Requirements

### Required

| Component          | Minimum Version | Notes                              |
|--------------------|----------------|------------------------------------|
| C compiler         | C99            | GCC >= 4.9, Clang >= 3.5          |
| POSIX environment  | POSIX.1-2008   | Linux, macOS, Cygwin, FreeBSD     |
| GNU Make           | 3.81           | BSD make may work but is untested  |
| bash-server        | 0.1.0          | The server this library connects to|

### Optional

| Component          | Purpose                                    |
|--------------------|--------------------------------------------|
| pkg-config         | Simplified compiler/linker flag discovery   |
| Valgrind           | Memory leak checking during development     |
| Doxygen            | Generating HTML API documentation           |
| gcov / lcov        | Code coverage reporting                     |

### Header Dependencies

libbashclient requires only standard C99 and POSIX headers. There are no
external library dependencies. The following system headers are used
internally:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
```

No additional libraries need to be installed.

---

## Quick Start

For the impatient:

```bash
git clone <repository-url>
cd bash-server/clients/c
make
sudo make install
```

Then in your code:

```c
#include <bashclient.h>
```

And compile with:

```bash
gcc -o myapp myapp.c -lbashclient
```

---

## Build from Source

### Obtaining the Source

The libbashclient source is located in the `bash-server/clients/c/` subdirectory of the
bash-server repository:

```
bash-server/clients/c/
  include/
    bashclient.h        Public API header
  src/
    bashclient.c        Core client implementation
    protocol.c          Wire protocol (NDJSON framing, JSON)
    transport.c         Transport layer (socket, stdio, fd, pipe)
    internal.h          Private declarations
  tests/
    test_connect.c      Connection tests
    test_eval.c         Command evaluation tests
    test_state.c        State management tests
    test_observe.c      Observer tests
    test_debug.c        Debugger tests
    test_pty.c          PTY channel tests
    test_memory.c       Memory management tests
  examples/
    eval.c              Basic evaluation example
    observe.c           Command observer example
    debugger.c          Debugger example
    pty.c               PTY terminal example
  Makefile
  libbashclient.pc.in  pkg-config template
```

### Build Targets

| Target             | Description                                    |
|--------------------|------------------------------------------------|
| `make`             | Build shared and static libraries              |
| `make shared`      | Build `libbashclient.so` only                  |
| `make static`      | Build `libbashclient.a` only                   |
| `make check`       | Build and run the test suite                   |
| `make examples`    | Build example programs                         |
| `make install`     | Install libraries, header, and pkg-config file |
| `make uninstall`   | Remove installed files                         |
| `make clean`       | Remove build artifacts                         |
| `make distclean`   | Remove all generated files                     |
| `make coverage`    | Generate code coverage report (requires gcov)  |
| `make docs`        | Generate Doxygen documentation                 |

### Build Options

Set these as environment variables or pass them to make:

| Variable   | Default           | Description                        |
|------------|-------------------|------------------------------------|
| `CC`       | `cc`              | C compiler                         |
| `CFLAGS`   | `-O2 -Wall`       | Compiler flags                     |
| `PREFIX`   | `/usr/local`      | Installation prefix                |
| `LIBDIR`   | `$(PREFIX)/lib`   | Library installation directory     |
| `INCDIR`   | `$(PREFIX)/include` | Header installation directory    |
| `DESTDIR`  | (empty)           | Staging directory for packaging    |
| `DEBUG`    | `0`               | Set to `1` for debug build (-g -O0)|
| `ASAN`     | `0`               | Set to `1` for AddressSanitizer    |

### Debug Build

```bash
make DEBUG=1
```

This enables `-g -O0 -DDEBUG` flags, which include additional assertions
and verbose logging to stderr when the `BC_DEBUG` environment variable is
set.

### AddressSanitizer Build

```bash
make ASAN=1
```

Useful during development to catch buffer overflows, use-after-free, and
other memory errors at runtime.

### Optimized Release Build

```bash
make CFLAGS="-O2 -Wall -Wextra -DNDEBUG"
```

### Running Tests

```bash
make check
```

The test suite requires a running bash-server instance. If no server is
running, the tests start a temporary server automatically. Tests use
`alarm()` timeouts (10 seconds per test) and fork-based isolation.

Expected output on success:

```
test_connect: 5/5 passed
test_eval: 8/8 passed
test_state: 12/12 passed
test_observe: 6/6 passed
test_debug: 9/9 passed
test_pty: 5/5 passed
test_memory: 4/4 passed
All 49 tests passed.
```

---

## Installation

### System-Wide Installation

```bash
sudo make install
```

This installs:
- `/usr/local/lib/libbashclient.so` (shared library)
- `/usr/local/lib/libbashclient.so.0` (SONAME symlink)
- `/usr/local/lib/libbashclient.so.0.1.0` (versioned library)
- `/usr/local/lib/libbashclient.a` (static library)
- `/usr/local/include/bashclient.h` (public header)
- `/usr/local/lib/pkgconfig/libbashclient.pc` (pkg-config file)

After installation, update the shared library cache:

```bash
sudo ldconfig
```

### Custom Prefix

```bash
make PREFIX=/opt/bashclient install
```

### Staging for Packaging

```bash
make DESTDIR=/tmp/stage install
```

This creates the full directory hierarchy under `/tmp/stage/` without
modifying the system.

### Per-User Installation

```bash
make PREFIX=$HOME/.local install
```

Ensure `$HOME/.local/lib` is in your `LD_LIBRARY_PATH` and
`$HOME/.local/include` is in your compiler include path:

```bash
export LD_LIBRARY_PATH=$HOME/.local/lib:$LD_LIBRARY_PATH
export C_INCLUDE_PATH=$HOME/.local/include:$C_INCLUDE_PATH
export PKG_CONFIG_PATH=$HOME/.local/lib/pkgconfig:$PKG_CONFIG_PATH
```

---

## Shared vs Static Linking

### Shared Library (libbashclient.so)

Advantages:
- Smaller executable size
- Library can be updated without recompiling your program
- Memory shared between processes using the same library

Compile and link:

```bash
gcc -o myapp myapp.c -lbashclient
```

At runtime, the dynamic linker must find `libbashclient.so`. Ensure it is
in a directory listed in `/etc/ld.so.conf`, or set `LD_LIBRARY_PATH`.

### Static Library (libbashclient.a)

Advantages:
- No runtime dependency on the shared library
- Simpler deployment (single binary)
- No `LD_LIBRARY_PATH` issues

Compile and link:

```bash
gcc -o myapp myapp.c -Wl,-Bstatic -lbashclient -Wl,-Bdynamic
```

Or link directly:

```bash
gcc -o myapp myapp.c /usr/local/lib/libbashclient.a
```

### Position-Independent Code

The shared library is compiled with `-fPIC`. The static library is also
compiled with `-fPIC` by default so it can be linked into shared libraries.
If you only need the static library for static executables and want
marginally smaller code, rebuild with:

```bash
make static CFLAGS="-O2 -Wall"
```

---

## Platform Notes

### Linux (glibc)

The primary development platform. All features are supported. No special
build steps required.

```bash
make
sudo make install
sudo ldconfig
```

### Linux (musl)

Fully supported. Build with musl-gcc if available:

```bash
make CC=musl-gcc
```

### macOS

Shared library uses `.dylib` extension instead of `.so`. The Makefile
detects macOS automatically. Install with:

```bash
make
sudo make install
```

macOS uses `DYLD_LIBRARY_PATH` instead of `LD_LIBRARY_PATH`:

```bash
export DYLD_LIBRARY_PATH=/usr/local/lib:$DYLD_LIBRARY_PATH
```

Note: macOS does not support `SO_PEERCRED`. The library uses alternative
authentication mechanisms on macOS.

On macOS with System Integrity Protection (SIP), `DYLD_LIBRARY_PATH` is
stripped for system binaries. Install to a standard path or use
`install_name_tool` to set the library path in your executable:

```bash
install_name_tool -add_rpath /usr/local/lib myapp
```

### Cygwin

Fully supported. The shared library is built as `cygbashclient-0.dll`
following Cygwin naming conventions:

```bash
make
make install
```

Cygwin-specific notes:
- Unix sockets work normally under Cygwin
- Windows Named Pipe transport is available via `BC_TRANSPORT_PIPE`
- Link with `-lbashclient` as on Linux
- No `ldconfig` needed; Cygwin searches `PATH` for DLLs

### FreeBSD

Supported. Use `gmake` instead of `make`:

```bash
gmake
sudo gmake install
```

### OpenBSD / NetBSD

Should work but is not regularly tested. Use `gmake` and ensure the
compiler supports C99.

---

## pkg-config Integration

After installation, `pkg-config` can provide the correct compiler and
linker flags:

```bash
pkg-config --cflags libbashclient
# Output: -I/usr/local/include

pkg-config --libs libbashclient
# Output: -L/usr/local/lib -lbashclient

pkg-config --static --libs libbashclient
# Output: -L/usr/local/lib -lbashclient
```

### Using pkg-config in Makefiles

```makefile
CFLAGS += $(shell pkg-config --cflags libbashclient)
LDFLAGS += $(shell pkg-config --libs libbashclient)

myapp: myapp.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
```

### Using pkg-config with CMake

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(BASHCLIENT REQUIRED libbashclient)

target_include_directories(myapp PRIVATE ${BASHCLIENT_INCLUDE_DIRS})
target_link_libraries(myapp ${BASHCLIENT_LIBRARIES})
```

### Using pkg-config with Meson

```meson
bashclient_dep = dependency('libbashclient')
executable('myapp', 'myapp.c', dependencies: [bashclient_dep])
```

---

## Verifying the Installation

### Check the Header

```bash
echo '#include <bashclient.h>' | cc -E -x c - >/dev/null 2>&1 && echo "Header found" || echo "Header NOT found"
```

### Check the Library

```bash
cc -o /dev/null -x c /dev/null -lbashclient 2>/dev/null && echo "Library found" || echo "Library NOT found"
```

### Check pkg-config

```bash
pkg-config --exists libbashclient && echo "pkg-config OK" || echo "pkg-config NOT found"
```

### Compile and Run a Test Program

```c
/* verify.c - Installation verification */
#include <bashclient.h>
#include <stdio.h>

int main(void)
{
    printf("libbashclient version: %s\n", bc_version());
    printf("Header API version: %d\n", BC_API_VERSION);

    /* Attempt a connection to verify runtime functionality.
       This will fail if no server is running, which is fine
       for verification purposes. */
    bc_client_t *c = bc_connect(NULL, NULL);
    if (c) {
        printf("Connected to bash-server successfully.\n");
        bc_close(c);
    } else {
        printf("No server running (expected if bash-server is not started).\n");
        printf("Library loaded and initialized correctly.\n");
    }

    return 0;
}
```

Build and run:

```bash
gcc -o verify verify.c -lbashclient
./verify
```

Expected output (without a running server):

```
libbashclient version: 0.1.0
Header API version: 1
No server running (expected if bash-server is not started).
Library loaded and initialized correctly.
```

### Check Shared Library Dependencies

On Linux:

```bash
ldd $(which myapp) | grep bashclient
# Expected: libbashclient.so.0 => /usr/local/lib/libbashclient.so.0
```

On macOS:

```bash
otool -L myapp | grep bashclient
```

On Cygwin:

```bash
cygcheck myapp | grep bashclient
# Expected: cygbashclient-0.dll
```

---

## Uninstalling

```bash
sudo make uninstall
sudo ldconfig
```

This removes all files installed by `make install`.

---

## Cross-Compilation

### Setting the Cross-Compiler

```bash
make CC=arm-linux-gnueabihf-gcc PREFIX=/opt/arm-sysroot
```

### Cross-Compile for a Different Architecture

```bash
make CC=aarch64-linux-gnu-gcc \
     CFLAGS="-O2 -Wall --sysroot=/opt/aarch64-sysroot" \
     PREFIX=/opt/aarch64-sysroot/usr
```

### Install to Staging Directory

```bash
make DESTDIR=/tmp/cross-stage install
```

Then copy the staging directory contents to the target system.

---

## Troubleshooting Build Issues

### "bashclient.h: No such file or directory"

The header is not in the compiler's include search path. Solutions:

```bash
# Add the include directory explicitly
gcc -I/usr/local/include -o myapp myapp.c -lbashclient

# Or use pkg-config
gcc $(pkg-config --cflags libbashclient) -o myapp myapp.c $(pkg-config --libs libbashclient)
```

### "cannot find -lbashclient"

The library is not in the linker's search path. Solutions:

```bash
# Add the library directory explicitly
gcc -o myapp myapp.c -L/usr/local/lib -lbashclient

# Or update the library cache
sudo ldconfig /usr/local/lib
```

### "error while loading shared libraries"

The runtime linker cannot find the shared library. Solutions:

```bash
# Temporary fix
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

# Permanent fix
echo "/usr/local/lib" | sudo tee /etc/ld.so.conf.d/local.conf
sudo ldconfig
```

### Compiler Warnings

The library is compiled with `-Wall -Wextra` by default. If you see
warnings in the library source code, please report them as bugs.

Your own code may produce warnings when using the library API. Common
resolutions:

```c
/* Warning: unused variable */
bc_eval_result_t *r = bc_eval(c, "echo hello");
(void)r;  /* Suppress warning if intentionally unused */

/* Warning: implicit function declaration */
/* Ensure you have: */
#include <bashclient.h>
```

### Make Errors

If `make` fails with syntax errors, ensure you are using GNU Make:

```bash
make --version
# Should show "GNU Make 3.81" or later

# On BSD systems, use gmake
gmake
```

For further build troubleshooting, see [TROUBLESHOOTING.md](TROUBLESHOOTING.md).
