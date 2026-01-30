# Contributing to bashclient

Thank you for your interest in contributing to the bashclient Python
package. This document covers development setup, code conventions,
testing practices, and the process for submitting changes.

---

## Table of Contents

- [Development Setup](#development-setup)
- [Project Structure](#project-structure)
- [Code Style](#code-style)
  - [Python Conventions](#python-conventions)
  - [Type Hints](#type-hints)
  - [Docstrings](#docstrings)
  - [Naming Conventions](#naming-conventions)
- [Testing](#testing)
  - [Running Tests](#running-tests)
  - [Test Organization](#test-organization)
  - [Writing Tests](#writing-tests)
  - [Async Tests](#async-tests)
  - [Mock Transport](#mock-transport)
  - [Integration Tests](#integration-tests)
- [Common Development Tasks](#common-development-tasks)
  - [Adding a New Channel Method](#adding-a-new-channel-method)
  - [Adding a New Transport](#adding-a-new-transport)
  - [Adding a New Type](#adding-a-new-type)
  - [Adding a New Exception](#adding-a-new-exception)
- [Documentation](#documentation)
- [Commit Conventions](#commit-conventions)
- [Pull Request Process](#pull-request-process)
- [License](#license)

---

## Development Setup

### Prerequisites

- Python 3.8 or later
- pip (usually included with Python)
- git
- bash-server binary (for integration tests only)

### Clone and Install

```bash
# Clone the repository (if you haven't already)
git clone <repository-url>
cd bash

# Create a virtual environment
python -m venv .venv
source .venv/bin/activate    # Linux/macOS/Cygwin

# Install in editable mode with dev dependencies
cd clients/python
pip install -e ".[dev]"

# Verify the installation
python -c "import bashclient; print(bashclient.__version__)"
pytest --co  # list all collected tests without running them
```

### Editor Setup

The project uses standard Python tooling. Recommended editor
configuration:

- Enable Python type checking (mypy or pyright)
- Set line length to 79 characters (PEP 8)
- Use 4-space indentation (no tabs)
- Configure auto-format on save (optional, but use black-compatible
  formatting if you do)

---

## Project Structure

```
clients/python/
    bashclient/             # Package source
        __init__.py         # Public API, __version__, __all__
        client.py           # BashClient class
        channels.py         # Channel method implementations
        protocol.py         # NDJSON wire protocol
        transport.py        # Transport backends
        types.py            # Dataclass types
        errors.py           # Exception hierarchy
    tests/                  # Test suite
        __init__.py
        conftest.py         # Shared fixtures
        test_protocol.py    # Protocol unit tests
        test_channels.py    # Channel tests (mock transport)
        test_integration.py # Integration tests (real server)
    examples/               # Example scripts
        eval.py
        observe.py
        debugger.py
        pty.py
    pyproject.toml          # Package metadata and config
    LICENSE                 # GPLv3+ license text
    README.md               # Package overview
    API.md                  # API reference
    GUIDE.md                # User guide
    INSTALL.md              # Installation guide
    ARCHITECTURE.md         # Internal architecture
    CONTRIBUTING.md         # This file
    TROUBLESHOOTING.md      # Troubleshooting guide
    CHANGELOG.md            # Version history
    api-metadata.json       # Machine-readable API metadata
```

---

## Code Style

### Python Conventions

Follow PEP 8 with these specifics:

- **Line length**: 79 characters for code, 72 for docstrings and
  comments.
- **Indentation**: 4 spaces. No tabs.
- **Imports**: Group in order: standard library, third-party, local.
  Separate groups with a blank line. Use absolute imports within
  the package.
- **String quotes**: Use double quotes for user-facing strings and
  docstrings. Single quotes are acceptable for internal strings
  and dict keys.
- **Trailing commas**: Use trailing commas in multi-line collections
  and function signatures.
- **f-strings**: Preferred for string formatting (Python 3.6+).

```python
# Good
from bashclient.types import EvalResult
from bashclient.errors import BashClientError

result = EvalResult(
    stdout="hello\n",
    stderr="",
    exit_code=0,
    duration=0.042,
)

message = f"Command exited with code {result.exit_code}"

# Avoid
from bashclient.types import *
message = "Command exited with code %d" % result.exit_code
```

### Type Hints

All public functions and methods must have complete type annotations.
Private methods should also have type hints where practical.

Use `typing` module types for Python 3.8 compatibility:

```python
from typing import Optional, List, Dict, Callable, Any, Union, Awaitable

# Good (Python 3.8 compatible)
def get_var(self, name: str) -> Optional[VarInfo]:
    ...

def on_observe(self, callback: Callable[[ObserveEvent], Any]) -> None:
    ...

# Avoid (requires Python 3.10+)
def get_var(self, name: str) -> VarInfo | None:
    ...
```

### Docstrings

Use Google-style docstrings for all public classes, methods, and
functions:

```python
async def eval(self, cmd: str, timeout: Optional[float] = None) -> EvalResult:
    """Execute a shell command and return the result.

    Sends the command to bash-server for evaluation and collects
    stdout, stderr, and exit code.

    Args:
        cmd: The shell command to execute.
        timeout: Maximum time to wait in seconds. Uses the client's
            default timeout if not specified.

    Returns:
        An EvalResult containing stdout, stderr, exit_code, and
        optional duration.

    Raises:
        AuthenticationError: If not authenticated.
        TimeoutError: If the command does not complete within the
            timeout period.
        ChannelError: If the server returns an error response.

    Example:
        >>> result = await client.eval("echo hello")
        >>> print(result.stdout)
        hello
    """
```

### Naming Conventions

| Element        | Convention       | Example                      |
|----------------|------------------|------------------------------|
| Module         | lowercase        | `protocol.py`                |
| Class          | PascalCase       | `BashClient`, `EvalResult`   |
| Function       | snake_case       | `encode_frame`               |
| Method         | snake_case       | `connect_stdio`              |
| Private method | _snake_case      | `_reader_loop`               |
| Constant       | UPPER_SNAKE_CASE | `CHAN_CONTROL`               |
| Variable       | snake_case       | `exit_code`                  |
| Type alias     | PascalCase       | `Callback`                   |

---

## Testing

### Running Tests

```bash
# Run all tests
pytest

# Verbose output
pytest -v

# Run a specific file
pytest tests/test_protocol.py

# Run tests matching a pattern
pytest -k "test_encode"

# Run with coverage (if coverage is installed)
pytest --cov=bashclient --cov-report=term-missing

# Run only unit tests (skip integration)
pytest -m "not integration"

# Run only integration tests
pytest -m integration
```

### Test Organization

| File                    | Type        | Dependencies     |
|-------------------------|-------------|------------------|
| `test_protocol.py`     | Unit        | None             |
| `test_channels.py`     | Unit        | Mock transport   |
| `test_types.py`        | Unit        | None             |
| `test_errors.py`       | Unit        | None             |
| `test_transport.py`    | Unit + Integ| Socket/subprocess|
| `test_integration.py`  | Integration | bash-server      |

### Writing Tests

Follow these conventions:

1. **One test function per behavior**. Name it `test_<what>_<condition>`.
2. **Arrange-Act-Assert** structure.
3. **Test both success and failure** paths.
4. **Use fixtures** for shared setup.
5. **Keep tests independent** -- no test should depend on another.

```python
def test_encode_frame_basic():
    """encode_frame produces valid NDJSON with channel ID."""
    result = encode_frame(1, {"op": "eval", "cmd": "ls"})
    decoded = json.loads(result.decode("utf-8"))
    assert decoded["ch"] == 1
    assert decoded["op"] == "eval"
    assert decoded["cmd"] == "ls"
    assert result.endswith(b"\n")


def test_decode_frame_missing_channel():
    """decode_frame raises ProtocolError when 'ch' is missing."""
    line = b'{"op": "eval"}\n'
    with pytest.raises(ProtocolError):
        decode_frame(line)
```

### Async Tests

The test suite uses `pytest-asyncio` with `asyncio_mode="auto"`.
Async test functions are detected automatically:

```python
# No decorator needed -- auto mode detects async functions
async def test_eval_simple(client_with_mock):
    """eval returns stdout from server response."""
    result = await client_with_mock.eval("echo hello")
    assert result.stdout == "hello\n"
    assert result.exit_code == 0
```

### Mock Transport

Use the `MockTransport` class from `conftest.py` for channel tests:

```python
@pytest.fixture
def mock_transport():
    """Create a mock transport with pre-loaded responses."""
    def _factory(responses: List[bytes]):
        return MockTransport(responses)
    return _factory


async def test_ping(mock_transport):
    responses = [
        b'{"ch":0,"op":"pong","version":"5.1"}\n',
    ]
    client = BashClient()
    client._transport = mock_transport(responses)
    client._start_reader()

    result = await client.ping()
    assert result.server_version == "5.1"
```

### Integration Tests

Integration tests require a running bash-server. They are marked with
`@pytest.mark.integration` and skipped when bash-server is not
available:

```python
@pytest.mark.integration
async def test_eval_integration(bash_client):
    """End-to-end eval through real bash-server."""
    result = await bash_client.eval("echo integration_test")
    assert result.stdout.strip() == "integration_test"
    assert result.exit_code == 0
```

To run integration tests:

```bash
# Ensure bash-server is on PATH
pytest -m integration
```

---

## Common Development Tasks

### Adding a New Channel Method

1. **Define the method in `channels.py`**:

```python
async def _state_list_vars(client: "BashClient", pattern: str = "*") -> List[VarInfo]:
    """List variables matching a glob pattern."""
    await client._send(CHAN_STATE, {
        "op": "list",
        "namespace": "variables",
        "pattern": pattern,
    })
    response = await client._recv(CHAN_STATE)
    if response.get("op") == "error":
        raise ChannelError(response.get("message", "list failed"))
    return [
        VarInfo(
            name=v["name"],
            value=v.get("value"),
            type=v.get("type", "string"),
            attributes=v.get("attributes", []),
        )
        for v in response.get("items", [])
    ]
```

2. **Add the type to `types.py`** (if needed):

If the method returns a new type, add it as a dataclass.

3. **Bind the method in `client.py`**:

```python
from .channels import _state_list_vars

class BashClient:
    ...
    async def list_vars(self, pattern: str = "*") -> List[VarInfo]:
        """List variables matching a pattern. See channels.py."""
        return await _state_list_vars(self, pattern)
```

4. **Export in `__init__.py`**:

Update `__all__` if any new public types were added.

5. **Write tests**:

- Add a test in `test_channels.py` with mock transport
- Add an integration test in `test_integration.py`

6. **Update documentation**:

- Add the method to `API.md`
- Add a usage example to `GUIDE.md`
- Update `api-metadata.json`

### Adding a New Transport

1. **Subclass `Transport` in `transport.py`**:

```python
class CustomTransport(Transport):
    """Transport over custom connection type."""

    async def connect(self, **kwargs) -> None:
        # Establish connection
        self._connected = True

    async def read_line(self) -> bytes:
        # Read one NDJSON line
        ...

    async def write(self, data: bytes) -> None:
        # Write data
        ...

    async def close(self) -> None:
        # Clean up
        self._connected = False

    @property
    def is_connected(self) -> bool:
        return self._connected
```

2. **Add a connect method to `BashClient`** in `client.py`:

```python
async def connect_custom(self, **kwargs) -> None:
    """Connect via custom transport."""
    self._transport = CustomTransport()
    await self._transport.connect(**kwargs)
    self._start_reader()
```

3. **Write tests** for the new transport.

4. **Update documentation**: ARCHITECTURE.md, API.md, INSTALL.md
   (platform notes).

### Adding a New Type

1. **Add the dataclass to `types.py`**:

```python
@dataclass(frozen=True)
class NewResult:
    """Description of what this type represents."""
    field1: str
    field2: int
    field3: Optional[float] = None
```

2. **Export in `__init__.py`**: Add to `__all__`.

3. **Use in channel methods**: Import and return from the relevant
   channel method.

4. **Update `api-metadata.json`**: Add to the `types` array.

### Adding a New Exception

1. **Add to `errors.py`**:

```python
class NewError(BashClientError):
    """Description of when this error occurs."""
    pass
```

2. **Export in `__init__.py`**: Add to `__all__`.

3. **Raise in the appropriate module**: Import and raise where needed.

4. **Update `api-metadata.json`**: Add to the `errors` array.

---

## Documentation

### Documentation Files

| File                | Content                           |
|---------------------|-----------------------------------|
| `README.md`         | Package overview, quick start     |
| `API.md`            | Complete API reference            |
| `GUIDE.md`          | User guide with examples          |
| `INSTALL.md`        | Installation instructions         |
| `ARCHITECTURE.md`   | Internal design documentation     |
| `CONTRIBUTING.md`   | This file                         |
| `TROUBLESHOOTING.md`| Problem diagnosis and solutions   |
| `CHANGELOG.md`      | Version history                   |
| `api-metadata.json` | Machine-readable API metadata     |
| `examples/README.md`| Example script documentation      |

### When to Update Documentation

- **New channel method**: Update API.md, GUIDE.md, api-metadata.json
- **New transport**: Update API.md, INSTALL.md, ARCHITECTURE.md,
  api-metadata.json
- **New type**: Update API.md, api-metadata.json
- **New exception**: Update API.md, TROUBLESHOOTING.md, api-metadata.json
- **Bug fix**: Update CHANGELOG.md, possibly TROUBLESHOOTING.md
- **Behavior change**: Update API.md, GUIDE.md, CHANGELOG.md

### Documentation Style

- Use Markdown for all documentation
- Use code blocks with language identifiers for syntax highlighting
- Use tables for structured information
- Keep line length under 72 characters in prose
- Use absolute anchors for cross-document links
- Use concrete examples rather than abstract descriptions

---

## Commit Conventions

Commit messages follow this format:

```
<type>: <short summary>

<optional longer description>
```

### Types

| Type       | Meaning                                    |
|------------|--------------------------------------------|
| `feat`     | New feature                                |
| `fix`      | Bug fix                                    |
| `docs`     | Documentation changes only                 |
| `test`     | Adding or modifying tests                  |
| `refactor` | Code change that neither fixes nor adds    |
| `style`    | Formatting, whitespace, etc.               |
| `chore`    | Build, tooling, or maintenance changes     |

### Examples

```
feat: add list_vars method to STATE channel

Adds a new method to list variables matching a glob pattern.
Returns a list of VarInfo dataclasses.

fix: handle empty stdout in eval response

Previously, eval would set stdout to None when the server returned
no stdout frames. Now it correctly returns an empty string.

docs: update API.md with list_vars documentation

test: add mock transport tests for list_vars
```

### Rules

1. Summary line: imperative mood, lowercase, no period, under 72 chars
2. Blank line between summary and body
3. Body: explain what and why, not how
4. Reference issues or PRs if applicable

---

## Pull Request Process

1. **Create a branch** from the current development branch:

   ```bash
   git checkout -b feat/my-feature
   ```

2. **Make your changes** following the code style and conventions above.

3. **Write tests** for all new functionality. Ensure existing tests
   still pass:

   ```bash
   pytest -v
   ```

4. **Update documentation** as described above.

5. **Commit** with conventional commit messages.

6. **Push** and create a pull request.

7. **PR description** should include:
   - Summary of changes
   - Motivation / context
   - Test plan
   - Documentation updates

8. **Review**: Address feedback promptly. Force-push to update the
   branch if needed.

9. **Merge**: Squash-merge is preferred for single-feature PRs.

---

## License

bashclient is licensed under the GNU General Public License v3.0 or
later (GPLv3+). All contributions must be compatible with this license.

By submitting a contribution, you agree that your work will be
licensed under GPLv3+ as part of this project.

See the `LICENSE` file for the full license text.

### License Headers

All Python source files should include a license header:

```python
# bashclient - Async Python client for bash-server
# Copyright (C) 2025 The Bash Contributors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
```
