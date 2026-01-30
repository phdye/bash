# bash-serverrc(5) — bash-server configuration file

# DESCRIPTION

The **bash-serverrc** file provides configuration directives for
**bash-server**(1). It is read at startup before the server begins
accepting connections.

The default location is **~/.bash-serverrc**. An alternate path may be
specified with the **--config** command-line option.

The file is processed line by line in order. Blank lines and lines
whose first non-whitespace character is **#** are ignored. For
directives that accept a single value, the first matching directive
wins; subsequent duplicates are silently ignored.

# FORMAT

Each directive occupies one line and has the form:

```
DIRECTIVE VALUE
```

Leading and trailing whitespace is stripped. The directive keyword is
case-sensitive.

# Directives

**socket** *PATH*
: Set the Unix domain socket path the server listens on. *PATH* is an
  absolute filesystem path. The parent directory must exist and be
  writable by the server process.

  If this directive is absent, the socket path is resolved through the
  following fallback chain (first match wins):

  1. **--socket** command-line option
  2. **$BASH_SERVER_SOCKET** environment variable
  3. **socket** directive in **~/.bash-serverrc**
  4. **$XDG_RUNTIME_DIR/bash-server/sock**
  5. **/tmp/bash-server-***\<uid\>***/sock**

Future versions may introduce additional directives. Unrecognized
directives produce a warning on stderr but do not prevent startup.

# EXAMPLES

Minimal configuration setting only the socket path:

```
# ~/.bash-serverrc — bash-server configuration
socket /run/user/1000/bash-server/sock
```

Configuration with comments and blank lines:

```
# bash-server config
# Written 2025-04-10

# Listen on a custom socket path
socket /home/alice/.local/bash-server/sock

# Future directives will go here
```

Because first-match wins, only the first **socket** line takes effect:

```
socket /tmp/primary.sock
socket /tmp/secondary.sock    # ignored
```

# FILES

**~/.bash-serverrc**
: Default configuration file location.

# SEE ALSO

**bash-server**(1),
**bash-server-token**(5),
**bash-server-v1-protocol**(5),
**bash-server-v2-protocol**(5)

# AUTHORS

GNU Bash is written by Brian Fox and Chet Ramey. The bash-server
extension and this documentation were developed as part of the
Cygwin bash-server project.

# COPYRIGHT

Copyright (C) 2024-2025 Free Software Foundation, Inc. License GPLv3+:
GNU GPL version 3 or later <https://www.gnu.org/licenses/gpl.html>.
This is free software; you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.
