# bashclient-api-metadata(5) — client binding API metadata file format

# DESCRIPTION

The **api-metadata.json** file is a machine-readable API index placed in
the root of each client binding package directory (e.g.,
`clients/python/api-metadata.json`, `clients/typescript/api-metadata.json`).
It provides a complete, structured description of every public class,
method, type, error, and constant exported by that binding.

The primary consumers are **AI coding assistants** and **tool-discovery
systems** that need to understand a client library's API surface without
parsing source code. Human developers can also use it as a quick
reference or to generate documentation stubs.

Each binding maintains its own **api-metadata.json** because the public
API surface differs across languages — Python exposes context managers
and async iterators, TypeScript uses Promises and generics, C exposes
opaque handles and error codes, and Java uses builders and checked
exceptions. The file captures these language-specific details while
keeping a uniform top-level schema.

# FORMAT

The file MUST be valid JSON (RFC 8259). All string values use UTF-8
encoding. The top-level value is a JSON object with the following keys.

# Top-level structure

```json
{
  "package":   { ... },
  "modules":   [ ... ],
  "classes":   [ ... ],
  "types":     [ ... ],
  "errors":    [ ... ],
  "constants": [ ... ],
  "examples":  [ ... ]
}
```

All seven keys are REQUIRED. An empty array `[]` is permitted where a
section has no entries (e.g., a minimal binding might have no custom
types).

# package object

The **package** object describes the binding as a whole.

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| name | string | yes | Package/distribution name (e.g., `"bashclient"`, `"@bashclient/core"`) |
| language | string | yes | Target language: `"python"`, `"typescript"`, `"c"`, `"java"` |
| version | string | yes | SemVer version string (e.g., `"1.0.0"`) |
| description | string | yes | One-line human-readable summary |
| python_requires | string | no | Minimum Python version (e.g., `">=3.8"`) |
| node_requires | string | no | Minimum Node.js version (e.g., `">=16.0.0"`) |
| java_requires | string | no | Minimum Java version (e.g., `">=11"`) |
| dependencies | object | no | Map of dependency name → version constraint |
| license | string | yes | SPDX license identifier (e.g., `"GPL-3.0-or-later"`) |
| documentation | object | no | Links to documentation files |

The **documentation** sub-object maps human-readable labels to relative
file paths or URLs:

```json
"documentation": {
  "readme": "README.md",
  "api_reference": "docs/api.md",
  "changelog": "CHANGELOG.md",
  "man_page": "bash-server-client-api(7)"
}
```

Only the **language** field constrains which `*_requires` field is
relevant. Unrelated `*_requires` fields SHOULD be omitted rather than
set to `null`.

# modules array

Each entry in the **modules** array describes one importable module,
file, or header.

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| name | string | yes | Fully qualified module name (e.g., `"bashclient.client"`, `"bashclient.h"`) |
| description | string | yes | What this module provides |
| classes | array | no | List of class names defined in this module |
| exports | array | no | List of free functions or symbols exported |

For C bindings, each header file is a "module". For TypeScript, each
source file that re-exports from `index.ts` is a module.

# classes array

Each entry in the **classes** array describes one public class, struct,
or interface.

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| name | string | yes | Class name (e.g., `"BashClient"`, `"bc_client_t"`) |
| module | string | yes | Module where this class is defined |
| description | string | no | What this class represents |
| factory_methods | array | no | Class methods or free functions that create instances |
| methods | array | no | Instance methods |
| properties | array | no | Public properties / getters |

The **factory_methods** and **methods** arrays contain method entry
objects (see below). The **properties** array contains objects with
fields: **name** (string), **type** (string), **description** (string),
and **readonly** (boolean, default `true`).

# method entries

Method entries appear in **factory_methods** and **methods** arrays.

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| name | string | yes | Method name |
| signature | string | yes | Full language-specific signature |
| description | string | no | What this method does |
| parameters | array | yes | Ordered list of parameter objects |
| returns | object | yes | Return type description |
| raises | array | no | Errors/exceptions this method can raise |
| example | string | no | Short code snippet showing usage |
| docs | string | no | Link to detailed documentation |
| async | boolean | no | Whether the method is async (default `false`) |
| static | boolean | no | Whether the method is static (default `false`) |

