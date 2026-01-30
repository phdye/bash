# Cygwin Patches for Bash 5.1

This directory contains patches adapted from the Cygwin bash 5.2.21-1 source package
for use with this bash 5.1 repository.

## Source
Original patches from: `/usr/src/bash-5.2.21-1.src/`
These patches originated from Fedora and were adapted by Cygwin maintainers.

## Patch Categories

### Build System Fixes
| Patch | File(s) | Description |
|-------|---------|-------------|
| `00-fix-aclocal-unistd.patch` | `aclocal.m4` | Fix implicit function declarations in configure tests (GCC 14+ compatibility) |

### Config-top.h Feature Enablement
These patches enable optional bash features via config-top.h defines:

| Patch | Feature | Description |
|-------|---------|-------------|
| `01-profile.patch` | `NON_INTERACTIVE_LOGIN_SHELLS` | Makes login shells source profile files even when non-interactive |
| `02-ssh-source-bash.patch` | `SSH_SOURCE_BASHRC` | Sources ~/.bashrc for SSH connections |
| `03-broken-pipe.patch` | `DONT_REPORT_BROKEN_PIPE_WRITE_ERRORS` | Suppresses SIGPIPE write error messages |
| `04-logout.patch` | `SYS_BASH_LOGOUT` | Enables system-wide /etc/bash.bash_logout |

### Bug Fixes
| Patch | File(s) | Description |
|-------|---------|-------------|
| `10-pgrp-sync.patch` | `configure` | Forces pgrp_pipe=yes for cross-compilation (template) |
| `11-memleak-lc-all.patch` | `locale.c` | Fixes memory leak when LC_ALL is set |
| `12-noecho.patch` | `parse.y`, `subst.c` | Fixes echo_input_at_read in command substitution |
| `13-size-type.patch` | `variables.h` | Changes `int` to `size_t` in VARLIST struct |
| `14-setlocale.patch` | `builtins/setattr.def` | Fixes setattr for special variables |

### Documentation
| Patch | File(s) | Description |
|-------|---------|-------------|
| `20-manpage-trap.patch` | `doc/bash.1` | Clarifies trap command behavior |
| `21-man-ulimit.patch` | `doc/bash.1` | Documents POSIX mode ulimit behavior |
| `22-syslog-history.patch` | `doc/bash.1` | Documents syslog_history option |

### Build/Debug
| Patch | File(s) | Description |
|-------|---------|-------------|
| `30-interpreter.patch` | `execute_cmd.c`, `config.h.in` | Better ELF interpreter error handling (template) |
| `31-debuginfo.patch` | `builtins/Makefile.in` | Keeps .c files for debugging |

### Cygwin-Specific
| Patch | File(s) | Description |
|-------|---------|-------------|
| `50-cygwin.patch` | Documentation | Summary and application notes |
| `50-cygwin-full.patch` | Multiple | Full Cygwin patch (from bash 5.2, needs adaptation) |

## Excluded Patches (in DISABLE/)

### `no-loadable-builtins.patch`
**NOT APPLIED** - Disables loadable builtins installation.
Excluded because enabling loadable modules support is a goal of this repository.

## Application Order

Apply patches in numerical order (00, 01, 02... 10, 11... 20, 21... etc.)

```bash
cd /path/to/bash
for patch in cygwin-patches/[0-4]*.patch; do
    patch -p1 < "$patch"
done
# Regenerate configure after 00-fix-aclocal-unistd.patch
autoconf --force
```

## Testing

After applying patches, build and test:
```bash
./configure
make -j12
make check  # run test suite
```
