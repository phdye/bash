# GNU Bash 5.1 with bash-server

This is GNU Bash 5.1 (patch level 16) extended with **bash-server**, a
persistent evaluation daemon that embeds the full Bash interpreter as a
shared library.

## Components

| Artifact | Description |
|----------|-------------|
| `bash` | GNU Bash shell |
| `cygbash-5.1.dll` | Bash as a shared library (4.6 MB) |
| `bash-server` | Persistent evaluation daemon |
| `bashclient` | Standalone CLI client |

## Build (Cygwin)

```bash
./configure --enable-bash-server
make -j12
make bash-server
make bashclient
```

## bash-server

`bash-server` exposes the Bash interpreter over Unix sockets, stdio, file
descriptors, or Windows Named Pipes.  Two protocol versions are supported:

- **v1** — Line-oriented text protocol (AUTH, EVAL, PING, QUIT)
- **v2** — Channel-multiplexed JSON frames or NDJSON, with 6 channels:

| Channel | ID | Purpose |
|---------|----|---------|
| CONTROL | 0 | Auth, ping, configure, disconnect |
| COMMAND | 1 | Eval commands, capture stdout/stderr/exit code |
| STATE | 2 | Get/set/unset variables, functions, aliases, traps |
| OBSERVE | 3 | Pre/post command event streaming |
| DEBUG | 4 | Breakpoints, stepping, AST inspection |
| PTY | 5 | Spawn PTY, I/O relay, resize, signal |

Protocol auto-detection selects v1 or v2 from the first byte of each
connection.

### Quick Start

```bash
# Start the server
bash-server --verbose

# In another terminal, connect with bashclient
SOCK=/tmp/bash-server-$(id -u)/sock
TOKEN=$(cat ${SOCK}.token)
bashclient --socket "$SOCK" --auth "$TOKEN" --eval 'echo hello world'
```

### Server Documentation

See [`doc/server/`](doc/server/README.md) for the full documentation suite:
protocol specification, architecture, configuration, security model,
channel reference, and 62 man pages.

## Client Libraries

Native client libraries for the v2 NDJSON protocol in 6 languages:

| Language | Directory | Min Version |
|----------|-----------|-------------|
| Python | `bash-server/clients/python/` | Python 3.8+ |
| TypeScript | `bash-server/clients/typescript/` | Node.js 16+ |
| C | `bash-server/clients/c/` | C99, POSIX |
| C# | `bash-server/clients/csharp/` | .NET 8+ |
| Go | `bash-server/clients/go/` | Go 1.21+ |
| Java | `bash-server/clients/java/` | Java 11+ |

All bindings support 4 transport modes (Unix socket, stdio, fd, Named Pipes)
and all 6 v2 channels.  See [`bash-server/clients/README.md`](bash-server/clients/README.md)
for usage examples and API comparison.

## Test Suites

**Bash tests** — 79 test files with 353 sub-tests:
```bash
cd tests && sh run-all
```

**bash-server tests** — 14 test binaries covering protocol, socket, config,
session, JSON, state, observe, debug, PTY, serialization, transport, and
Named Pipes:
```bash
cd bash-server/tests && make check
```

## Directory Structure

```
├── bash-server/           Persistent evaluation daemon (11 source modules)
│   ├── clients/           Client libraries (Python, TypeScript, C, C#, Go, Java)
│   └── tests/             Server unit tests (14 test binaries)
├── bashclient/            Standalone CLI client
├── builtins/              44 built-in command definitions
├── doc/
│   └── server/            bash-server documentation suite + man pages
├── lib/
│   ├── readline/          Command-line editing
│   ├── sh/                Shell utility library
│   ├── glob/              Pathname expansion
│   └── termcap/           Terminal capabilities
├── tests/                 Bash test suite (79 tests)
├── examples/              Example scripts and loadable builtins
└── support/               Build utilities
```

## Platform

