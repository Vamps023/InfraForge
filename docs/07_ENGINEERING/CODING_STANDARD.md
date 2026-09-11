# Coding standard

## General rules

1. Production code must represent real reachable behavior.
2. Placeholder success responses, fake progress, mock production data, empty production handlers, and silent fallback paths are forbidden.
3. A design document is not implementation evidence.
4. Stable entity IDs are used across persistence, protocol, renderer mapping, diagnostics, and tests.
5. Errors cross boundaries explicitly.

## C++

- C++23 language mode.
- RAII for resources.
- Prefer value semantics/unique ownership; shared ownership requires a concrete lifetime reason.
- Raw owning pointers are forbidden.
- Domain code avoids platform/render/database headers.
- Exceptions must not cross C ABI/plugin/process boundaries unhandled.
- Use strong domain types or explicit units where ambiguity creates correctness risk.
- Thread ownership and synchronization are documented for mutable shared structures.

## TypeScript/React

- TypeScript strict mode.
- No `any` in production feature code without a documented interop reason and boundary validation.
- React components render/coordinate UI; domain algorithms do not live in components.
- Zustand/store state is UI-only unless a documented exception is accepted by ADR.
- Server/project state is accessed through typed client/query services.
- Feature internals are private; cross-feature imports use exported feature APIs.

## Electron

- renderer Node integration disabled;
- context isolation enabled;
- sandbox enabled where compatible with the chosen native viewport integration;
- preload exposes minimal validated methods;
- no arbitrary IPC channel passthrough;
- business logic is not added to Electron main/preload.

## File/module size

Approximate review thresholds, not mechanical limits:

- React component: review architecture above ~250 lines.
- TypeScript module: review architecture above ~400 lines.
- C++ implementation file: review architecture above ~500 lines.

Large files can be justified for generated tables or cohesive data, but giant controller/store modules must be split by responsibility.

## Dependencies

A new dependency requires:

- concrete need;
- license review;
- supported-platform review;
- ownership of update/security responsibility;
- no duplication of an existing capability without rationale.

## Comments

Comments explain invariants, constraints, coordinate conventions, ownership, or non-obvious reasoning. Comments do not restate obvious code and are not used to hide incomplete implementation.