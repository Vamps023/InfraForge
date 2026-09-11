# ADR-0007: One canonical project georeference

- Status: Accepted
- Date: 2026-09-11

## Decision

Each project owns exactly one canonical georeference configuration. All importers, terrain, roads, environment, simulation, renderer conversion, and exporters use the same Geo service.

Independent feature-level CRS/origin implementations are forbidden.