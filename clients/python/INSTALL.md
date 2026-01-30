# Installation Guide

## bashclient - Async Python Client for bash-server

**Package**: `bashclient`
**Version**: 0.1.0
**License**: GNU General Public License v3.0 or later

---

## Table of Contents

- [System Requirements](#system-requirements)
- [Quick Install](#quick-install)
- [Installation Methods](#installation-methods)
  - [Install from Source (Editable)](#install-from-source-editable)
  - [Install from Source Distribution](#install-from-source-distribution)
  - [System-Wide Install](#system-wide-install)
- [Development Installation](#development-installation)
- [Virtual Environment Setup](#virtual-environment-setup)
- [Platform Notes](#platform-notes)
  - [Linux](#linux)
  - [macOS](#macos)
  - [Cygwin](#cygwin)
  - [Windows Native](#windows-native)
- [Verification](#verification)
- [pyproject.toml Reference](#pyprojecttoml-reference)
- [Upgrading](#upgrading)
- [Uninstalling](#uninstalling)
- [Troubleshooting Install Issues](#troubleshooting-install-issues)

---

## System Requirements

### Python

- **Minimum**: Python 3.8
- **Recommended**: Python 3.10 or later
- **Required modules**: `asyncio`, `json`, `base64`, `socket`, `subprocess`,
  `dataclasses`, `typing` (all part of the standard library)

bashclient has **zero external dependencies**. It uses only the Python
standard library.

### Operating System Support

| Platform       | Transport: Unix Socket | Transport: stdio | Transport: fd | Transport: Named Pipe |
|----------------|:----------------------:|:-----------------:|:-------------:|:---------------------:|
| Linux          | Yes                    | Yes               | Yes           | No                    |
| macOS          | Yes                    | Yes               | Yes           | No                    |
| Cygwin         | Yes                    | Yes               | Yes           | Yes                   |
| Windows native | No                     | Yes               | No            | Yes (planned)         |

### bash-server

To actually use bashclient, you need a running `bash-server` instance.
See the main project documentation for building and running bash-server.

The client supports bash-server protocol v2 (NDJSON wire format).

---

## Quick Install

If you just want to get started:

```bash
cd /path/to/bash          # Repository root
pip install -e clients/python
```

Verify:

```bash
python -c "import bashclient; print(bashclient.__version__)"
# Output: 0.1.0
```

---

## Installation Methods

### Install from Source (Editable)

This is the recommended method during development. Changes to the source
code take effect immediately without reinstalling.

```bash
# From the bash repository root
pip install -e clients/python
```

Or, if you are inside the Python package directory:

```bash
cd clients/python
pip install -e .
```

The `-e` (editable) flag creates a link from your Python environment to
the source directory. Any modifications to the `.py` files are reflected
immediately.

### Install from Source Distribution

Build a source distribution and install it:

```bash
cd clients/python

# Build the sdist
python -m build --sdist

# Install from the generated archive
pip install dist/bashclient-0.1.0.tar.gz
```

This method copies the files into your site-packages directory. Changes
to the source require rebuilding and reinstalling.

**Note**: Building an sdist requires the `build` package:

```bash
pip install build
```

### System-Wide Install

To install for all users on the system (requires root/admin privileges):

```bash
# Linux / macOS / Cygwin
sudo pip install clients/python

# Or from within the package directory
cd clients/python
sudo pip install .
```

System-wide installation is generally discouraged in favor of virtual
environments. See [Virtual Environment Setup](#virtual-environment-setup).

---

## Development Installation

For development work (running tests, linting, type checking), install
with the `dev` extras:

```bash
cd clients/python
pip install -e ".[dev]"
```

This installs the package in editable mode along with development
dependencies:

| Package         | Purpose                        |
|-----------------|--------------------------------|
| `pytest`        | Test runner                    |
| `pytest-asyncio`| Async test support for pytest  |

After installing dev dependencies, run the test suite:

```bash
# Run all tests
pytest

# Run with verbose output
pytest -v

# Run a specific test file
pytest tests/test_protocol.py

# Run tests matching a pattern
pytest -k "test_encode"
```

### Development Dependencies Detail

**pytest** (>=7.0):

The primary test runner. Tests are discovered automatically from files
matching `test_*.py` in the `tests/` directory.

**pytest-asyncio** (>=0.21):

Enables testing of async functions. The test suite uses
`asyncio_mode="auto"` so async test functions are detected and run
automatically without explicit markers.

Configuration in `pyproject.toml`:

```toml
[tool.pytest.ini_options]
asyncio_mode = "auto"
testpaths = ["tests"]
```

---

## Virtual Environment Setup

Using a virtual environment is strongly recommended to avoid conflicts
with system Python packages.

### Using venv (built-in)

```bash
# Create a virtual environment
python -m venv .venv

# Activate it
# Linux / macOS / Cygwin:
source .venv/bin/activate

# Windows (cmd):
.venv\Scripts\activate.bat

# Windows (PowerShell):
.venv\Scripts\Activate.ps1

# Install bashclient
pip install -e ".[dev]"

# Verify
python -c "import bashclient; print(bashclient.__version__)"

# Deactivate when done
deactivate
```

### Using virtualenv

```bash
# Install virtualenv if needed
pip install virtualenv

# Create environment
virtualenv .venv

# Activate and install (same as above)
source .venv/bin/activate
pip install -e ".[dev]"
```

### Using conda

```bash
# Create environment
conda create -n bashclient python=3.10

# Activate
conda activate bashclient

# Install
pip install -e ".[dev]"
```

---

## Platform Notes

### Linux

Linux is the primary development platform. All features work natively.

**Unix socket transport** (default):

Unix domain sockets (`AF_UNIX`) are fully supported. The default socket
path follows the bash-server resolution order:

1. Explicit path via `connect(path)`
2. `$BASH_SERVER_SOCKET` environment variable
3. `~/.bash-serverrc` configuration file
4. `$XDG_RUNTIME_DIR/bash-server/sock`
5. `/tmp/bash-server-<uid>/sock`

**Permissions**: The socket file must be readable and writable by the
connecting user. bash-server creates sockets with mode `0600` by default.

**SELinux**: If SELinux is enforcing, you may need to configure policies
to allow socket connections between processes. Check `ausearch -m avc`
for denials.

### macOS

macOS support is equivalent to Linux for all practical purposes.

**Unix socket transport**: Fully supported via `AF_UNIX`. Socket path
resolution is identical to Linux.

**Socket path length**: macOS limits Unix socket paths to 104 bytes
(vs 108 on Linux). This is rarely an issue but be aware if using deeply
nested paths.

**Gatekeeper**: If installing from source, macOS may warn about
unverified software. This does not apply to pip-installed packages.

### Cygwin

Cygwin provides a POSIX-compatible environment on Windows with full
Unix socket support.

**Unix socket transport**: Works natively through Cygwin's AF_UNIX
emulation layer. Use Cygwin paths (e.g., `/tmp/bash-server-1000/sock`).

**Named Pipe transport**: Additionally supported on Cygwin for
interoperability with Windows-native processes. Connect with:

```python
client = BashClient()
await client.connect_named_pipe(r"\\.\pipe\bash-server")
```

**Python requirement**: Use the Cygwin-native Python, not Windows Python.
Install via Cygwin setup:

```bash
# Cygwin setup packages
python3
python3-pip
python3-devel   # if building C extensions (not needed for bashclient)
```

**Path translation**: When specifying socket paths, always use POSIX
paths within Cygwin. The transport layer handles any necessary
translation.

### Windows Native

Windows native Python has limited support because `AF_UNIX` is not
universally available.

**AF_UNIX on Windows**: Python 3.9+ on Windows 10 build 17063+ supports
`AF_UNIX` sockets. However, this is not available on older Windows
versions.

**Named Pipe transport**: The recommended transport for Windows native
Python. Requires bash-server to be running with `--named-pipe`.

**stdio transport**: Works on all Windows versions. Launch bash-server
as a subprocess:

```python
client = BashClient()
await client.connect_stdio(["bash-server", "--stdio"])
```

**Limitations**:

- fd transport is not supported (Windows does not use file descriptors
  in the POSIX sense)
- Unix socket transport requires Windows 10 17063+ and Python 3.9+
- Named Pipe transport requires Cygwin bash-server (Windows-native
  bash-server is not yet available)

---

## Verification

After installation, verify that bashclient is properly installed:

### Check version

```bash
python -c "import bashclient; print(bashclient.__version__)"
```

Expected output:

```
0.1.0
```

### Check module imports

```bash
python -c "
from bashclient import BashClient
from bashclient.types import EvalResult, ObserveEvent
from bashclient.errors import BashClientError, AuthenticationError
from bashclient.transport import UnixSocketTransport
from bashclient.protocol import encode_frame, decode_frame
print('All imports successful')
"
```

### Run tests (development install)

```bash
pytest -v
```

### Quick connectivity test

If you have a running bash-server:

```bash
python -c "
import asyncio
from bashclient import BashClient

async def test():
    client = BashClient()
    await client.connect('/tmp/bash-server-\$(id -u)/sock')
    await client.auth(token='YOUR_TOKEN_HERE')
    result = await client.ping()
    print(f'Connected: {result}')
    await client.close()

asyncio.run(test())
"
```

---

## pyproject.toml Reference

The package is configured via `pyproject.toml` in the `clients/python/`
directory. Key sections:

```toml
[build-system]
requires = ["setuptools>=64"]
build-backend = "setuptools.backends._legacy:_Backend"

[project]
name = "bashclient"
version = "0.1.0"
description = "Async Python client for bash-server v2 NDJSON protocol"
readme = "README.md"
license = "GPL-3.0-or-later"
requires-python = ">=3.8"
dependencies = []

[project.optional-dependencies]
dev = [
    "pytest>=7.0",
    "pytest-asyncio>=0.21",
]

[tool.pytest.ini_options]
asyncio_mode = "auto"
testpaths = ["tests"]
```

Key points:

- **No runtime dependencies**: The `dependencies` list is empty.
- **Python 3.8+**: Set via `requires-python`.
- **Dev extras**: pytest and pytest-asyncio for development.
- **License**: GPLv3+ (SPDX: `GPL-3.0-or-later`).

---

## Upgrading

### Editable install

If installed with `-e`, upgrading is automatic. Pull the latest source:

```bash
git pull
# Changes are immediately available
```

### Non-editable install

Reinstall from source:

```bash
pip install --upgrade clients/python
```

Or from an sdist:

```bash
pip install --upgrade dist/bashclient-0.2.0.tar.gz
```

---

## Uninstalling

```bash
pip uninstall bashclient
```

This removes the package from your Python environment. If installed in
editable mode, it removes the link but leaves the source files intact.

---

## Troubleshooting Install Issues

### "No module named 'bashclient'"

**Cause**: The package is not installed in the active Python environment.

**Fix**: Verify you are using the correct Python:

```bash
which python
python -m pip list | grep bashclient
```

If using a virtual environment, ensure it is activated:

```bash
source .venv/bin/activate
```

### "pip: command not found"

**Cause**: pip is not installed or not on PATH.

**Fix**: Install pip:

```bash
python -m ensurepip --upgrade
```

Or use the module directly:

```bash
python -m pip install -e clients/python
```

### "python: command not found" (Cygwin)

**Cause**: Python is not installed in Cygwin.

**Fix**: Install via Cygwin setup or:

```bash
# If python3 exists but python does not
alias python=python3
```

### "error: invalid command 'egg_info'" or setuptools errors

**Cause**: Old version of setuptools.

**Fix**: Upgrade setuptools:

```bash
pip install --upgrade setuptools pip
```

### Permission denied during install

**Cause**: Attempting system-wide install without privileges.

**Fix**: Use `--user` flag or a virtual environment:

```bash
pip install --user -e clients/python
```

### "ModuleNotFoundError: No module named 'setuptools'"

**Cause**: setuptools is not available in the Python environment.

**Fix**:

```bash
pip install setuptools
```

### Tests fail with "No module named 'pytest'"

**Cause**: Dev dependencies not installed.

**Fix**: Install with dev extras:

```bash
pip install -e ".[dev]"
```

### Tests fail with "SyntaxError" on Python 3.7

**Cause**: bashclient requires Python 3.8+. Features used include
`asyncio.run()`, positional-only parameters, and `typing.Protocol`.

**Fix**: Upgrade to Python 3.8 or later.

### Import works but connect() fails

This is not an installation issue but a runtime issue. See
[TROUBLESHOOTING.md](TROUBLESHOOTING.md) for connection problems.