Each **parameter** object:

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| name | string | yes | Parameter name |
| type | string | yes | Language-specific type annotation |
| description | string | yes | What this parameter controls |
| required | boolean | yes | Whether the parameter is mandatory |
| default | any | no | Default value (omit if required is `true`) |

The **returns** object:

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| type | string | yes | Return type annotation |
| description | string | no | What the return value represents |

Each entry in the **raises** array is a string naming an error class
(e.g., `"AuthError"`, `"TimeoutError"`). The error details are in the
top-level **errors** array.

# types array

Each entry describes a data class, named tuple, interface, or struct
used in method signatures.

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| name | string | yes | Type name (e.g., `"EvalResult"`, `"bc_result_t"`) |
| kind | string | yes | One of: `"dataclass"`, `"interface"`, `"struct"`, `"enum"`, `"typedef"` |
| description | string | no | What this type represents |
| fields | array | yes | List of field objects |

Each **field** object:

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| name | string | yes | Field name |
| type | string | yes | Language-specific type |
| description | string | yes | What this field contains |
| default | any | no | Default value, if any |

# errors array

Each entry describes an error class or error code.

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| name | string | yes | Error class name (e.g., `"AuthError"`, `"BC_ERR_AUTH"`) |
| base | string | no | Parent error class (e.g., `"BashClientError"`) |
| description | string | yes | When and why this error is raised |
| when_raised | string | yes | Specific conditions that trigger this error |

See **bash-server-client-errors**(5) for the unified error hierarchy
across all bindings.

# constants array

Each entry describes a public constant or enum value.

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| name | string | yes | Constant name (e.g., `"DEFAULT_TIMEOUT"`, `"CHAN_CONTROL"`) |
| value | any | yes | The constant's value |
| type | string | yes | Type of the value (e.g., `"int"`, `"float"`, `"string"`) |
| description | string | yes | What this constant represents |

# examples array

Each entry provides a self-contained usage example.

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| title | string | yes | Short title (e.g., `"Basic eval"`) |
| file | string | no | Path to a standalone example file |
| description | string | yes | What this example demonstrates |
| code_snippet | string | no | Inline code (use `\n` for line breaks) |

At least one of **file** or **code_snippet** SHOULD be present. If
both are provided, **code_snippet** is treated as an excerpt of the
full example in **file**.

# EXAMPLES

A minimal **api-metadata.json** for a Python client binding:

