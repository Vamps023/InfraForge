# ADR-0010: Placeholder production paths are forbidden

- Status: Accepted
- Date: 2026-09-11

## Decision

A production route/control may not return hard-coded success, fake progress, dummy project data, fabricated renderer state, or silent browser/mock fallback merely to make a workflow appear complete.

An unavailable capability remains unavailable and is reported honestly until its real vertical path exists.

## Consequences

`docs/IMPLEMENTATION_STATUS.md` distinguishes specifications from implementation. Definition-of-Done requires real entry-point-to-domain integration.