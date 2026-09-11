# ADR-0004: Frontend-engine contract uses authenticated WebSocket + Protocol Buffers

- Status: Accepted
- Date: 2026-09-11

## Decision

The local frontend communicates with the native engine over loopback WebSocket. Message schemas are defined once in committed Protocol Buffer schemas and generated for C++ and TypeScript.

## Security

Each desktop engine launch receives a cryptographically random session token. No command is accepted before hello/authentication and protocol negotiation complete.

## Consequences

No parallel handwritten JSON production protocol is maintained. Documentation JSON snippets, when present, are explanatory only.