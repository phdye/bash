# ansi\_strip(3) — strip ANSI escape sequences from byte stream

# SYNOPSIS

    #include "server.h"

    typedef struct ansi_strip_state {
        int state;      /* STRIP_NORMAL, STRIP_ESC, STRIP_CSI, etc. */
        int active;     /* 1 = stripping enabled */
    } ansi_strip_state_t;

    size_t ansi_strip(ansi_strip_state_t *st, const char *in, size_t in_len,
                      char *out, size_t out_size);

# DESCRIPTION

Strips ANSI escape sequences from a byte stream.  The filter is
**stateful**: the `ansi_strip_state_t` structure persists across
calls, allowing correct handling of escape sequences that are split
across `read()` boundaries.

**State machine states:**

| State | Description |
|-------|-------------|
| `STRIP_NORMAL` | Passthrough mode; normal bytes are copied to output |
| `STRIP_ESC` | ESC (0x1B) seen; waiting for sequence type indicator |
| `STRIP_CSI` | CSI sequence (ESC\[...); consuming parameter/intermediate bytes until final byte (0x40-0x7E) |
| `STRIP_OSC` | OSC sequence (ESC\]...); consuming until BEL (0x07) or ST (ESC \\) |
| `STRIP_OSC_ESC` | ESC seen within OSC; checking for ST backslash |
| `STRIP_CHARSET` | Charset designator (ESC( or ESC)); consuming one byte |

**Handled sequence types:**

- **CSI** (Control Sequence Introducer): `ESC [` parameters final-byte
- **8-bit CSI**: Single byte 0x9B followed by parameters and final-byte
- **OSC** (Operating System Command): `ESC ]` ... `BEL` or `ESC \`
- **Two-character escapes**: `ESC` + final byte (0x40-0x7E)
- **Charset designators**: `ESC (` or `ESC )` + one byte

The output is always less than or equal to the input in length, so
**in-place operation is safe**: `out` may alias `in`.

# PARAMETERS

- **st** — Pointer to the strip state.  Must be zero-initialized
  before the first call (use `memset` or aggregate initialization).
  The `active` field must be set to 1 to enable stripping.

- **in** — Input buffer containing raw bytes (may include ANSI
  sequences).

- **in_len** — Number of bytes in the input buffer.

- **out** — Output buffer for clean (stripped) bytes.  May be the
  same pointer as **in** for in-place operation.

- **out_size** — Size of the output buffer in bytes.

# RETURN VALUE

Returns the number of clean bytes written to **out**.  This value is
always in the range \[0, min(in\_len, out\_size)\].

Returns 0 if the entire input consisted of escape sequences.

# SEE ALSO

`pty_handle_spawn`(3)

# NOTES

- The state machine does not validate escape sequence parameters.
  Malformed sequences are silently consumed (bytes are dropped rather
  than passed through).
- The `active` field in the state struct is not checked by
  `ansi_strip()` itself; the caller (PTY relay loop) checks it before
  calling.  If called with `active == 0`, the function still strips
  sequences.
- This function is not static; it is exported for use by test code
  (`test_pty.c`).
