# InfraForge agent engineering contract

This file applies to AI coding agents and automated contributors.

## Required reading

Before production changes, read:

1. `README.md`
2. `docs/IMPLEMENTATION_STATUS.md`
3. `docs/00_PRODUCT/PRD.md`
4. `docs/01_ARCHITECTURE/TRD.md`
5. relevant accepted ADRs/domain specifications
6. `CONTRIBUTING.md`

## Non-negotiable rules

- Do not copy OpenGeoStudio architecture as implementation scaffolding.
- Do not add fake production success, mock native responses, timer-based fake progress, dummy world data, empty handlers, or silent fallback behavior.
- Do not move domain/business logic into Electron or React to avoid implementing the native path.
- Do not create a giant cross-domain frontend store.
- Do not create duplicate CRS/origin/project models.
- Do not add a parallel implementation when a canonical module exists; extend/refactor the canonical path.
- Do not claim implementation or verification from docs/interfaces/tests alone.
- Do not mark `docs/IMPLEMENTATION_STATUS.md` implemented without production evidence.

## Feature workflow

Trace the real path first: UI/API -> contract -> application -> domain -> persistence/render/simulation where applicable. Implement the required path coherently. Build/typecheck/test the layers you changed. Report unavailable toolchains and unexecuted runtime checks exactly.

## Architecture protection

If a requested change conflicts with an accepted ADR, update/replace the ADR explicitly before implementing a contradictory architecture. Do not silently erode boundaries through convenience imports or compatibility shims.