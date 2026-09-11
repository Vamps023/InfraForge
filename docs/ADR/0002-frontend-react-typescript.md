# ADR-0002: Frontend uses React and strict TypeScript

- Status: Accepted
- Date: 2026-09-11

## Decision

The editor presentation layer uses React with TypeScript strict mode.

## Consequences

- React owns presentation/transient UI state only.
- Canonical project data is not stored as mutable frontend domain state.
- Feature UI modules communicate with the engine through typed client/query services.
- A shared design-system package owns common controls/tokens.