```json
{
  "package": {
    "name": "bashclient",
    "language": "python",
    "version": "1.0.0",
    "description": "Python client for bash-server",
    "python_requires": ">=3.8",
    "dependencies": {},
    "license": "GPL-3.0-or-later",
    "documentation": {
      "readme": "README.md",
      "api_reference": "docs/api.md"
    }
  },
  "modules": [
    {
      "name": "bashclient.client",
      "description": "Main client class and connection management",
      "classes": ["BashClient"],
      "exports": ["connect"]
    }
  ],
  "classes": [
    {
      "name": "BashClient",
      "module": "bashclient.client",
      "description": "Connection to a running bash-server instance",
      "factory_methods": [
        {
          "name": "connect",
          "signature": "connect(socket_path: str = None, *, timeout: float = 30.0) -> BashClient",
          "description": "Connect to a bash-server and authenticate",
          "parameters": [
            {
              "name": "socket_path",
              "type": "Optional[str]",
              "description": "Path to the Unix domain socket. Resolved automatically if omitted.",
              "required": false,
              "default": null
            },
            {
              "name": "timeout",
              "type": "float",
              "description": "Connection timeout in seconds",
              "required": false,
              "default": 30.0
            }
          ],
          "returns": {
            "type": "BashClient",
            "description": "Connected and authenticated client instance"
          },
          "raises": ["AuthError", "TransportError", "TimeoutError"],
          "static": true
        }
      ],
      "methods": [
        {
          "name": "eval",
          "signature": "eval(command: str, *, timeout: float = None) -> EvalResult",
          "description": "Evaluate a shell command and return the result",
          "parameters": [
            {
              "name": "command",
              "type": "str",
              "description": "Shell command to evaluate",
              "required": true
            },
            {
              "name": "timeout",
              "type": "Optional[float]",
              "description": "Per-command timeout in seconds",
              "required": false,
              "default": null
            }
          ],
          "returns": {
            "type": "EvalResult",
            "description": "Command output and exit code"
          },
          "raises": ["ServerError", "TimeoutError", "ProtocolError"]
        },
        {
          "name": "close",
          "signature": "close() -> None",
          "description": "Disconnect from the server",
          "parameters": [],
          "returns": {
            "type": "None"
          }
        }
      ],
      "properties": [
        {
          "name": "connected",
          "type": "bool",
          "description": "Whether the client is currently connected",
          "readonly": true
        }
      ]
    }
  ],
  "types": [
    {
      "name": "EvalResult",
      "kind": "dataclass",
      "description": "Result of a command evaluation",
      "fields": [
        {
          "name": "stdout",
          "type": "str",
          "description": "Standard output from the command"
        },
        {
          "name": "stderr",
          "type": "str",
          "description": "Standard error from the command"
        },
        {
          "name": "exit_code",
          "type": "int",
          "description": "Exit status (0 = success)"
        }
      ]
    }
  ],
  "errors": [
    {
      "name": "BashClientError",
      "base": "Exception",
      "description": "Base exception for all bashclient errors",
      "when_raised": "Never raised directly; use subclasses"
    },
    {
      "name": "AuthError",
      "base": "BashClientError",
      "description": "Authentication with the server failed",
      "when_raised": "Invalid or expired token, or token file not found"
    },
    {
      "name": "TimeoutError",
      "base": "BashClientError",
      "description": "Operation exceeded its time limit",
      "when_raised": "Connect or eval timeout expired"
    }
  ],
  "constants": [
    {
      "name": "DEFAULT_TIMEOUT",
      "value": 30.0,
      "type": "float",
      "description": "Default connection timeout in seconds"
    },
    {
      "name": "PROTOCOL_V2",
      "value": 2,
      "type": "int",
      "description": "Protocol version for v2 JSON framing"
    }
  ],
  "examples": [
    {
      "title": "Basic eval",
      "file": "examples/basic_eval.py",
      "description": "Connect, evaluate a command, and print the output",
      "code_snippet": "client = BashClient.connect()\nresult = client.eval('echo hello')\nprint(result.stdout)\nclient.close()"
    }
  ]
}
```

# NOTES

The **api-metadata.json** file MUST reside at the root of its client
binding directory:

```
clients/
  python/
    api-metadata.json
    bashclient/
      __init__.py
      client.py
  typescript/
    api-metadata.json
    src/
      index.ts
  c/
    api-metadata.json
    include/
      bashclient.h
  java/
    api-metadata.json
    src/
      ...
```

Validate the file with standard JSON tools:

```sh
python3 -m json.tool api-metadata.json > /dev/null
```

or

```sh
jq empty api-metadata.json
```

The **version** field in the **package** object SHOULD track the client
binding version, not the bash-server version. When the bash-server
protocol changes in a way that affects the client API, both the binding
version and the metadata file should be updated together.

AI tools consuming this file should treat the **signature** field as
the authoritative calling convention and the **parameters** array as
the structured breakdown. If there is a conflict, the **signature**
field takes precedence.

# SEE ALSO

**bash-server-client-api**(7),
**bash-server-client-errors**(5),
**bash-server-json-messages**(5),
**bash-server**(1)

# AUTHORS

GNU Bash is written by Brian Fox and Chet Ramey. The bash-server
extension and this documentation were developed as part of the
Cygwin bash-server project.

# COPYRIGHT

Copyright (C) 2024-2025 Free Software Foundation, Inc. License GPLv3+:
GNU GPL version 3 or later <https://www.gnu.org/licenses/gpl.html>.
This is free software; you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.
