#!/usr/bin/env python3
"""
score_remesh.py -- scores a candidate quad mesh against the Rhino target (M3,
plan/roadmap/03_M3_scoring_script.md)

    score_remesh.py CANDIDATE.obj TARGET.obj [--out runs.jsonl] [--samples N] [--seed S]

prints one JSON line on stdout and, with --out, also appends it to that file, so
that the results of many runs accumulate into one .jsonl file. Each metric is
reported for the candidate and the target, with the candidate's delta:

  quads              quad faces, their delta and percent delta from the target's
  edge_length_cv     stddev / mean of the lengths of the distinct undirected edges
                     of the quad faces: how uniform the remesh is, at any scale
  fit                distance from each candidate vertex to the nearest point of
                     the target's surface (its quads split into two triangles), as
                     mean / rms / p95 / max, in the OBJ's units and, as *_rel,
                     divided by the target's bounding-box diagonal. Both meshes
                     derive from source.obj and share its frame, so there is no
                     registration step.
  fit_reverse        the same, from each target vertex to the candidate's surface;
                     catches a candidate that covers only part of the target
  degenerate_quads   quads that revisit a vertex (fewer than 4 distinct indices)

A self-score (a mesh against itself) gives 0 for every delta and every distance.
Only vertices that some face uses are measured. Faces that are not quads are
counted in non_quad_faces and left out of the quad metrics, but their triangles
are part of the surface that fit_reverse measures to.

Exit codes: 0 on success, 1 if a mesh cannot be read or --out cannot be written,
2 for a usage error. Needs numpy and scipy.

The module can also be imported: score(candidate_path, target_path) returns the
record as a dict.
"""

import argparse
import json
import sys

import numpy as np
from scipy.spatial import cKDTree

# Bumped when a field changes meaning, so older .jsonl lines can be told apart
SCORER_VERSION = 1


class PolyMesh:
    """Polygon mesh: V is (n, 3) float64, faces is a list of int arrays (0-based)"""

    def __init__(self, V, faces):
        self.V = np.asarray(V, dtype=np.float64).reshape(-1, 3)
        self.faces = faces
        self.sizes = np.array([len(f) for f in faces], dtype=np.int64)
        self.flat = (np.concatenate(faces).astype(np.int64) if faces
                     else np.zeros(0, dtype=np.int64))
        self.offsets = np.concatenate([[0], np.cumsum(self.sizes)])

    def of_size(self, n):
        """(m, n) array of the faces with n corners"""
        sel = np.flatnonzero(self.sizes == n)
        return self.flat[self.offsets[sel][:, None] + np.arange(n)]

    def triangles(self):
        """(t, 3) fan triangulation of all faces"""
        tris = []
        for n in np.unique(self.sizes):
            f = self.of_size(n)
            for k in range(1, n - 1):
                tris.append(f[:, [0, k, k + 1]])
        return np.concatenate(tris) if tris else np.zeros((0, 3), dtype=np.int64)

    def referenced(self):
        """Indices of the vertices that some face uses"""
        return np.unique(self.flat)


def load_obj(path):
    """Reads the 'v' and 'f' records of an OBJ file, keeping polygons intact. Extra
    fields on 'v' lines (Rhino's "v x y z 0 0 0" colours) and the texture and normal
    indices of 'f' corners are ignored; negative indices count from the end."""
    V, faces = [], []
    with open(path, 'r') as f:
        for lineno, line in enumerate(f, 1):
            tok = line.split()
            if not tok:
                continue
            try:
                if tok[0] == 'v':
                    V.append([float(x) for x in tok[1:4]])
                    if len(V[-1]) != 3:
                        raise ValueError('vertex with fewer than 3 coordinates')
                elif tok[0] == 'f':
                    idx = [int(t.split('/')[0]) for t in tok[1:]]
                    if len(idx) < 3:
                        raise ValueError('face with fewer than 3 corners')
                    n = len(V)
                    face = [i - 1 if i > 0 else n + i for i in idx]
                    if 0 in idx or min(face) < 0 or max(face) >= n:
                        raise ValueError('face index out of range')
                    faces.append(np.array(face, dtype=np.int64))
            except ValueError as e:
                raise ValueError('%s:%d: %s' % (path, lineno, e)) from None
    if not V or not faces:
        raise ValueError('%s: no vertices or no faces' % path)
    return PolyMesh(V, faces)


def _dot(u, v):
    return np.einsum('ij,ij->i', u, v)


def _closest_on_segments(p, a, b):
    ab = b - a
    with np.errstate(invalid='ignore', divide='ignore'):
        t = np.clip(_dot(p - a, ab) / _dot(ab, ab), 0.0, 1.0)
    t = np.where(np.isfinite(t), t, 0.0)
    return a + t[:, None] * ab


