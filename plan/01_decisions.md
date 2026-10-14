# Decisions

| id | status | blocks |
|---|---|---|
| D1 | Decided | — |
| D2 | Decided | — |
| D3 | Decided | — |
| D4 | Decided | — |
| D5 | Open | M4 |
| D6 | Decided | — |

## D1 — Build system

**Decided (Giacomo Forcina, 2026-10-13):** new, separate CMakeLists.txt for
the core library/CLI, independent of the existing qmake GUI app.

## D2 — Weld strategy

**Decided (Giacomo Forcina, 2026-10-13):** exact-position dedup only, no
epsilon tolerance parameter.

## D3 — Windows scope

**Decided (Giacomo Forcina, 2026-10-13):** deferred. This plan is Linux-only;
Windows P/Invoke is an explicit open item, not a milestone.

## D4 — Model type parameter

**Decided (Giacomo Forcina, 2026-10-13):** stays hardcoded to Organic, not
exposed as a CLI flag or tuning dimension.

## D5 — Scoring metric weighting

**Open.** How to combine M3's separate metrics (quad-count delta,
edge-length CV, spatial-fit distance, degenerate-quad count) into one ranking
score for M4. Cannot be decided before M3's scorer exists and M4's Task 1 has
produced real score distributions to weigh against each other — a guessed
weighting risks optimizing for the wrong thing across the whole sweep.
Blocks: M4 (specifically M4's Task 2, ranking).

## D6 — Non-quad faces in the output

**Decided (Giacomo Forcina, 2026-10-14):** the core's `QuadExtractor` also
emits 3- and 5- to 7-gons (2.1 % of the faces at 11,054 target quads on
`source.obj`). The CLI post-processes its result to all-quad by splitting
every face into quads, Catmull–Clark style: an n-gon becomes n quads, so
quads are split too. Accepted consequences: the output is always pure quads;
the face count grows about 4×, so `--target-quads` no longer directly sets
the final face count; the output diverges from what the GUI produces for the
same input. Raised by M2.1 (Open #1), implemented in M2.3 as
`autoremesher_core_split_into_quads()`, which the CLI always calls.

