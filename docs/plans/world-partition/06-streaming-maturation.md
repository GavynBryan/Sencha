# Retired Streaming Maturation Experiment

Status: superseded by the implemented contracts in Plans 11 and 12.

This phase originally mixed traversal conditions and connection-local hints into
residency demand. That model has been removed. The retained maturation work is:

- dormant visible/physics neighbors;
- point-to-Zone-AABB spatial-radius demand;
- explainable demand records;
- traversal grace and linger;
- explicit gameplay pins;
- graph-level hop, radius, and resident-cap overrides.

Connections now contribute topology only. Gate state never erases an edge or
suppresses loading, and cross-Graph connections only seed the destination Zone;
the destination Graph expands according to its own policy. See Plan 11 for the
runtime contract and Plan 12 for the deletion ledger and tests.
