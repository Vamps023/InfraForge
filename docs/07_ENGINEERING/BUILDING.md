# Building and running InfraForge

This document describes how to configure, build, test, and run the InfraForge stack from source.

## Prerequisites

### Native C++ Toolchain
- **CMake**: version >= 3.25.
- **C++23 Compiler**:
  - Windows: Visual Studio 2022 (MSVC 19.38 / toolset v143 or newer).
  - Linux: GCC >= 13 or Clang >= 17.
- **vcpkg**: Pinned to the registry baseline in `.vcpkg` (or an external vcpkg installation).
- **Vulkan SDK**: Vulkan 1.3 SDK installed with development headers, loaders, and Khronos validation layers.

### Web and Desktop Toolchain
- **Node.js**: >= 24.0.0.
- **npm**: >= 10.0.0.
- **Buf CLI**: version >= 1.30 (or managed via `@bufbuild/buf` devDependency).

---

## 1. Native engine and viewport build

InfraForge uses CMake with vcpkg in manifest mode (`vcpkg.json`).

### Step 1.1: Bootstrap vcpkg

If not already bootstrapped:

**Windows:**
```powershell
.\.vcpkg\bootstrap-vcpkg.bat -disableMetrics
```

**Linux:**
```bash
./.vcpkg/bootstrap-vcpkg.sh -disableMetrics
```

### Step 1.2: Configure CMake

Set the toolchain file to vcpkg's `vcpkg.cmake`:

```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=.vcpkg/scripts/buildsystems/vcpkg.cmake -DINFRAFORGE_WARNINGS_AS_ERRORS=ON
```

### Step 1.3: Compile native executables

Build the Release configuration:

```bash
cmake --build build --config Release
```

This compiles:
- `infraforge-engine` (in `build/engine/Release/` or `build/engine/`)
- `infraforge-viewport` (in `build/viewport/Release/` or `build/viewport/`)
- Test suites: `infraforge-engine-tests` and `infraforge-viewport-tests`

### Step 1.4: Run native self-check and tests

Run CTest:
```bash
ctest --test-dir build --build-config Release --output-on-failure
```

Run engine startup self-check (validates SQLite persistence, PROJ georeferencing, and protocol baseline):

**Windows:**
```powershell
.\build\engine\Release\infraforge-engine.exe --self-check
```

**Linux:**
```bash
./build/engine/infraforge-engine --self-check
```

---

## 2. Web and desktop shell setup

### Step 2.1: Install dependencies

From the repository root:

```bash
npm install
```

### Step 2.2: Generate protocol TypeScript bindings

```bash
npm run proto:lint
npm run proto:generate:ts
```

This generates TypeScript contracts into `packages/protocol-ts/src/gen` based on `contracts/proto/`.

### Step 2.3: Typecheck and unit tests

```bash
npm run typecheck
npm --workspace @infraforge/frontend run test
npm --workspace @infraforge/desktop run test
```

---

## 3. End-to-end protocol verification

To verify the real engine WebSocket protocol without mocks:

**Windows:**
```powershell
$env:INFRAFORGE_ENGINE_PATH = "$PWD\build\engine\Release\infraforge-engine.exe"
npm run verify:engine
```

**Linux:**
```bash
INFRAFORGE_ENGINE_PATH="$PWD/build/engine/infraforge-engine" npm run verify:engine
```

---

## 4. Running the desktop application

Build the web bundles and launch Electron with supervised engine and native viewport:

```bash
npm run start:desktop
```

Electron supervises:
1. `infraforge-engine` with a loopback port and session authentication token.
2. `infraforge-viewport` parented into the shell window via Win32 child HWND.
3. React editor shell connected to the engine via authenticated WebSocket.
