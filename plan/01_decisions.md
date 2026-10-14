# Decisions

| id | status | blocks |
|---|---|---|
| D1 | Decided | — |
| D2 | Decided | — |
| D3 | Decided | — |
| D4 | Decided | — |
| D5 | Decided | — |
| D6 | Revised | — |

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

**Decided (Giacomo Forcina, 2026-10-14), reverted (Giacomo Forcina,
2026-10-15):** originally, the CLI post-processed its result to all-quad by
splitting every face into quads, Catmull–Clark style (`autoremesher_core_split_into_quads()`,
implemented in M2.3). Giacomo visually reviewed M4's winning trial against
`quad-remeshed.obj` and judged the topology messy and overly dense, and the
overall shape deformed in several zones, versus the Rhino target's clean,
low-resolution layout — even though that trial scored best on every one of
M3's numeric metrics (mean, rms, p95 and max fit) across all 90 swept trials.
The numeric metrics did not catch this.

The mandatory split is judged the most likely cause: it is applied to
*every* face, including faces that were already valid quads, quadrupling the
face count and breaking each quad's clean edge flow into a 4-quad pinwheel
around a centroid; the new centroid/midpoint vertices also sit slightly
inside the true surface rather than on it (per `HANDOFF_M2_TASK3.md`).

**Reverted to the original (a) reading from M2.1's Open #1:** the CLI no
longer calls the split by default. Output is the natural polygon mesh the
core produces (quads plus a small fraction of triangles/pentagons — 2.1% on
`source.obj`), with non-quad faces counted and reported, not eliminated.
`autoremesher_core_split_into_quads()` stays in the library (already written
and tested) but is no longer called unconditionally by the CLI.

This requires new work, not just a flag flip: M2 needs a task to change the
CLI's default and reword its exit criterion back to "mostly quads"; M3 needs
to rebaseline its self-score/default-baseline measurements against the
unsplit output (and double-check its degenerate-quad rule doesn't miscount a
valid triangle/pentagon as degenerate); M4 needs to rerun the whole sweep and
ranking against the unsplit output, and this time end with an explicit visual
comparison against `quad-remeshed.obj` before the winner is reported done —
not just a metrics pass. See each phase's new task (M2 Task 4, M3 Task 3, M4
Task 3).

