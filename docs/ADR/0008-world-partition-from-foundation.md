# ADR-0008: Large-world partitioning is foundational

- Status: Accepted
- Date: 2026-09-11

## Decision

Spatial partitioning, dirty-region computation, and renderer working-set streaming are designed before terrain/road feature scale grows.

Canonical entity identity is independent of partition/chunk identity.