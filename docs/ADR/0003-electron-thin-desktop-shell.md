# ADR-0003: Electron is a thin desktop shell

- Status: Accepted
- Date: 2026-09-11

## Decision

Electron owns window/OS/process integration and secure preload only. Project/domain/GIS/simulation logic is excluded from Electron main/preload.

## Security requirements

Renderer Node integration is disabled, context isolation is enabled, and preload exposes only explicit methods. Arbitrary IPC passthrough is forbidden.