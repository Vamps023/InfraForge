# ADR-0009: Native engine owns canonical mutable project state

- Status: Accepted
- Date: 2026-09-11

## Decision

The frontend receives projections/results/events but does not own a duplicate mutable Project object. The native engine owns the canonical open-project session and revision.

## Consequences

Frontend state stores remain limited to UI concerns. Edits are commands and become visible as accepted results/events rather than optimistic mutation of an entire project graph.