# Contributing to bash-server Client Libraries

Thank you for your interest in contributing to the bash-server client
libraries. This document covers shared standards, conventions, and
processes that apply across all four language bindings.

For language-specific contribution details, see the CONTRIBUTING.md in
each binding's directory:

- [Python](python/CONTRIBUTING.md)
- [TypeScript](typescript/CONTRIBUTING.md)
- [C](c/CONTRIBUTING.md)
- [Java](java/CONTRIBUTING.md)

---

## Table of Contents

- [Project Structure](#project-structure)
- [Shared Standards](#shared-standards)
- [API Naming Conventions](#api-naming-conventions)
- [Channel Method Matrix](#channel-method-matrix)
- [Adding a New Binding](#adding-a-new-binding)
- [Adding a New Channel Operation](#adding-a-new-channel-operation)
- [Adding a New Transport](#adding-a-new-transport)
- [Error Types](#error-types)
- [Testing Requirements](#testing-requirements)
- [Documentation Requirements](#documentation-requirements)
- [PR Checklist](#pr-checklist)
- [Commit Message Format](#commit-message-format)
- [Release Process](#release-process)
- [Code Review](#code-review)
- [License](#license)

---

## Project Structure

```
clients/
    README.md               Overview and quick comparison
    CONTRIBUTING.md          This file (shared contributor guide)
    COMPARISON.md            Language selection guide
    PROTOCOL.md              v2 NDJSON protocol from client perspective
    TESTING.md               Testing philosophy and practices
    python/
        bashclient/          Package source
            __init__.py
            client.py        BashClient class
            channels.py      Channel method implementations
            protocol.py      NDJSON encode/decode, base64
            transport.py     4 transport backends
            types.py         Dataclass result types
            errors.py        Exception hierarchy
        tests/
            test_protocol.py     Unit tests
            test_channels.py     Channel tests (mock transport)
            test_integration.py  Integration tests (real server)
            conftest.py          Shared fixtures
        examples/            Usage examples
        pyproject.toml       Package metadata + dev deps
        README.md            Python-specific docs
        ARCHITECTURE.md      Internal design details
        INSTALL.md           Installation guide
    typescript/
        src/
            index.ts         Public API re-exports
            client.ts        BashClient class
            channels.ts      Channel implementations
            protocol.ts      NDJSON encode/decode
            transport.ts     Transport backends
            types.ts         TypeScript interfaces
            errors.ts        Error class hierarchy
        test/
            protocol.test.ts
            channels.test.ts
            integration.test.ts
        examples/
        package.json
        tsconfig.json
        jest.config.js
        README.md
    c/
        include/
            bashclient.h     Public API header
        src/
            client.c         Connection lifecycle
            channels.c       Channel operations
            protocol.c       NDJSON encode/decode
            transport.c      Transport backends
            types.c          Type constructors/destructors
            errors.c         Error handling
        tests/
            test_protocol.c
            test_channels.c
            test_integration.c
        examples/
        Makefile
        README.md
    java/
        src/
            main/java/org/gnu/bash/client/
                BashClient.java
                channels/         Channel implementations
                protocol/         NDJSON, base64
                transport/        Transport backends
                types/            Result/event POJOs
                errors/           Exception hierarchy
            test/java/org/gnu/bash/client/
                ProtocolTest.java
                ChannelsTest.java
                IntegrationTest.java
        examples/
        pom.xml
        README.md
```

Each binding follows the same six-module internal architecture:

| Module     | Responsibility                                     |
|------------|----------------------------------------------------|
| errors     | Exception/error hierarchy                          |
| types      | Message and result type definitions                |
| protocol   | NDJSON encode/decode, base64, JSON helpers         |
| transport  | Unix socket, stdio, fd, Named Pipe backends        |
| channels   | Per-channel request/response + server-push logic   |
| client     | Top-level API, message routing, lifecycle          |

---

## Shared Standards

All four bindings implement the same protocol, the same channels, and
the same error types. This uniformity is critical: a user switching
between languages should find the same operations, the same semantics,
and the same error conditions.

### Protocol Compliance

Every binding MUST:

1. Implement the v2 NDJSON wire protocol as specified in
   [PROTOCOL.md](PROTOCOL.md)
2. Support all 6 channels (CONTROL, COMMAND, STATE, OBSERVE, DEBUG, PTY)
3. Support all 4 transports (Unix socket, stdio, fd, Named Pipe)
4. Encode binary data as base64 in JSON string fields
5. Handle server-push messages (observe events, debug events, PTY output)
6. Implement the full error hierarchy (5 error types)
7. Respect the 1MB maximum frame size

### API Parity

All bindings expose equivalent operations. When a new operation is added
to one binding, it MUST be added to all four. The operations may differ
in naming convention and async model, but the semantics must be
identical.

### Semantic Versioning

All bindings share the same version number. A version bump in one
binding means a version bump in all four. This ensures users can
reference a single version across polyglot projects.

---

## API Naming Conventions

Each language follows its idiomatic naming conventions while maintaining
semantic equivalence across all four bindings.

### General Rules

| Aspect           | Python          | TypeScript       | C                | Java             |
|------------------|-----------------|------------------|------------------|------------------|
| **Case style**   | snake_case      | camelCase        | snake_case       | camelCase        |
| **Prefix**       | none            | none             | `bc_`            | none             |
| **Async model**  | async/await     | Promise/await    | blocking         | blocking         |
| **Error model**  | exceptions      | exceptions       | return codes     | exceptions       |
| **Memory**       | GC              | GC               | manual (free)    | GC               |
| **Namespace**    | module/class    | module/class     | prefix           | package/class    |
| **Callbacks**    | sync or async   | sync or async    | function pointer | functional iface |

### Connection Methods

| Operation          | Python                       | TypeScript                   | C                            | Java                         |
|--------------------|------------------------------|------------------------------|------------------------------|------------------------------|
| Unix socket        | `BashClient.connect(path)`   | `BashClient.connect(path)`   | `bc_connect(path)`           | `BashClient.connect(path)`   |
| Subprocess stdio   | `BashClient.connect_stdio()` | `BashClient.connectStdio()`  | `bc_connect_stdio(argv)`     | `BashClient.connectStdio()`  |
| File descriptor    | `BashClient.connect_fd(fd)`  | `BashClient.connectFd(fd)`   | `bc_connect_fd(fd)`          | `BashClient.connectFd(fd)`   |
| Named Pipe         | `BashClient.connect_named_pipe()` | `BashClient.connectNamedPipe()` | `bc_connect_named_pipe()` | `BashClient.connectNamedPipe()` |
| Authenticate       | `client.auth(token)`         | `client.auth(token)`         | `bc_auth(c, token)`          | `client.auth(token)`         |
| Disconnect         | `client.close()`             | `client.close()`             | `bc_close(c)`                | `client.close()`             |
| Ping               | `client.ping()`              | `client.ping()`              | `bc_ping(c, &result)`        | `client.ping()`              |

### Command Channel

| Operation          | Python                       | TypeScript                   | C                              | Java                         |
|--------------------|------------------------------|------------------------------|--------------------------------|------------------------------|
| Eval               | `await client.eval(cmd)`     | `await client.eval(cmd)`     | `bc_eval(c, cmd, &result)`     | `client.eval(cmd)`           |
| Eval parsed AST    | `await client.eval_parsed()` | `await client.evalParsed()`  | `bc_eval_parsed(c, json, &r)`  | `client.evalParsed(json)`    |

### State Channel

| Operation          | Python                           | TypeScript                       | C                                    | Java                             |
|--------------------|----------------------------------|----------------------------------|--------------------------------------|----------------------------------|
| Get variable       | `client.state.get_var(name)`     | `client.state.getVar(name)`      | `bc_state_get_var(c, name, &v)`      | `client.state.getVar(name)`      |
| Set variable       | `client.state.set_var(n, v)`     | `client.state.setVar(n, v)`      | `bc_state_set_var(c, n, v, attr)`    | `client.state.setVar(n, v)`      |
| Unset variable     | `client.state.unset_var(name)`   | `client.state.unsetVar(name)`    | `bc_state_unset_var(c, name)`        | `client.state.unsetVar(name)`    |
| Get function       | `client.state.get_func(name)`    | `client.state.getFunc(name)`     | `bc_state_get_func(c, name, &f)`    | `client.state.getFunc(name)`     |
| Set function       | `client.state.set_func(n, b)`    | `client.state.setFunc(n, b)`     | `bc_state_set_func(c, n, body)`      | `client.state.setFunc(n, b)`     |
| Unset function     | `client.state.unset_func(name)`  | `client.state.unsetFunc(name)`   | `bc_state_unset_func(c, name)`       | `client.state.unsetFunc(name)`   |
| Get alias          | `client.state.get_alias(name)`   | `client.state.getAlias(name)`    | `bc_state_get_alias(c, name, &a)`   | `client.state.getAlias(name)`    |
| Set alias          | `client.state.set_alias(n, v)`   | `client.state.setAlias(n, v)`    | `bc_state_set_alias(c, n, v)`        | `client.state.setAlias(n, v)`    |
| Unset alias        | `client.state.unset_alias(name)` | `client.state.unsetAlias(name)`  | `bc_state_unset_alias(c, name)`      | `client.state.unsetAlias(name)`  |
| Get trap           | `client.state.get_trap(sig)`     | `client.state.getTrap(sig)`      | `bc_state_get_trap(c, sig, &t)`     | `client.state.getTrap(sig)`      |
| Set trap           | `client.state.set_trap(s, act)`  | `client.state.setTrap(s, act)`   | `bc_state_set_trap(c, sig, act)`     | `client.state.setTrap(s, act)`   |
| Unset trap         | `client.state.unset_trap(sig)`   | `client.state.unsetTrap(sig)`    | `bc_state_unset_trap(c, sig)`        | `client.state.unsetTrap(sig)`    |
| Inspect namespace  | `client.state.inspect(ns)`       | `client.state.inspect(ns)`       | `bc_state_inspect(c, ns, &json)`     | `client.state.inspect(ns)`       |

### Observe Channel

| Operation          | Python                           | TypeScript                       | C                                       | Java                             |
|--------------------|----------------------------------|----------------------------------|-----------------------------------------|----------------------------------|
| Subscribe          | `client.observe.subscribe(lvl)`  | `client.observe.subscribe(lvl)`  | `bc_observe_subscribe(c, lvl)`          | `client.observe.subscribe(lvl)`  |
| Unsubscribe        | `client.observe.unsubscribe()`   | `client.observe.unsubscribe()`   | `bc_observe_unsubscribe(c)`             | `client.observe.unsubscribe()`   |
| On pre_command     | `client.observe.on("pre_command", cb)` | `client.observe.on("pre_command", cb)` | `bc_observe_on_pre_command(c, cb, ud)` | `client.observe.on("pre_command", cb)` |
| On post_command    | `client.observe.on("post_command", cb)` | `client.observe.on("post_command", cb)` | `bc_observe_on_post_command(c, cb, ud)` | `client.observe.on("post_command", cb)` |

### Debug Channel

| Operation          | Python                             | TypeScript                         | C                                         | Java                               |
|--------------------|------------------------------------|------------------------------------|-------------------------------------------|--------------------------------------|
| Enable             | `client.debug.enable()`            | `client.debug.enable()`            | `bc_debug_enable(c)`                      | `client.debug.enable()`             |
| Disable            | `client.debug.disable()`           | `client.debug.disable()`           | `bc_debug_disable(c)`                     | `client.debug.disable()`            |
| Add breakpoint     | `client.debug.add_breakpoint()`    | `client.debug.addBreakpoint()`     | `bc_debug_add_breakpoint(c, ...)`         | `client.debug.addBreakpoint()`      |
| Remove breakpoint  | `client.debug.remove_breakpoint()` | `client.debug.removeBreakpoint()`  | `bc_debug_remove_breakpoint(c, id)`       | `client.debug.removeBreakpoint()`   |
| List breakpoints   | `client.debug.list_breakpoints()`  | `client.debug.listBreakpoints()`   | `bc_debug_list_breakpoints(c, &list)`     | `client.debug.listBreakpoints()`    |
| Continue           | `client.debug.continue_()`         | `client.debug.continue_()`         | `bc_debug_continue(c)`                    | `client.debug.continue_()`          |
| Step               | `client.debug.step()`              | `client.debug.step()`              | `bc_debug_step(c)`                        | `client.debug.step()`               |
| Next               | `client.debug.next()`              | `client.debug.next()`              | `bc_debug_next(c)`                        | `client.debug.next()`               |
| Finish             | `client.debug.finish()`            | `client.debug.finish()`            | `bc_debug_finish(c)`                      | `client.debug.finish()`             |
| Skip               | `client.debug.skip()`              | `client.debug.skip()`              | `bc_debug_skip(c)`                        | `client.debug.skip()`               |
| Inspect AST        | `client.debug.inspect_ast()`       | `client.debug.inspectAst()`        | `bc_debug_inspect_ast(c, &json)`          | `client.debug.inspectAst()`         |
| On break_hit       | `client.debug.on("break_hit", cb)` | `client.debug.on("break_hit", cb)` | `bc_debug_on_break_hit(c, cb, ud)`        | `client.debug.on("break_hit", cb)`  |

### PTY Channel

| Operation          | Python                         | TypeScript                     | C                                    | Java                           |
|--------------------|--------------------------------|--------------------------------|--------------------------------------|--------------------------------|
| Spawn              | `client.pty.spawn(**kw)`       | `client.pty.spawn(opts)`       | `bc_pty_spawn(c, r, c, sh, sa, &i)` | `client.pty.spawn(r, c, ...)`  |
| Write input        | `client.pty.write_input(data)` | `client.pty.writeInput(data)`  | `bc_pty_write(c, data, len)`         | `client.pty.writeInput(data)`  |
| Resize             | `client.pty.resize(r, c)`     | `client.pty.resize(r, c)`     | `bc_pty_resize(c, rows, cols)`       | `client.pty.resize(r, c)`     |
| Signal             | `client.pty.signal(sig)`       | `client.pty.signal(sig)`       | `bc_pty_signal(c, name)`             | `client.pty.signal(sig)`       |
| Close              | `client.pty.close()`           | `client.pty.close()`           | `bc_pty_close(c)`                    | `client.pty.close()`           |
| On output          | `client.pty.on("output", cb)`  | `client.pty.on("output", cb)`  | `bc_pty_on_output(c, cb, ud)`        | `client.pty.on("output", cb)`  |
| On exit            | `client.pty.on("exit", cb)`    | `client.pty.on("exit", cb)`    | `bc_pty_on_exit(c, cb, ud)`          | `client.pty.on("exit", cb)`    |

---

## Adding a New Binding

If you want to add a fifth language binding (e.g., Rust, Go, Ruby),
follow this process.

### Minimum Requirements

1. **Protocol compliance**: Implement the full v2 NDJSON wire protocol
   as specified in [PROTOCOL.md](PROTOCOL.md). This is non-negotiable.

2. **All 6 channels**: Every channel must be supported with full
   operation coverage matching the existing bindings.

3. **All 4 transports**: Unix socket, stdio, fd, Named Pipe.

4. **Error hierarchy**: Implement the 5 standard error types
   (AuthError, ProtocolError, TimeoutError, TransportError, ServerError).

5. **Test coverage**: All three test tiers (unit, channel, integration).
   See [TESTING.md](TESTING.md) for details.

6. **Documentation**: README.md, examples, inline API docs.

7. **Idiomatic API**: Follow the target language's conventions for
   naming, error handling, async patterns, and memory management. Do NOT
   port another binding line-for-line.

### Directory Structure

Create `clients/<language>/` with:

```
clients/<language>/
    README.md               Quick start, API overview, installation
    examples/               At least 3 working examples
    <source>/               Source organized by module
    <tests>/                All 3 test tiers
    <build-config>          Language-appropriate build file
```

### Checklist for New Bindings

- [ ] All 6 channels implemented
- [ ] All 4 transports implemented
- [ ] All 5 error types implemented
- [ ] Unit tests for protocol layer (no server needed)
- [ ] Channel tests with mock transport
- [ ] Integration tests with `bash-server --stdio`
- [ ] README.md with quick start and API overview
- [ ] At least 3 working examples
- [ ] Added to the top-level clients/README.md matrix
- [ ] Added to COMPARISON.md
- [ ] CI configuration added
- [ ] License header in all source files

---

## Adding a New Channel Operation

When bash-server adds a new operation to an existing channel (e.g., a
new STATE operation or a new DEBUG command), the change must be
propagated to all four bindings.

### Process

1. **Understand the protocol**: Read the server-side implementation and
   the updated wire protocol docs. Determine:
   - Which channel (0-5)?
   - Request-response or server-push?
   - What fields are in the request and response messages?
   - Any binary data requiring base64?

2. **Update PROTOCOL.md**: Add the new message types to the protocol
   specification with wire examples.

3. **Implement in all 4 bindings**: Each binding needs:
   - New method(s) in the channels module
   - New type(s) in the types module (if new response shape)
   - Updated public API (re-exports, etc.)

4. **Add tests in all 4 bindings**: Each binding needs:
   - Unit test for message construction
   - Channel test with mock transport
   - Integration test (if server is available)

5. **Update documentation**: Each binding's README needs the new
   operation listed.

### Naming the New Operation

Follow each language's naming convention:

| If the operation is called `foo_bar` in the protocol... |
|---------------------------------------------------------|
| Python: `client.channel.foo_bar()`                      |
| TypeScript: `client.channel.fooBar()`                   |
| C: `bc_channel_foo_bar(c, ...)`                         |
| Java: `client.channel.fooBar()`                         |

### PR Requirements for New Operations

A PR that adds a new channel operation MUST include changes to all 4
bindings. PRs that add an operation to only one binding will not be
merged (exception: the initial implementation PR for a new binding).

---

## Adding a New Transport

When bash-server adds a new transport mode, all four bindings must be
updated to support it.

### Process

1. **Understand the transport**: How does the connection open? What I/O
   primitives does it use? Is it platform-specific?

2. **Update PROTOCOL.md**: Document the new transport's connection
   semantics.

3. **Implement the transport backend** in each binding's transport
   module. The new transport must implement the same interface as
   existing transports (read_line, write, close, is_connected).

4. **Add a factory method** to BashClient in each binding
   (`connect_<transport>` / `connectTransport` / `bc_connect_<transport>`).

5. **Add tests**: Unit tests for the transport and integration tests
   using the new transport mode.

6. **Update README.md** in each binding with the new connect method.

---

## Error Types

All bindings implement the same five error types with equivalent
semantics:

| Error Type       | Meaning                                   | When Raised                                    |
|------------------|-------------------------------------------|------------------------------------------------|
| `AuthError`      | Authentication failed                     | Wrong token, auth before connect, re-auth      |
| `ProtocolError`  | Wire protocol violation                   | Malformed frame, unknown channel, bad JSON     |
| `TimeoutError`   | Operation timed out                       | No response within deadline                    |
| `TransportError` | Connection/IO failure                     | Socket closed, write failed, connect refused   |
| `ServerError`    | Server returned an error response         | Server sent `{"type":"error",...}`              |

### Language-Specific Error Patterns

**Python**: Exception classes inheriting from `BashClientError`:

```python
class BashClientError(Exception): pass
class AuthError(BashClientError): pass
class ProtocolError(BashClientError): pass
class TimeoutError(BashClientError): pass
class TransportError(BashClientError): pass
class ServerError(BashClientError): pass
```

**TypeScript**: Error classes extending `BashClientError`:

```typescript
export class BashClientError extends Error { }
export class AuthError extends BashClientError { }
export class ProtocolError extends BashClientError { }
export class TimeoutError extends BashClientError { }
export class TransportError extends BashClientError { }
export class ServerError extends BashClientError { }
```

**C**: Return codes plus `bc_error()` for message:

```c
#define BC_OK             0
#define BC_ERR_AUTH      -1
#define BC_ERR_PROTOCOL  -2
#define BC_ERR_TIMEOUT   -3
#define BC_ERR_TRANSPORT -4
#define BC_ERR_SERVER    -5

const char *bc_error(bc_client_t *c);  /* Last error message */
```

**Java**: Exception classes extending `BashClientException`:

```java
public class BashClientException extends Exception { }
public class AuthException extends BashClientException { }
public class ProtocolException extends BashClientException { }
public class TimeoutException extends BashClientException { }
public class TransportException extends BashClientException { }
public class ServerException extends BashClientException { }
```

---

## Testing Requirements

See [TESTING.md](TESTING.md) for the full testing guide. This section
summarizes the requirements for contributors.

### Three Test Tiers

Every binding must have tests at all three levels:

1. **Unit tests**: Test protocol encoding/decoding, base64, JSON helpers,
   message construction. No server, no I/O. Pure logic tests.

2. **Channel tests**: Test channel methods using a mock transport.
   Pre-load response sequences, verify request frames, verify parsed
   results, verify error handling.

3. **Integration tests**: Test against a real `bash-server --stdio`
   instance. Full round-trip from connect to close. Skipped if
   bash-server is not available.

### Coverage Targets

| Test Tier     | Minimum Coverage | What to Cover                           |
|---------------|------------------|-----------------------------------------|
| Unit          | 100% of protocol | Every encode/decode function, edge cases |
| Channel       | 100% of methods  | Every channel method, error paths        |
| Integration   | Core paths       | Connect, auth, eval, state get/set       |

### Running Tests

```bash
# Python
cd clients/python
pip install -e ".[dev]"
pytest

# TypeScript
cd clients/typescript
npm install
npm test

# C
cd clients/c
make check

# Java
cd clients/java
mvn test
```

### Test Naming Conventions

| Language   | Pattern                              | Example                         |
|------------|--------------------------------------|---------------------------------|
| Python     | `test_<module>_<operation>`          | `test_protocol_encode_frame`    |
| TypeScript | `describe/it` blocks                | `describe("protocol")` / `it("encodes frame")` |
| C          | `test_<module>_<operation>` function | `test_protocol_encode_frame`    |
| Java       | `@Test` method                       | `testProtocolEncodeFrame()`     |

---

## Documentation Requirements

When making changes, update the following documentation:

### Per-Binding Documentation

Each binding's README.md must include:

- Installation instructions
- Quick start example
- API overview (all channels)
- Transport configuration
- Error handling examples
- Development setup

### Shared Documentation

| File              | Update When...                              |
|-------------------|---------------------------------------------|
| `README.md`       | New binding, new channel, new transport     |
| `CONTRIBUTING.md`  | Process changes, new conventions            |
| `COMPARISON.md`    | New binding, API changes, new features      |
| `PROTOCOL.md`      | New message types, protocol changes         |
| `TESTING.md`       | New test patterns, infrastructure changes   |

### Example Programs

Each binding should have at minimum these examples:

1. **Basic eval**: Connect, auth, eval, print result
2. **State operations**: Get/set variables, functions, aliases
3. **Observe events**: Subscribe, print pre/post command events

Additional examples for debug and PTY are encouraged but not required.

### Inline Documentation

- **Python**: Docstrings on all public classes, methods, and functions.
  Follow Google-style docstrings.
- **TypeScript**: JSDoc comments on all exported declarations.
  Include `@param`, `@returns`, `@throws` tags.
- **C**: Doxygen-style comments in the public header (`bashclient.h`).
  Document every function, struct, typedef, and macro.
- **Java**: Javadoc on all public classes, methods, and constructors.
  Include `@param`, `@return`, `@throws` tags.

---

## PR Checklist

Use this checklist for every pull request. Copy it into your PR
description and check off each item.

### All PRs

- [ ] Code follows the language's style conventions
- [ ] No compiler warnings or linter errors
- [ ] All existing tests pass
- [ ] New code has test coverage at all applicable tiers
- [ ] Documentation is updated (README, examples, inline docs)
- [ ] Commit messages follow the format below

### New Channel Operation PRs

- [ ] Implemented in **all 4 bindings**
- [ ] Tests added in **all 4 bindings**
- [ ] PROTOCOL.md updated with wire examples
- [ ] README.md updated in each binding
- [ ] clients/README.md channel table updated

### New Transport PRs

- [ ] Implemented in **all 4 bindings**
- [ ] Factory method added to BashClient in each binding
- [ ] Tests added in **all 4 bindings**
- [ ] PROTOCOL.md updated
- [ ] README.md updated in each binding
- [ ] clients/README.md transport table updated

### New Binding PRs

- [ ] All 6 channels implemented
- [ ] All 4 transports implemented
- [ ] All 5 error types implemented
- [ ] Three tiers of tests (unit, channel, integration)
- [ ] README.md with quick start and full API overview
- [ ] At least 3 working examples
- [ ] clients/README.md updated with new row
- [ ] COMPARISON.md updated with new column
- [ ] TESTING.md updated with new language section
- [ ] Build/package configuration (Makefile, pom.xml, etc.)
- [ ] CI configuration added
- [ ] License headers in all source files

### Bug Fix PRs

- [ ] Failing test that reproduces the bug (added BEFORE the fix)
- [ ] Fix applied
- [ ] Test now passes
- [ ] Same bug checked in other bindings (fix all if applicable)

---

## Commit Message Format

Use conventional commit format with the binding name as scope:

```
<type>(<scope>): <description>

[optional body]
```

### Types

| Type       | When                                        |
|------------|---------------------------------------------|
| `feat`     | New feature or operation                    |
| `fix`      | Bug fix                                     |
| `test`     | Adding or fixing tests                      |
| `docs`     | Documentation changes                       |
| `refactor` | Code restructuring without behavior change  |
| `chore`    | Build, CI, tooling changes                  |

### Scopes

| Scope         | Meaning                             |
|---------------|-------------------------------------|
| `python`      | Python binding changes              |
| `typescript`  | TypeScript binding changes          |
| `c`           | C binding changes                   |
| `java`        | Java binding changes                |
| `clients`     | Shared docs, cross-binding changes  |
| `protocol`    | Protocol specification changes      |

### Examples

```
feat(python): add inspect_ast method to debug channel

Implements CHAN_DEBUG inspect_ast operation for the Python binding.
Returns the COMMAND tree JSON for the current execution context.
```

```
fix(c): fix buffer overflow in base64 decode

The output buffer was sized for the encoded length, not the decoded
length. For payloads > 768 bytes, this caused a heap overflow.
Fixes #42.
```

```
feat(clients): add PTY channel to all 4 bindings

Implements CHAN_PTY (channel 5) across Python, TypeScript, C, and Java.
Supports spawn, write, resize, signal, close, and output/exit callbacks.
```

```
test(java): add channel tests for state operations

Adds mock transport tests for all 11 state operations (get/set/unset
for vars, funcs, aliases, traps, plus inspect).
```

---

## Release Process

### Version Bumping

All four bindings share the same version number. When releasing:

1. Update version in all binding metadata files:
   - `python/pyproject.toml` (`version = "X.Y.Z"`)
   - `python/bashclient/__init__.py` (`__version__ = "X.Y.Z"`)
   - `typescript/package.json` (`"version": "X.Y.Z"`)
   - `c/Makefile` (`VERSION = X.Y.Z`)
   - `java/pom.xml` (`<version>X.Y.Z</version>`)

2. Update `clients/README.md` if any table content changed.

3. Create a single commit: `chore(clients): release vX.Y.Z`

4. Tag the commit: `clients-vX.Y.Z`

### Version Policy

- **Major**: Breaking API change in any binding
- **Minor**: New operations, new transports, new bindings
- **Patch**: Bug fixes, documentation, internal refactoring

### Publishing

| Binding      | Registry          | Command                   |
|--------------|-------------------|---------------------------|
| Python       | PyPI              | `python -m build && twine upload dist/*` |
| TypeScript   | npm               | `npm publish`             |
| C            | Source tarball     | `make dist`               |
| Java         | Maven Central     | `mvn deploy`              |

---

## Code Review

### What Reviewers Check

1. **Protocol correctness**: Does the new code follow the wire protocol
   specification exactly? Compare against PROTOCOL.md.

2. **Cross-binding parity**: If this adds an operation, are all 4
   bindings updated? Do the semantics match?

3. **Test coverage**: Are all three test tiers present? Do tests cover
   error paths, not just happy paths?

4. **Naming conventions**: Does the code follow the target language's
   idioms and this guide's naming table?

5. **Documentation**: Is the README updated? Are inline docs present?
   Are examples correct?

6. **Memory safety (C)**: Are all allocations freed? Are buffer sizes
   correct? Are strings null-terminated?

7. **Thread safety (Java)**: Is shared state properly synchronized?
   Are callbacks invoked on the correct thread?

8. **Error handling**: Are all error paths covered? Do errors propagate
   correctly through the stack?

### Review Turnaround

Aim for initial review within 48 hours. Cross-binding PRs may take
longer as they need reviewers familiar with multiple languages.

---

## License

All code in the `clients/` directory is licensed under the
**GNU General Public License v3 or later** (GPLv3+).

Every source file must include a license header:

```
Copyright (C) <year> Free Software Foundation, Inc.

This file is part of bash-server.

bash-server is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

bash-server is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with bash-server.  If not, see <https://www.gnu.org/licenses/>.
```

Contributions are accepted under the same license. By submitting a
pull request, you agree that your contribution is licensed under GPLv3+.
