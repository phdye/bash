# bash-server API Reference

Man-page-style reference for each protocol operation and supporting functions.

## Protocol Operations

| Page | Section | Description |
|------|---------|-------------|
| [auth.md](auth.md) | 3 | Authenticate a client session |
| [eval.md](eval.md) | 3 | Evaluate a Bash command |
| [ping.md](ping.md) | 3 | Health check / keepalive |
| [quit.md](quit.md) | 3 | Graceful session disconnect |

## Protocol Support Functions

| Page | Section | Description |
|------|---------|-------------|
| [protocol_read_line.md](protocol_read_line.md) | 3 | Read a line from a socket |
| [protocol_write_line.md](protocol_write_line.md) | 3 | Write a formatted line to a socket |
| [protocol_parse_command.md](protocol_parse_command.md) | 3 | Parse a protocol command line |
| [protocol_base64_encode.md](protocol_base64_encode.md) | 3 | Base64 encode binary data |
| [protocol_base64_decode.md](protocol_base64_decode.md) | 3 | Base64 decode a string |
| [protocol_secure_compare.md](protocol_secure_compare.md) | 3 | Constant-time string comparison |

## Socket Functions

| Page | Section | Description |
|------|---------|-------------|
| [server_socket_create.md](server_socket_create.md) | 3 | Create and bind a Unix domain socket |
| [server_socket_close.md](server_socket_close.md) | 3 | Close socket and clean up files |
| [server_accept_client.md](server_accept_client.md) | 3 | Accept a client connection |

## Session Functions

| Page | Section | Description |
|------|---------|-------------|
| [session_init.md](session_init.md) | 3 | Initialize a client session |
| [session_handle.md](session_handle.md) | 3 | Main session command loop |
| [session_cleanup.md](session_cleanup.md) | 3 | Clean up session resources |
| [session_execute_command.md](session_execute_command.md) | 3 | Execute a command (internal) |
