# Contributing to libbashclient

Thank you for your interest in contributing to libbashclient. This
document covers the development setup, coding standards, testing
requirements, and contribution workflow.

---

## Table of Contents

1. [Development Setup](#development-setup)
2. [Building for Development](#building-for-development)
3. [C Coding Style](#c-coding-style)
   - [Formatting Rules](#formatting-rules)
   - [Naming Conventions](#naming-conventions)
   - [Header Organization](#header-organization)
   - [Error Handling Style](#error-handling-style)
   - [Comments](#comments)
4. [Testing](#testing)
   - [Running Tests](#running-tests)
   - [Writing Tests](#writing-tests)
   - [Test Requirements](#test-requirements)
   - [Coverage](#coverage)
5. [Adding New Functions](#adding-new-functions)
6. [ABI Stability](#abi-stability)
7. [Documentation Standards](#documentation-standards)
8. [Commit Messages](#commit-messages)
9. [Pull Request Process](#pull-request-process)
10. [Release Process](#release-process)
11. [License](#license)

---

## Development Setup

### Prerequisites

- GCC >= 4.9 or Clang >= 3.5 (C99 support required)
- GNU Make >= 3.81
- Valgrind (for memory leak testing)
- A running bash-server (for integration tests)

### Clone and Build

```bash
# The library lives inside the bash-server repository
cd clients/c

# Debug build (recommended for development)
make DEBUG=1

# Run tests
make check

# Run tests under Valgrind
make valgrind
```

### Editor Configuration

The project uses 4-space indentation, no tabs, Unix line endings (LF).
Suggested editor settings:

```
indent_style = space
indent_size = 4
end_of_line = lf
charset = utf-8
trim_trailing_whitespace = true
insert_final_newline = true
max_line_length = 80
```

---

## Building for Development

### Debug Build

```bash
make DEBUG=1
```

This enables:
- `-g` debug symbols
- `-O0` no optimization (for accurate debugger stepping)
- `-DDEBUG` enables internal assertions and verbose logging
- No stripping of symbols

### AddressSanitizer Build

```bash
make ASAN=1
```

Catches at runtime:
- Buffer overflows (stack and heap)
- Use-after-free
- Double-free
- Memory leaks (at exit)

### UndefinedBehaviorSanitizer Build

```bash
make CFLAGS="-std=c99 -g -O0 -fsanitize=undefined -Wall -Wextra"
```

### Static Analysis

```bash
# With GCC
make CC=gcc CFLAGS="-std=c99 -fanalyzer -Wall -Wextra"

# With Clang scan-build
scan-build make clean all
```

---

## C Coding Style

### Formatting Rules

The project follows K&R style with 4-space indentation:

```c
/* Function definitions: opening brace on new line */
int bc_eval_something(bc_client_t *c, const char *cmd)
{
    /* Local variable declarations at top of block */
    int rc;
    char *result = NULL;

    /* Validate parameters */
    if (!c || !cmd)
        return set_error(c, BC_ERR_PARAM, "NULL parameter");

    /* Control structures: brace on same line */
    if (condition) {
        do_something();
    } else {
        do_other();
    }

    /* Single-statement bodies: braces optional but consistent */
    for (int i = 0; i < n; i++)
        process(i);

    /* Switch: cases at same indent as switch */
    switch (type) {
    case TYPE_A:
        handle_a();
        break;
    case TYPE_B:
        handle_b();
        break;
    default:
        handle_default();
        break;
    }

    return BC_OK;
}
```

Specific rules:

| Rule                          | Example                          |
|-------------------------------|----------------------------------|
| Indentation                   | 4 spaces, no tabs                |
| Line length                   | 80 columns soft limit            |
| Brace style (functions)       | Opening brace on new line        |
| Brace style (control flow)    | Opening brace on same line       |
| Pointer declaration           | `char *ptr` (star with variable) |
| Cast spacing                  | `(int)value` (no space)          |
| Binary operators              | `a + b` (spaces around)          |
| Unary operators               | `!flag` (no space)               |
| Function calls                | `func(a, b)` (no space before paren) |
| Comma spacing                 | `a, b, c` (space after comma)    |
| Semicolons                    | `for (;;)` (no space before)     |
| Blank lines                   | One between functions, logically grouped blocks |
| Trailing whitespace           | Never                            |
| Final newline                 | Always                           |

### Naming Conventions

| Entity          | Convention          | Example                     |
|-----------------|---------------------|-----------------------------|
| Public functions| `bc_` prefix, snake | `bc_var_get`, `bc_eval`     |
| Public types    | `bc_` prefix, `_t`  | `bc_client_t`, `bc_opts_t`  |
| Public constants| `BC_` prefix, UPPER | `BC_OK`, `BC_ERR_AUTH`      |
| Public macros   | `BC_` prefix, UPPER | `BC_API_VERSION`            |
| Internal funcs  | snake_case, no prefix| `set_error`, `read_frame`  |
| Internal types  | snake_case, `_t`   | `json_builder_t`            |
| Local variables | snake_case          | `socket_path`, `read_len`   |
| Struct members  | snake_case          | `stdout_data`, `exit_code`  |
| Enum values     | `BC_` prefix, UPPER | `BC_CB_PRE_COMMAND`         |
| File-scope vars | `g_` prefix         | `g_last_error`              |

### Header Organization

Public header (`bashclient.h`):

```c
#ifndef BASHCLIENT_H
#define BASHCLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Version macros */
#define BC_API_VERSION 1
#define BC_VERSION_STRING "0.1.0"

/* Error codes */
#define BC_OK            0
#define BC_ERR_AUTH      (-1)
/* ... */

/* Transport constants */
#define BC_TRANSPORT_SOCKET 0
/* ... */

/* Forward declarations */
typedef struct bc_client bc_client_t;

/* Option structs */
typedef struct { /* ... */ } bc_opts_t;
#define BC_OPTS_INIT { /* zero-init */ }

/* Result structs */
typedef struct { /* ... */ } bc_eval_result_t;

/* Callback typedefs */
typedef void (*bc_pre_command_cb)(const bc_pre_command_event_t *, void *);

/* Connection functions */
bc_client_t *bc_connect(const char *socket_path, const char *auth_token);
/* ... */

/* Command functions */
bc_eval_result_t *bc_eval(bc_client_t *c, const char *command);
/* ... */

/* State functions */
/* ... */

/* Observe functions */
/* ... */

/* Debug functions */
/* ... */

/* PTY functions */
/* ... */

/* Utility functions */
void bc_free(void *ptr);
const char *bc_version(void);
/* ... */

#ifdef __cplusplus
}
#endif

#endif /* BASHCLIENT_H */
```

Private header (`internal.h`):

```c
#ifndef BASHCLIENT_INTERNAL_H
#define BASHCLIENT_INTERNAL_H

#include "bashclient.h"

/* Only internal implementation details here */
/* Never include this from user code */

#endif /* BASHCLIENT_INTERNAL_H */
```

### Error Handling Style

Use the `set_error()` helper for all error paths:

```c
/* Good: clear error with message */
if (!cmd)
    return set_error(c, BC_ERR_PARAM, "command is NULL");

/* Good: include system error details */
if (connect(fd, addr, len) < 0)
    return set_error(c, BC_ERR_TRANSPORT,
                     "connect: %s", strerror(errno));

/* Good: include protocol details */
if (channel < 0 || channel > 5)
    return set_error(c, BC_ERR_PROTOCOL,
                     "invalid channel %d", channel);
```

Use goto-based cleanup for functions with multiple resources:

```c
int some_function(bc_client_t *c)
{
    int rc = BC_ERR_NOMEM;
    char *buf = NULL;
    char *result = NULL;

    buf = malloc(1024);
    if (!buf) goto cleanup;

    result = malloc(2048);
    if (!result) goto cleanup;

    /* ... work ... */
    rc = BC_OK;

cleanup:
    free(buf);
    free(result);
    return rc;
}
```

### Comments

```c
/* Single-line comments use C89 style */

/*
 * Multi-line comments use this format.
 * Each line starts with " * ".
 */

/* Function documentation in the header file */

/**
 * bc_eval - Evaluate a shell command.
 *
 * Sends the command to bash-server for execution and blocks until
 * the result is available.
 *
 * @c:       Client handle (must not be NULL)
 * @command: Shell command string (must not be NULL)
 *
 * Returns a heap-allocated result struct on success, or NULL on
 * failure. The caller must free the result with bc_eval_result_free().
 *
 * Errors: BC_ERR_PARAM, BC_ERR_TRANSPORT, BC_ERR_TIMEOUT, BC_ERR_SERVER
 */
bc_eval_result_t *bc_eval(bc_client_t *c, const char *command);
```

Do NOT use `//` C++ style comments. The library targets C99 but maintains
compatibility with C89 comment syntax for consistency.

---

## Testing

### Running Tests

```bash
# Build and run all tests
make check

# Run a specific test binary
make test_eval
./test_eval

# Run under Valgrind
make valgrind

# Run with AddressSanitizer
make check ASAN=1
```

### Writing Tests

Each test is a function returning 0 (pass) or 1 (fail):

```c
#include "test_harness.h"
#include "mock_server.h"
#include <bashclient.h>

static int test_eval_simple(void)
{
    alarm(10);  /* 10-second watchdog */

    mock_server_start("/tmp/test-sock");
    mock_server_expect(1, "eval",
        "{\"type\":\"complete\",\"stdout\":\"hello\\n\","
        "\"stderr\":\"\",\"exit_code\":0}");

    bc_client_t *c = bc_connect("/tmp/test-sock", "test-token");
    ASSERT_NOT_NULL(c);

    bc_eval_result_t *r = bc_eval(c, "echo hello");
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ(r->stdout_data, "hello\n");
    ASSERT_STR_EQ(r->stderr_data, "");
    ASSERT_EQ(r->exit_code, 0);

    bc_eval_result_free(r);
    bc_close(c);
    mock_server_stop();
    return 0;
}

int main(void)
{
    int passed = 0, failed = 0, total = 0;

    printf("test_eval:\n");
    RUN_TEST(test_eval_simple);
    /* ... more tests ... */

    printf("%d/%d passed\n", passed, total);
    return failed > 0 ? 1 : 0;
}
```

### Test Requirements

Every test function MUST:

1. **Set an alarm timeout**: `alarm(10)` at the start. This prevents
   hangs from blocking the entire test suite.

2. **Clean up all resources**: Close client handles, free results, stop
   mock servers. Tests are run under Valgrind, and leaks cause failure.

3. **Use fork isolation for dangerous operations**: If a test might
   crash or corrupt state, run it in a child process:

   ```c
   static int test_double_free_safety(void)
   {
       pid_t child = fork();
       if (child == 0) {
           /* Child: do the dangerous thing */
           bc_eval_result_t *r = bc_eval(c, "echo test");
           bc_eval_result_free(r);
           bc_eval_result_free(r);  /* Double free -- should not crash */
           _exit(0);
       }
       /* Parent: wait with timeout */
       int status;
       for (int i = 0; i < 100; i++) {
           if (waitpid(child, &status, WNOHANG) == child)
               return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;
           usleep(100000);
       }
       kill(child, SIGKILL);
       waitpid(child, NULL, 0);
       return 1;  /* Timed out */
   }
   ```

4. **Test one thing**: Each test function should verify a single behavior.
   Use descriptive names: `test_eval_returns_stderr_on_error`, not
   `test_eval_2`.

### Coverage

Generate a coverage report:

```bash
make coverage
```

This produces an HTML report in `coverage/index.html`. Aim for >= 80%
line coverage on new code. The coverage target for the full library is
85%.

---

## Adding New Functions

To add a public API function:

1. **Declare in `bashclient.h`**: Add the function prototype with
   documentation comment. Place it in the correct channel section.

2. **Implement in `bashclient.c`**: Follow the standard pattern:
   - Validate parameters
   - Build JSON request
   - Send via `proto_write_frame()`
   - Wait for response via `wait_for_response()`
   - Parse response into result struct
   - Return result (or error code)

3. **Write tests**: Add test cases to the appropriate `test_*.c` file.
   Cover success, error, edge cases, and NULL parameter handling.

4. **Update documentation**:
   - Add to `API.md` with full prototype, description, parameters,
     return value, errors, and example
   - Add to `api-metadata.json`
   - Update `GUIDE.md` if the function introduces a new concept

5. **Update `CHANGELOG.md`**: Add the new function under "Added" in
   the unreleased section.

Example checklist for adding `bc_var_list()`:

- [ ] `bashclient.h`: prototype and doc comment
- [ ] `bashclient.c`: implementation
- [ ] `test_state.c`: tests (success, error, empty list, large list)
- [ ] `API.md`: reference entry
- [ ] `api-metadata.json`: function metadata
- [ ] `GUIDE.md`: usage example (if applicable)
- [ ] `CHANGELOG.md`: entry under "Added"

---

## ABI Stability

### Rules

1. **Never remove a public function.** Deprecate it instead by adding a
   doc comment and a compile-time warning attribute.

2. **Never change a function's signature.** If the signature must change,
   add a new function (e.g., `bc_eval2()`) and deprecate the old one.

3. **Never change a struct's layout.** Adding fields to the end is
   allowed only if the struct is always allocated by the library (not
   by the caller). For caller-allocated structs like `bc_opts_t`, new
   fields must be added to the end and zero must be a safe default (so
   that `BC_OPTS_INIT` continues to work).

4. **Never change error code values.** New error codes must use new
   negative integers (e.g., `BC_ERR_NEW = -8`).

5. **Bump `BC_API_VERSION`** when adding new functions or types. This
   allows callers to conditionally compile for different API versions:

   ```c
   #if BC_API_VERSION >= 2
       bc_var_list(c, ...);
   #endif
   ```

6. **Bump the SONAME** (major version) only for incompatible changes
   that cannot be avoided. This should be extremely rare.

### Versioning

The library uses semantic versioning:

- **SONAME**: `libbashclient.so.MAJOR` (e.g., `libbashclient.so.0`)
- **Real name**: `libbashclient.so.MAJOR.MINOR.PATCH`
- **API version**: `BC_API_VERSION` (integer, incremented on additions)

---

## Documentation Standards

### API Reference (API.md)

Every public function must have an entry in `API.md` with:

1. **Prototype** in a code block
2. **Description**: what the function does, when to use it
3. **Parameters**: each parameter with type and description
4. **Return value**: what is returned on success and failure
5. **Errors**: which `BC_ERR_*` codes can be returned
6. **Example**: minimal working code snippet

### Guide (GUIDE.md)

The guide covers how-to topics and recipes. When adding a new feature:

- Add a section in the appropriate channel chapter
- Include a complete, compilable code example
- Show expected output

### Machine-Readable (api-metadata.json)

Keep `api-metadata.json` in sync with `bashclient.h`. This file is used
by documentation generators and language binding generators.

---

## Commit Messages

Follow the conventional format:

```
<type>: <short description>

<optional body>

<optional footer>
```

Types:
- `feat`: New feature or function
- `fix`: Bug fix
- `docs`: Documentation only
- `test`: Test additions or changes
- `refactor`: Code restructuring without behavior change
- `style`: Formatting, whitespace
- `build`: Build system changes

Examples:

```
feat: add bc_var_list() for listing all variables

Implements the STATE channel "list" operation to return all
variable names in the current shell context. Returns a
NULL-terminated array of strings.

test: add coverage for bc_eval with empty command string

fix: prevent double-free in bc_eval_result_free

bc_eval_result_free() now NULLs pointer fields after freeing
them, making accidental double-free calls safe.
```

---

## Pull Request Process

1. **Create a branch**: `feat/var-list` or `fix/double-free`
2. **Write code**: Follow the coding style and add tests
3. **Run checks**: `make check`, `make valgrind`, `make ASAN=1 check`
4. **Update docs**: API.md, GUIDE.md, api-metadata.json, CHANGELOG.md
5. **Open PR**: Describe what and why, link related issues
6. **Address review**: Push fixes as additional commits
7. **Squash merge**: Maintainer squashes into a clean commit

### PR Checklist

- [ ] Code follows the C coding style
- [ ] All new functions have tests
- [ ] `make check` passes
- [ ] `make valgrind` reports no leaks
- [ ] API.md updated for new/changed functions
- [ ] api-metadata.json updated
- [ ] CHANGELOG.md updated
- [ ] No warnings with `-Wall -Wextra -Wpedantic`

---

## Release Process

1. Update `BC_VERSION_STRING` in `bashclient.h`
2. Update `CHANGELOG.md`: move unreleased items to new version section
3. Update `libbashclient.pc.in` version
4. Tag: `git tag v0.2.0`
5. Build release artifacts: `make dist`

---

## License

libbashclient is licensed under the GNU General Public License v3 or
later (GPLv3+). By contributing, you agree that your contributions will
be licensed under the same license.

All source files must include the standard license header:

```c
/*
 * libbashclient - C client library for bash-server
 * Copyright (C) 2025 The Bash Server Authors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
```
