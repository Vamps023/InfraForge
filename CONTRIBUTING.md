# Contributing to InfraForge

## Before implementation

1. Read `README.md`, `docs/IMPLEMENTATION_STATUS.md`, the relevant PRD/TRD/domain docs, and accepted ADRs.
2. Trace the required production path before writing code.
3. Confirm whether the feature depends on an unfinished lower phase.
4. Do not copy OpenGeoStudio structure/code as a shortcut around InfraForge boundaries.

## Production rules

- No placeholder production handlers.
- No hard-coded success.
- No fake or timer-based progress pretending to measure work.
- No silent fallback from native/real functionality to mock/browser/demo functionality.
- No duplicated canonical state in the frontend.
- No direct database access from renderer/UI.
- No giant global feature store.
- No schema/table/command created merely because it may be useful later; add it with the real domain capability.

## Change scope

Implement one vertical capability coherently. When a change crosses UI, protocol, application, domain, persistence, renderer, or docs, update the complete required path rather than leaving disconnected helpers.

## Verification

Report exactly what was built and run. Compilation is not runtime verification. Unit tests are not packaged-app verification. Mock tests are not GPU verification.

## Commits

Use conventional, descriptive commits such as:

- `feat(project): add transactional project creation`
- `feat(protocol): add authenticated session handshake`
- `fix(renderer): release stale chunk resources after eviction`
- `docs(architecture): record renderer process decision`

## Implementation status

Update `docs/IMPLEMENTATION_STATUS.md` only when evidence exists. Never mark a capability implemented because a specification, interface, stub, or test-only helper exists.