Developed and tested on Cygwin (x86_64).  The `--no-peercred` flag and
`--named-pipe` transport address Cygwin-specific `AF_UNIX` socket quirks.

## License

GNU General Public License v3 or later.  See [COPYING](COPYING).

---

# Original GNU Bash README

Introduction
============

This is GNU Bash, version 5.1.  Bash is the GNU Project's Bourne
Again SHell, a complete implementation of the POSIX shell spec,
but also with interactive command line editing, job control on
architectures that support it, csh-like features such as history
substitution and brace expansion, and a slew of other features.
For more information on the features of Bash that are new to this
type of shell, see the file `doc/bashref.texi'.  There is also a
large Unix-style man page.  The man page is the definitive description
of the shell's features.

See the file POSIX for a discussion of how the Bash defaults differ
from the POSIX spec and a description of the Bash `posix mode'.

There are some user-visible incompatibilities between this version
of Bash and previous widely-distributed versions, bash-4.4 and
bash-5.0.  For details, see the file COMPAT.  The NEWS file tersely
lists features that are new in this release.

Bash is free software, distributed under the terms of the [GNU] General
Public License as published by the Free Software Foundation,
version 3 of the License (or any later version).  For more information,
see the file COPYING.

A number of frequently-asked questions are answered in the file
`doc/FAQ'.

To compile Bash, type `./configure', then `make'.  Bash auto-configures
the build process, so no further intervention should be necessary.  Bash
builds with `gcc' by default if it is available.  If you want to use `cc'
instead, type

	CC=cc ./configure

if you are using a Bourne-style shell.  If you are not, the following
may work:

	env CC=cc ./configure

Read the file INSTALL in this directory for more information about how
to customize and control the build process.  The file NOTES contains
platform-specific installation and configuration information.

If you are a csh user and wish to convert your csh aliases to Bash
aliases, you may wish to use the script `examples/misc/alias-conv.sh'
as a starting point.  The script `examples/misc/cshtobash' is a
more ambitious script that attempts to do a more complete job.

Reporting Bugs
==============

Bug reports for bash should be sent to:

	bug-bash@gnu.org

using the `bashbug' program that is built and installed at the same
time as bash.

The discussion list `bug-bash@gnu.org' often contains information
about new ports of Bash, or discussions of new features or behavior
changes that people would like.  This mailing list is also available
as a usenet newsgroup: gnu.bash.bug.

When you send a bug report, please use the `bashbug' program that is
built at the same time as bash.  If bash fails to build, try building
bashbug directly with `make bashbug'.  If you cannot build `bashbug',
please send mail to bug-bash@gnu.org with the following information:

	* the version number and release status of Bash (e.g., 2.05a-release)
	* the machine and OS that it is running on (you may run
	  `bashversion -l' from the bash build directory for this information)
	* a list of the compilation flags or the contents of `config.h', if
	  appropriate
	* a description of the bug
	* a recipe for recreating the bug reliably
	* a fix for the bug if you have one!

The `bashbug' program includes much of this automatically.

Questions and requests for help with bash and bash programming may be
sent to the help-bash@gnu.org mailing list.

If you would like to contact the Bash maintainers directly, send mail
to bash-maintainers@gnu.org.

While the Bash maintainers do not promise to fix all bugs, we would
like this shell to be the best that we can make it.

Other Packages
==============

This distribution includes, in examples/bash-completion, a recent version
of the `bash-completion' package, which provides programmable completions
for a number of commands. It's available as a package in many distributions,
and that is the first place from which to obtain it. If it's not a package
from your vendor, you may install the included version.

The latest version of bash-completion is always available from
https://github.com/scop/bash-completion.

Enjoy!

Chet Ramey
chet.ramey@case.edu

Copying and distribution of this file, with or without modification,
are permitted in any medium without royalty provided the copyright
notice and this notice are preserved.  This file is offered as-is,
without any warranty.