def closest_points(p, a, b, c):
    """Closest point of triangle (a[i], b[i], c[i]) to p[i], for each row i
    (Ericson, Real-Time Collision Detection, 5.1.5). A point in a corner's region
    gets that corner exactly, so a mesh's own vertices are at distance 0 from it.
    Degenerate triangles (collinear or coincident corners) fall back to the
    nearest of their three edges and three corners."""
    ab, ac = b - a, c - a
    ap, bp, cp = p - a, p - b, p - c
    d1, d2 = _dot(ab, ap), _dot(ac, ap)
    d3, d4 = _dot(ab, bp), _dot(ac, bp)
    d5, d6 = _dot(ab, cp), _dot(ac, cp)
    va = d3 * d6 - d5 * d4
    vb = d5 * d2 - d1 * d6
    vc = d1 * d4 - d3 * d2
    with np.errstate(invalid='ignore', divide='ignore'):
        denom = 1.0 / (va + vb + vc)
        q = a + ab * (vb * denom)[:, None] + ac * (vc * denom)[:, None]
        # The regions in reverse order of Ericson's tests, so that each earlier
        # test overrides the later ones, as its early returns do
        w = (d4 - d3) / ((d4 - d3) + (d5 - d6))
        m = (va <= 0) & (d4 - d3 >= 0) & (d5 - d6 >= 0)
        q = np.where(m[:, None], b + w[:, None] * (c - b), q)            # edge BC
        w = d2 / (d2 - d6)
        m = (vb <= 0) & (d2 >= 0) & (d6 <= 0)
        q = np.where(m[:, None], a + w[:, None] * ac, q)                 # edge AC
        m = (d6 >= 0) & (d5 <= d6)
        q = np.where(m[:, None], c, q)                                   # corner C
        v = d1 / (d1 - d3)
        m = (vc <= 0) & (d1 >= 0) & (d3 <= 0)
        q = np.where(m[:, None], a + v[:, None] * ab, q)                 # edge AB
        m = (d3 >= 0) & (d4 <= d3)
        q = np.where(m[:, None], b, q)                                   # corner B
        m = (d1 <= 0) & (d2 <= 0)
        q = np.where(m[:, None], a, q)                                   # corner A

    bad = ~np.isfinite(q).all(axis=1)
    if bad.any():
        P, A, B, C = p[bad], a[bad], b[bad], c[bad]
        cand = np.stack([A, B, C, _closest_on_segments(P, A, B),
                         _closest_on_segments(P, B, C), _closest_on_segments(P, C, A)])
        d = np.linalg.norm(cand - P[None], axis=2)
        q[bad] = cand[d.argmin(axis=0), np.arange(len(P))]
    return q


class SurfaceDistance:
    """Exact point-to-surface distance for a triangle mesh"""

    K = 16          # nearest triangle centroids tried first
    CHUNK = 20000   # points per batch in the exhaustive pass

    def __init__(self, V, T):
        self.tris = np.asarray(V, dtype=np.float64)[np.asarray(T)]       # (t, 3, 3)
        centroid = self.tris.mean(axis=1)
        self.r_max = float(np.linalg.norm(self.tris - centroid[:, None], axis=2).max())
        self.tree = cKDTree(centroid)

    def _distance(self, points, ti):
        t = self.tris[ti]
        return np.linalg.norm(closest_points(points, t[:, 0], t[:, 1], t[:, 2]) - points,
                              axis=1)

    def __call__(self, points):
        points = np.asarray(points, dtype=np.float64)
        k = min(self.K, len(self.tris))
        dc, idx = self.tree.query(points, k=k)
        dc, idx = dc.reshape(len(points), k), idx.reshape(len(points), k)
        best = np.full(len(points), np.inf)
        for j in range(k):
            best = np.minimum(best, self._distance(points, idx[:, j]))

        # A triangle not tried has its centroid at least dc[:, -1] from the point,
        # and every point of it within r_max of its centroid, so it is at least
        # dc[:, -1] - r_max away. Where that does not settle it, every triangle
        # with its centroid within best + r_max is tried.
        if k < len(self.tris):
            todo = np.flatnonzero(best > dc[:, -1] - self.r_max)
            for start in range(0, len(todo), self.CHUNK):
                sel = todo[start:start + self.CHUNK]
                near = self.tree.query_ball_point(points[sel], best[sel] + self.r_max)
                pi = np.repeat(sel, [len(n) for n in near])
                ti = np.concatenate(near).astype(np.int64)
                np.minimum.at(best, pi, self._distance(points[pi], ti))
        return best


