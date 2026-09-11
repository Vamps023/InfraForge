# ADR-0005: Project storage is SQLite plus file-backed content

- Status: Accepted
- Date: 2026-09-11

## Decision

Canonical structured project state uses SQLite. Large raster/model/texture/generated payloads are file-backed inside the project directory when project-owned.

## Consequences

- Structured mutations use transactions.
- Schema migrations are explicit and versioned.
- Generated cache is not canonical.
- Project-relative paths are preferred for project-owned files.