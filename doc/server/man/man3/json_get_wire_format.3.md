# json_get_wire_format(3) -- Return the current wire format for v2 frame I/O

# SYNOPSIS

    #include "server.h"

    int json_get_wire_format(void);

# DESCRIPTION

Returns the current process-global wire format used by `json_frame_read(3)`
and `json_frame_write(3)`.

| Return value  | Constant      | Description                              |
|---------------|---------------|------------------------------------------|
| 0             | `WIRE_BINARY` | 6-byte binary header + payload           |
| 1             | `WIRE_NDJSON` | Newline-delimited JSON with `"ch"` field |

# PARAMETERS

None.

# RETURN VALUE

Returns `WIRE_BINARY` (0) or `WIRE_NDJSON` (1).

# ERRORS

No error conditions.

# EXAMPLES

```c
if (json_get_wire_format() == WIRE_NDJSON) {
    fprintf(stderr, "using NDJSON wire format\n");
} else {
    fprintf(stderr, "using binary wire format\n");
}
```

# SEE ALSO

`json_set_wire_format(3)`, `json_frame_read(3)`,
`json_frame_write(3)`

# NOTES

- The default value (before any call to `json_set_wire_format(3)`) is
  `WIRE_BINARY` (0).

- This function simply reads a process-global static variable.  It is
  effectively free to call.
