# AUTH(3) — Authenticate Client Session

## NAME

AUTH — authenticate a client session with a shared secret token

## SYNOPSIS

```
AUTH <token>
```

## DESCRIPTION

The **AUTH** command authenticates the client's session using a 64-character
hexadecimal token.  The token must match the server's auto-generated token,
which is stored in `<socket-path>.token` at server startup.

Authentication is required before any **EVAL** commands can be executed.
**PING** and **QUIT** are available without authentication.

On the first successful authentication, the server initializes the embedded
Bash interpreter by calling the following functions in order:

1. `initialize_shell_builtins()`
2. `initialize_traps()`
3. `initialize_signals(0)`
4. `tilde_initialize()`
5. `initialize_shell_variables(shell_environment, 0)`
6. `initialize_job_control(0)`
7. `initialize_bash_input()`
8. `initialize_flags()`
9. `initialize_shell_options(0)`
10. `initialize_bashopts(0)`

The shell is configured as non-interactive (`interactive_shell = 0`) and
non-login (`login_shell = 0`), with `shell_name` set to `"bash-server"`.

Bash initialization is performed once per server lifetime and is guarded
by a static `bash_initialized` flag.

## ARGUMENTS

**token**
:   A 64-character lowercase hexadecimal string.  Generated from 32 bytes
    of `/dev/urandom` entropy.  Read from `<socket-path>.token`.

## RESPONSES

**`OK`**
:   Authentication successful.  The session is now authenticated.

**`OK already authenticated`**
:   The session was already authenticated.  No state change.  This makes
    AUTH idempotent — clients may safely send AUTH multiple times.

**`ERR token required`**
:   The AUTH command was sent without a token argument.

**`ERR invalid token`**
:   The provided token does not match the server's token.

## SECURITY

- Token comparison uses `protocol_secure_compare()`, a constant-time
  algorithm that prevents timing side-channel attacks.
- The token is transmitted in plaintext.  Security relies on Unix socket
  file permissions (mode `0600`).
- There is no rate limiting on failed authentication attempts.
- See [security.md](../security.md) for the complete threat model.

## EXAMPLES

Successful authentication:
```
→ AUTH a1b2c3d4e5f6...  (64 hex chars)
← OK
```

Already authenticated:
```
→ AUTH a1b2c3d4e5f6...
← OK already authenticated
```

Failed authentication:
```
→ AUTH 0000000000000000000000000000000000000000000000000000000000000000
← ERR invalid token
```

Missing token:
```
→ AUTH
← ERR token required
```

## SOURCE

- Handler: `handle_auth()` in `server_session.c:124`
- Token generation: `generate_auth_token()` in `server_main.c:422`
- Comparison: `protocol_secure_compare()` in `server_protocol.c:250`

## SEE ALSO

[eval.md](eval.md), [quit.md](quit.md), [protocol_secure_compare.md](protocol_secure_compare.md),
[security.md](../security.md)
