# Installation Guide

## bashclient &mdash; TypeScript/Node.js Client for bash-server

This document covers all installation methods, system requirements, development
setup, and platform-specific notes for the `bashclient` package.

---

## Table of Contents

1. [System Requirements](#system-requirements)
2. [Quick Install](#quick-install)
3. [Install from npm](#install-from-npm)
4. [Install from Source](#install-from-source)
5. [Development Setup](#development-setup)
6. [Build Configuration](#build-configuration)
7. [Test Configuration](#test-configuration)
8. [Platform Notes](#platform-notes)
9. [Verification](#verification)
10. [Uninstallation](#uninstallation)

---

## System Requirements

### Runtime Requirements

| Component        | Minimum Version | Recommended    | Notes                          |
|------------------|-----------------|----------------|--------------------------------|
| Node.js          | 16.0.0          | 20 LTS or 22   | ES2020 features required       |
| TypeScript       | 4.7+            | 5.x            | Only for source builds         |
| bash-server      | 0.1.0           | latest          | The server this client targets |
| Operating System | Linux, macOS, Windows (Cygwin) | Linux | Named Pipes require Cygwin on Windows |

### No External Dependencies

`bashclient` uses **only Node.js standard library modules**:

- `net` &mdash; Unix socket and TCP transport
- `child_process` &mdash; stdio transport (spawning bash-server)
- `fs` &mdash; fd transport, config file reading
- `path` &mdash; Path resolution
- `os` &mdash; Platform detection, home directory
- `events` &mdash; EventEmitter base
- `crypto` &mdash; Token generation
- `buffer` &mdash; Binary frame handling

Zero external runtime dependencies. Zero supply-chain risk from third-party
packages.

### Development Dependencies

These are only needed for building from source or contributing:

| Package      | Purpose                     |
|--------------|-----------------------------|
| `typescript` | TypeScript compiler         |
| `jest`       | Test runner                 |
| `ts-jest`    | TypeScript support for jest |
| `@types/node`| Node.js type definitions    |
| `@types/jest`| Jest type definitions       |

---

## Quick Install

For most users, a single command is all you need:

```bash
npm install bashclient
```

Verify the installation:

```bash
node -e "const { BashClient } = require('bashclient'); console.log('OK');"
```

---

## Install from npm

### Global Installation

Install globally to use the library in any project:

```bash
npm install -g bashclient
```

### Project-Local Installation

Install as a project dependency (recommended):

```bash
cd your-project
npm install bashclient
```

This adds `bashclient` to your `package.json` dependencies:

```json
{
  "dependencies": {
    "bashclient": "^0.1.0"
  }
}
```

### Dev-Only Installation

If you only need `bashclient` during development or testing:

```bash
npm install --save-dev bashclient
```

### Specific Version

Pin to a specific version:

```bash
npm install bashclient@0.1.0
```

### With package-lock.json

For reproducible installs in CI/CD:

```bash
npm ci
```

This uses the exact versions from `package-lock.json`.

---

## Install from Source

### Clone and Build

```bash
# Clone the repository
git clone https://github.com/your-org/bash.git
cd bash/bash-server/clients/typescript

# Install development dependencies
npm install

# Build TypeScript to JavaScript
npm run build

# Run the test suite
npm test

# Optionally link for local development
npm link
```

### Build Steps Explained

The build process compiles TypeScript source in `src/` to JavaScript in `dist/`:

```bash
# Full build (clean + compile + generate declarations)
npm run build

# Just compile (incremental)
npx tsc

# Watch mode for development
npx tsc --watch
```

### Source Directory Layout

```
bash-server/clients/typescript/
├── src/
│   ├── index.ts          # Public API re-exports
│   ├── client.ts         # BashClient class
│   ├── types.ts          # TypeScript interfaces and constants
│   ├── protocol.ts       # NDJSON frame encoding/decoding
│   ├── transport.ts      # Transport interface and implementations
│   ├── errors.ts         # Error class hierarchy
│   └── channels/
│       ├── control.ts    # ControlChannel
│       ├── command.ts    # CommandChannel
│       ├── state.ts      # StateChannel
│       ├── observe.ts    # ObserveChannel
│       ├── debug.ts      # DebugChannel
│       └── pty.ts        # PtyChannel
├── dist/                 # Compiled output (generated)
├── tests/
│   ├── client.test.ts
│   ├── protocol.test.ts
│   ├── transport.test.ts
│   └── channels/
│       ├── control.test.ts
│       ├── command.test.ts
│       ├── state.test.ts
│       ├── observe.test.ts
│       ├── debug.test.ts
│       └── pty.test.ts
├── package.json
├── tsconfig.json
├── jest.config.js
└── README.md
```

### Build Output

After `npm run build`, the `dist/` directory contains:

```
dist/
├── index.js              # CommonJS entry point
├── index.d.ts            # TypeScript declarations
├── client.js
├── client.d.ts
├── types.js
├── types.d.ts
├── protocol.js
├── protocol.d.ts
├── transport.js
├── transport.d.ts
├── errors.js
├── errors.d.ts
└── channels/
    ├── control.js
    ├── control.d.ts
    ├── command.js
    ├── command.d.ts
    ├── state.js
    ├── state.d.ts
    ├── observe.js
    ├── observe.d.ts
    ├── debug.js
    ├── debug.d.ts
    ├── pty.js
    └── pty.d.ts
```

---

## Development Setup

### Prerequisites

1. Install Node.js 16+ (20 LTS recommended):

   ```bash
   # Using nvm (recommended)
   nvm install 20
   nvm use 20

   # Or download from https://nodejs.org/
   ```

2. Verify Node.js and npm:

   ```bash
   node --version   # v20.x.x
   npm --version    # 10.x.x
   ```

3. Build and install bash-server (see main project README).

### Full Development Workflow

```bash
# 1. Clone and navigate to the TypeScript client
cd bash/bash-server/clients/typescript

# 2. Install all dependencies (including dev deps)
npm install

# 3. Run the test suite
npm test

# 4. Build for distribution
npm run build

# 5. Type-check without emitting
npx tsc --noEmit

# 6. Run tests in watch mode during development
npx jest --watch
```

### npm Scripts

The `package.json` defines these scripts:

```json
{
  "scripts": {
    "build": "tsc",
    "clean": "rm -rf dist/",
    "test": "jest",
    "test:watch": "jest --watch",
    "test:coverage": "jest --coverage",
    "prepublishOnly": "npm run clean && npm run build && npm test",
    "lint": "tsc --noEmit"
  }
}
```

---

## Build Configuration

### tsconfig.json

The TypeScript configuration targets Node.js 16+ compatibility:

```json
{
  "compilerOptions": {
    "target": "ES2020",
    "module": "commonjs",
    "lib": ["ES2020"],
    "outDir": "./dist",
    "rootDir": "./src",
    "declaration": true,
    "declarationMap": true,
    "sourceMap": true,
    "strict": true,
    "esModuleInterop": true,
    "skipLibCheck": true,
    "forceConsistentCasingInFileNames": true,
    "resolveJsonModule": true,
    "moduleResolution": "node",
    "types": ["node"]
  },
  "include": ["src/**/*.ts"],
  "exclude": ["node_modules", "dist", "tests"]
}
```

Key settings explained:

| Setting             | Value      | Why                                         |
|---------------------|------------|---------------------------------------------|
| `target`            | ES2020     | Supports optional chaining, nullish coalescing |
| `module`            | commonjs   | Node.js default module system               |
| `declaration`       | true       | Generates .d.ts for TypeScript consumers    |
| `strict`            | true       | Full type safety                            |
| `sourceMap`         | true       | Debugging support                           |

### package.json

```json
{
  "name": "bashclient",
  "version": "0.1.0",
  "description": "TypeScript/Node.js client for bash-server",
  "main": "dist/index.js",
  "types": "dist/index.d.ts",
  "files": ["dist/"],
  "engines": {
    "node": ">=16.0.0"
  },
  "license": "GPL-3.0-or-later",
  "keywords": [
    "bash",
    "shell",
    "bash-server",
    "evaluation",
    "daemon"
  ]
}
```

---

## Test Configuration

### jest.config.js

```js
/** @type {import('jest').Config} */
module.exports = {
  preset: 'ts-jest',
  testEnvironment: 'node',
  roots: ['<rootDir>/tests'],
  testMatch: ['**/*.test.ts'],
  transform: {
    '^.+\\.ts$': 'ts-jest',
  },
  collectCoverageFrom: [
    'src/**/*.ts',
    '!src/index.ts',
  ],
  coverageDirectory: 'coverage',
  coverageThresholds: {
    global: {
      branches: 80,
      functions: 80,
      lines: 80,
      statements: 80,
    },
  },
  testTimeout: 10000,
};
```

### Running Tests

```bash
# Run all tests
npm test

# Run with verbose output
npx jest --verbose

# Run a specific test file
npx jest tests/client.test.ts

# Run tests matching a pattern
npx jest --testNamePattern="connect"

# Run with coverage report
npm run test:coverage

# Watch mode (re-runs on file changes)
npx jest --watch
```

### Test Categories

| Category     | Location                  | Description                     |
|-------------|---------------------------|---------------------------------|
| Unit tests  | `tests/*.test.ts`         | Module-level tests, mocked I/O  |
| Channel tests| `tests/channels/*.test.ts`| Per-channel functionality       |
| Integration | `tests/integration/`      | Requires running bash-server    |

### Integration Tests

Integration tests require a running bash-server instance:

```bash
# Start bash-server
bash-server --name test-ts-client &

# Run integration tests
npx jest tests/integration/

# Stop bash-server
kill %1
```

---

## Platform Notes

### Linux

Linux is the primary development and deployment platform. All features work
out of the box.

```bash
# Install Node.js via package manager
sudo apt install nodejs npm    # Debian/Ubuntu
sudo dnf install nodejs npm    # Fedora/RHEL

# Or use nvm (recommended)
curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.39.7/install.sh | bash
nvm install 20
```

Unix socket transport is the default and recommended transport on Linux.

### macOS

All features work on macOS. Install Node.js via:

```bash
# Homebrew
brew install node

# Or nvm
nvm install 20
```

Unix socket transport works natively. Named Pipe transport is not available
on macOS.

### Windows (Cygwin)

On Windows, use the Cygwin environment for full compatibility:

```bash
# In Cygwin terminal
# Ensure Node.js is accessible (install Windows Node.js, accessible via PATH)
node --version

# Install from source
cd /home/user/bash/bash-server/clients/typescript
npm install
npm run build
npm test
```

**Transport availability on Windows/Cygwin:**

| Transport   | Status      | Notes                                    |
|-------------|-------------|------------------------------------------|
| Unix socket | Works       | Via Cygwin Unix socket emulation         |
| stdio       | Works       | spawn bash-server with --stdio           |
| fd          | Works       | File descriptor passing via Cygwin       |
| Named Pipe  | Works       | Native Windows Named Pipes via Cygwin    |

**Named Pipe transport** is only available on Cygwin/Windows and requires
bash-server built with `--enable-bash-server` on Cygwin.

### FreeBSD / Other Unix

Should work with any Node.js 16+ installation. Unix socket transport
is the default.

---

## Verification

After installation, verify everything works:

### 1. Check Import

```bash
node -e "
  const { BashClient } = require('bashclient');
  console.log('Import: OK');
  console.log('BashClient:', typeof BashClient);
"
```

Expected output:

```
Import: OK
BashClient: function
```

### 2. Check TypeScript Types

```bash
npx tsc --noEmit -e "
import { BashClient, EvalResult, Message } from 'bashclient';
const client: BashClient = {} as BashClient;
const result: EvalResult = { stdout: '', stderr: '', exit_code: 0 };
const msg: Message = { ch: 0, type: 'ping' };
"
```

### 3. Check All Exports

```bash
node -e "
  const bc = require('bashclient');
  const expected = [
    'BashClient',
    'AuthError',
    'ProtocolError',
    'TimeoutError',
    'TransportError',
    'ServerError',
    'CHAN_CONTROL',
    'CHAN_COMMAND',
    'CHAN_STATE',
    'CHAN_OBSERVE',
    'CHAN_DEBUG',
    'CHAN_PTY',
  ];
  const missing = expected.filter(e => !(e in bc));
  if (missing.length > 0) {
    console.error('Missing exports:', missing);
    process.exit(1);
  }
  console.log('All exports present: OK');
"
```

### 4. Connection Test

Requires a running bash-server:

```bash
# Start bash-server
bash-server --name verify-test &
SERVER_PID=$!

# Test connection
node -e "
  const { BashClient } = require('bashclient');
  (async () => {
    const client = await BashClient.connect();
    const result = await client.command.eval('echo hello');
    console.log('Result:', result.stdout.trim());
    await client.close();
    process.exit(result.stdout.trim() === 'hello' ? 0 : 1);
  })().catch(e => { console.error(e); process.exit(1); });
"

# Cleanup
kill $SERVER_PID
```

### 5. Version Check

```bash
node -e "
  const pkg = require('bashclient/package.json');
  console.log('bashclient version:', pkg.version);
  console.log('Node.js version:', process.version);
  console.log('Platform:', process.platform);
"
```

---

## Uninstallation

### Remove from Project

```bash
npm uninstall bashclient
```

### Remove Global Installation

```bash
npm uninstall -g bashclient
```

### Clean Build Artifacts (Source Install)

```bash
cd bash-server/clients/typescript
npm run clean
rm -rf node_modules/
```

---

## Troubleshooting Installation

### "Cannot find module 'bashclient'"

Ensure the package is installed in the correct project directory:

```bash
ls node_modules/bashclient/
```

If missing, run `npm install bashclient` in your project root.

### TypeScript Cannot Find Type Declarations

Ensure your `tsconfig.json` includes `node_modules`:

```json
{
  "compilerOptions": {
    "moduleResolution": "node"
  }
}
```

### Build Fails with TypeScript Errors

Ensure compatible TypeScript version:

```bash
npx tsc --version   # Should be 4.7+
```

### Node.js Version Too Old

```bash
node --version
```

If below 16.0.0, upgrade Node.js:

```bash
nvm install 20
nvm use 20
```

### Permission Errors on Global Install

```bash
# Use sudo (Linux/macOS)
sudo npm install -g bashclient

# Or configure npm prefix
npm config set prefix ~/.npm-global
export PATH=~/.npm-global/bin:$PATH
npm install -g bashclient
```

See [TROUBLESHOOTING.md](TROUBLESHOOTING.md) for more detailed solutions.
