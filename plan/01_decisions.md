# Decisions

| id | status | blocks |
|---|---|---|
| D1 | Decided | — |
| D2 | Decided | — |
| D3 | Decided | — |
| D4 | Decided | — |
| D5 | Decided | — |
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

**Decided (Giacomo Forcina, 2026-10-14):** fit-primary ranking with a
quad-count gate. First discard any trial whose quad count falls outside a
tolerance band around the target's 11,054 (±15–20%, M4 to pick the exact
band). Among survivors, rank by combined spatial fit (`fit` + `fit_reverse`),
with edge-length CV as a tie-breaker. Degenerate-quad count is a hard
disqualifier (any trial with degenerate quads is dropped), not part of the
score.

Reason: M3's baseline measurement showed pure fit-ranking would just reward
ever-denser meshes — the CLI's 104,000-quad default output has far better fit
(mean 0.073) than a trial matched to the target's actual quad count (2,764
quads, mean fit 0.573), purely because denser meshes trivially fit the source
surface better. The gate exists specifically to stop the sweep from picking
that kind of trial.

**CV direction:** scored by closeness to the *target's own* CV (0.5552), not
by minimizing CV. Every CLI output measured so far is more uniform than the
Rhino target, and the target is the thing being replicated — matching its
edge-length variation is the goal, not independently rewarding uniformity.

Blocks: — (was M4, now unlocked).

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