def edge_length_cv(mesh):
    """stddev / mean of the lengths of the distinct undirected quad edges. An edge
    from a vertex to itself (a repeated corner) is left out: degenerate_quads
    counts those quads. None if there are no quad edges or they all have length 0."""
    Q = mesh.of_size(4)
    E = np.stack([Q, np.roll(Q, -1, axis=1)], axis=2).reshape(-1, 2)
    E = np.unique(np.sort(E, axis=1), axis=0)
    E = E[E[:, 0] != E[:, 1]]
    if len(E) == 0:
        return None
    length = np.linalg.norm(mesh.V[E[:, 0]] - mesh.V[E[:, 1]], axis=1)
    mean = length.mean()
    return float(length.std() / mean) if mean > 0 else None


def degenerate_quads(mesh):
    """Quads with fewer than 4 distinct vertex indices"""
    Q = np.sort(mesh.of_size(4), axis=1)
    return int((Q[:, 1:] == Q[:, :-1]).any(axis=1).sum())


def sample_vertices(mesh, samples, seed):
    """The referenced vertices, or `samples` of them chosen at random with `seed`
    (all of them if samples is 0 or not below their number)"""
    idx = mesh.referenced()
    if 0 < samples < len(idx):
        idx = np.sort(np.random.default_rng(seed).choice(idx, samples, replace=False))
    return mesh.V[idx]


def distance_stats(d, diag):
    stats = {
        'samples': int(len(d)),
        'mean': float(d.mean()),
        'rms': float(np.sqrt((d * d).mean())),
        'p95': float(np.percentile(d, 95)),
        'max': float(d.max()),
    }
    for s in ('mean', 'rms', 'p95', 'max'):
        stats[s + '_rel'] = stats[s] / diag if diag > 0 else None
    return stats


def delta(c, t):
    return None if c is None or t is None else c - t


def score(candidate_path, target_path, samples=0, seed=0):
    """Scores the candidate OBJ against the target OBJ; returns the record. With
    samples > 0, each distance is measured from that many vertices only, chosen
    at random with seed."""
    cand, target = load_obj(candidate_path), load_obj(target_path)
    used = target.V[target.referenced()]
    diag = float(np.linalg.norm(used.max(axis=0) - used.min(axis=0)))

    qc, qt = int((cand.sizes == 4).sum()), int((target.sizes == 4).sum())
    cvc, cvt = edge_length_cv(cand), edge_length_cv(target)
    dc, dt = degenerate_quads(cand), degenerate_quads(target)
    fit = SurfaceDistance(target.V, target.triangles())(sample_vertices(cand, samples, seed))
    back = SurfaceDistance(cand.V, cand.triangles())(sample_vertices(target, samples, seed))
    return {
        'scorer_version': SCORER_VERSION,
        'candidate': candidate_path,
        'target': target_path,
        'quads': {'candidate': qc, 'target': qt, 'delta': qc - qt,
                  'delta_pct': 100.0 * (qc - qt) / qt if qt else None},
        'non_quad_faces': {'candidate': int((cand.sizes != 4).sum()),
                           'target': int((target.sizes != 4).sum())},
        'vertices': {'candidate': len(cand.V), 'target': len(target.V)},
        'edge_length_cv': {'candidate': cvc, 'target': cvt, 'delta': delta(cvc, cvt)},
        'target_bbox_diagonal': diag,
        'fit': distance_stats(fit, diag),
        'fit_reverse': distance_stats(back, diag),
        'degenerate_quads': {'candidate': dc, 'target': dt, 'delta': dc - dt},
    }


def non_negative(text):
    n = int(text)
    if n < 0:
        raise argparse.ArgumentTypeError('must be >= 0: %s' % text)
    return n


def main(argv=None):
    ap = argparse.ArgumentParser(
        description='Score a candidate quad mesh against a target quad mesh; prints '
                    'one JSON line.')
    ap.add_argument('candidate', help='candidate OBJ, e.g. autoremesher-cli output')
    ap.add_argument('target', help='target OBJ, e.g. quad-remeshed.obj')
    ap.add_argument('--out', help='also append the JSON line to this file')
    ap.add_argument('--samples', type=non_negative, default=0,
                    help='measure distances from this many random vertices of each '
                         'mesh (default 0: every vertex a face uses)')
    ap.add_argument('--seed', type=int, default=0,
                    help='random seed for --samples (default 0)')
    args = ap.parse_args(argv)

    try:
        record = score(args.candidate, args.target, args.samples, args.seed)
    except (OSError, ValueError) as e:
        print('score_remesh.py: %s' % e, file=sys.stderr)
        return 1
    line = json.dumps(record, allow_nan=False)
    if args.out:
        try:
            with open(args.out, 'a') as f:
                f.write(line + '\n')
        except OSError as e:
            print('score_remesh.py: %s' % e, file=sys.stderr)
            return 1
    print(line)
    return 0


if __name__ == '__main__':
    sys.exit(main())
