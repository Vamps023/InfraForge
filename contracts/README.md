# InfraForge contracts

`contracts/proto` is the only source for production frontend-engine wire message schemas.

## TypeScript generation

From the repository root:

```bash
npm install
npm run proto:lint
npm run proto:generate:ts
```

The generated TypeScript output is build output under `packages/protocol-ts/src/gen` and is intentionally not hand-edited.

## C++ generation

CMake invokes the protobuf compiler through the pinned vcpkg Protobuf dependency and writes generated C++ files into the native build directory. Generated C++ is not committed.

Any change to an existing field number or incompatible message shape must follow the protocol compatibility rules in `docs/03_PROTOCOL/WEBSOCKET_PROTOCOL.md`.