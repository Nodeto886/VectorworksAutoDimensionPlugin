#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Numerical verification of the AutoDimensionPlugin "virtual line intersection" geometry.

The production logic is compiled into the Vectorworks plug-in and cannot be
unit-tested without the licensed SDK and host runtime:
    sdk-projects/2025/AutoDimensionPlugin/Source/AutoDimensionObj.cpp
This file is a pure-stdlib Python 3 replica of the exact same math (symbol by
symbol) so the formulas can be exercised numerically.

Replicated blocks:
  * BuildLineMeasurement
  * CreateVirtualLineIntersectionDimensions
      - segment/virtual-line intersection math
      - scale-aware parallel rejection
      - relative duplicate rejection
      - insufficient-intersections bailout
      - projection sort
      - pairwise aligned dimensions
  * kEditConvert H/V projection selection

Run with:  python tools/test_autodim_geometry.py
Exits non-zero if any assertion fails. Safe to re-run any number of times.
"""

import math
import sys

# --- constants (mirror AutoDimensionObj.cpp) ---------------------------------
K_GEOMETRY_TOLERANCE = 1e-6
DEDUP_BASE_DISTANCE = 1e-4


# --- replica of the C++ math -------------------------------------------------

def build_virtual_line(first, second):
    """Replica of BuildLineMeasurement.

    Returns (dx, dy, length) or None when the line is degenerate (length <= 1e-6),
    matching the C++ false return that makes the caller bail with 0 dimensions
    (AutoDimensionObj.cpp:644-647).
    """
    dx = second[0] - first[0]            # cpp:169
    dy = second[1] - first[1]            # cpp:170
    length = math.hypot(dx, dy)          # cpp:171
    if length <= K_GEOMETRY_TOLERANCE:   # cpp:172-174
        return None
    return (dx, dy, length)


def in_parameter_range(t, u):
    """t/u range acceptance, identical to AutoDimensionObj.cpp:678."""
    return not (
        t < -K_GEOMETRY_TOLERANCE
        or t > 1.0 + K_GEOMETRY_TOLERANCE
        or u < -K_GEOMETRY_TOLERANCE
        or u > 1.0 + K_GEOMETRY_TOLERANCE
    )


def segment_intersection(first, v, seg_start, seg_end):
    """One candidate edge vs the virtual line.

    Virtual line: P = first + t*V, t in [0,1].
    Edge:         S = seg_start + u*Sd, u in [0,1], Sd = seg_end - seg_start.

    Returns (t, u, point) or None when the edge is skipped.
    """
    vx, vy = v
    sd_x = seg_end[0] - seg_start[0]            # cpp:670
    sd_y = seg_end[1] - seg_start[1]            # cpp:671
    det = sd_x * vy - vx * sd_y
    virtual_length = math.hypot(vx, vy)
    segment_length = math.hypot(sd_x, sd_y)
    parallel_tolerance = K_GEOMETRY_TOLERANCE * virtual_length * segment_length
    if abs(det) <= parallel_tolerance:           # scale-independent parallel / collinear / degenerate
        return None
    qx = seg_start[0] - first[0]                # cpp:674  q = seg.start - first
    qy = seg_start[1] - first[1]                # cpp:675
    t = (sd_x * qy - qx * sd_y) / det           # cpp:676
    u = (vx * qy - qx * vy) / det               # cpp:677
    if not in_parameter_range(t, u):            # cpp:678
        return None
    point = (first[0] + t * vx, first[1] + t * vy)  # cpp:679
    return (t, u, point)


def is_duplicate_of_any(point, existing, virtual_length, base_distance=DEDUP_BASE_DISTANCE):
    """Relative duplicate test used by CreateVirtualLineIntersectionDimensions."""
    dedup_distance = max(base_distance, virtual_length * 1e-6)
    for existing_point in existing:
        if math.hypot(existing_point[0] - point[0], existing_point[1] - point[1]) <= dedup_distance:
            return True
    return False


def collect_intersections(first, second, segments):
    """Replica of the intersection loop of CreateVirtualLineIntersectionDimensions.

    segments: iterable of ((sx0, sy0), (sx1, sy1)).
    Returns the projection-sorted intersection list, or [] when fewer than two
    intersections survive (the C++ returns 0 dimensions in that case).
    """
    virtual = build_virtual_line(first, second)
    if virtual is None:
        return []                                   # cpp:644-647
    vx, vy, length = virtual
    intersections = []
    for seg_start, seg_end in segments:
        hit = segment_intersection(first, (vx, vy), seg_start, seg_end)
        if hit is None:
            continue
        _t, _u, point = hit
        if is_duplicate_of_any(point, intersections, length):
            continue                                # cpp:684
        intersections.append(point)                 # cpp:685
    if len(intersections) < 2:
        return []                                   # cpp:689-692
    intersections.sort(
        key=lambda p: (p[0] - first[0]) * vx + (p[1] - first[1]) * vy
    )                                               # cpp:694-698 projection ascending
    return intersections


def pairwise_dimensions(intersections):
    """Consecutive pairs used to create aligned dimensions."""
    return [(intersections[i - 1], intersections[i]) for i in range(1, len(intersections))]


def convert_projection_end(start, end):
    """kEditConvert H/V projection end."""
    dx = end[0] - start[0]                          # cpp:989
    dy = end[1] - start[1]                          # cpp:990
    if abs(dx) >= abs(dy):                          # cpp:994
        return (end[0], start[1])                   # cpp:995  horizontal
    return (start[0], end[1])                       # cpp:996  vertical


# --- tiny stdlib test harness -------------------------------------------------

_TESTS = []


def test_case(fn):
    _TESTS.append(fn)
    return fn


def approx(a, b, tol=1e-9):
    return abs(a - b) <= tol


def pt_approx(p, q, tol=1e-9):
    return approx(p[0], q[0], tol) and approx(p[1], q[1], tol)


# --- test cases ---------------------------------------------------------------

@test_case
def test_cross_intersection_point():
    # virtual (0,0)->(10,0) crossed by edge (5,-5)->(5,5) at (5,0)
    hit = segment_intersection((0.0, 0.0), (10.0, 0.0), (5.0, -5.0), (5.0, 5.0))
    assert hit is not None, "expected a crossing intersection"
    t, u, point = hit
    assert approx(t, 0.5) and approx(u, 0.5), "expected t=u=0.5, got t=%r u=%r" % (t, u)
    assert pt_approx(point, (5.0, 0.0)), "expected (5,0), got %r" % (point,)


@test_case
def test_skewed_cross_parameters():
    # oblique virtual (0,0)->(10,10) crossed by edge (5,-5)->(5,15) at (5,5)
    hit = segment_intersection((0.0, 0.0), (10.0, 10.0), (5.0, -5.0), (5.0, 15.0))
    assert hit is not None, "expected an intersection"
    t, u, point = hit
    assert approx(t, 0.5) and approx(u, 0.5), "expected t=u=0.5, got t=%r u=%r" % (t, u)
    assert pt_approx(point, (5.0, 5.0)), "expected (5,5), got %r" % (point,)


@test_case
def test_oblique_virtual_line():
    # virtual (1,1)->(7,4), V=(6,3); edge x=3 vertical from (3,-1)->(3,8)
    hit = segment_intersection((1.0, 1.0), (6.0, 3.0), (3.0, -1.0), (3.0, 8.0))
    assert hit is not None, "expected an intersection"
    t, u, point = hit
    assert approx(t, 1.0 / 3.0) and approx(u, 1.0 / 3.0), "got t=%r u=%r" % (t, u)
    assert pt_approx(point, (3.0, 2.0)), "expected (3,2), got %r" % (point,)


@test_case
def test_t0_endpoint_touch_accepted():
    # intersection exactly at virtual line start (t == 0) must be accepted
    hit = segment_intersection((0.0, 0.0), (10.0, 0.0), (0.0, -5.0), (0.0, 5.0))
    assert hit is not None, "t==0 endpoint touch must be accepted"
    t, u, point = hit
    assert approx(t, 0.0) and approx(u, 0.5), "got t=%r u=%r" % (t, u)
    assert pt_approx(point, (0.0, 0.0)), "expected (0,0), got %r" % (point,)


@test_case
def test_t1_endpoint_touch_accepted():
    # intersection exactly at virtual line end (t == 1) must be accepted
    hit = segment_intersection((0.0, 0.0), (10.0, 0.0), (10.0, -5.0), (10.0, 5.0))
    assert hit is not None, "t==1 endpoint touch must be accepted"
    t, u, point = hit
    assert approx(t, 1.0) and approx(u, 0.5), "got t=%r u=%r" % (t, u)
    assert pt_approx(point, (10.0, 0.0)), "expected (10,0), got %r" % (point,)


@test_case
def test_u0_endpoint_touch_accepted():
    # intersection exactly at edge start (u == 0) must be accepted
    hit = segment_intersection((0.0, 0.0), (10.0, 0.0), (3.0, 0.0), (3.0, 5.0))
    assert hit is not None, "u==0 endpoint touch must be accepted"
    t, u, point = hit
    assert approx(t, 0.3) and approx(u, 0.0), "got t=%r u=%r" % (t, u)
    assert pt_approx(point, (3.0, 0.0)), "expected (3,0), got %r" % (point,)


@test_case
def test_u1_endpoint_touch_accepted():
    # intersection exactly at edge end (u == 1) must be accepted
    hit = segment_intersection((0.0, 0.0), (10.0, 0.0), (7.0, -5.0), (7.0, 0.0))
    assert hit is not None, "u==1 endpoint touch must be accepted"
    t, u, point = hit
    assert approx(t, 0.7) and approx(u, 1.0), "got t=%r u=%r" % (t, u)
    assert pt_approx(point, (7.0, 0.0)), "expected (7,0), got %r" % (point,)


@test_case
def test_parallel_skipped():
    # parallel edges never intersect (det == 0 -> skip)
    assert segment_intersection((0.0, 0.0), (10.0, 0.0), (0.0, 2.0), (10.0, 2.0)) is None
    assert collect_intersections((0.0, 0.0), (10.0, 0.0),
                                 [((0.0, 2.0), (10.0, 2.0))]) == []


@test_case
def test_collinear_skipped():
    # collinear edge lying on the virtual line (det == 0 -> skip)
    assert segment_intersection((0.0, 0.0), (10.0, 0.0), (2.0, 0.0), (8.0, 0.0)) is None


@test_case
def test_degenerate_edge_skipped():
    # zero-length edge has Sd=(0,0) -> det == 0 -> skip, no division by zero
    assert segment_intersection((0.0, 0.0), (10.0, 0.0), (5.0, 0.0), (5.0, 0.0)) is None


@test_case
def test_t_slightly_outside_rejected():
    # t = 1.000002 > 1 + 1e-6 -> rejected even though u is in range
    hit = segment_intersection((0.0, 0.0), (10.0, 0.0), (10.00002, -5.0), (10.00002, 5.0))
    assert hit is None, "t just past the virtual-line end must be rejected"


@test_case
def test_u_slightly_outside_rejected():
    # u ~ -2e-6 < -1e-6 -> rejected (edge starts a hair above the virtual line)
    hit = segment_intersection((0.0, 0.0), (10.0, 0.0), (5.0, 0.00001), (5.0, 5.0))
    assert hit is None, "u just below 0 must be rejected"


@test_case
def test_near_tolerance_accepted_geometric():
    # t = 1.0000005 (within 1+1e-6) -> accepted; point slightly past virtual end
    hit = segment_intersection((0.0, 0.0), (10.0, 0.0), (10.000005, -5.0), (10.000005, 5.0))
    assert hit is not None, "t within tolerance past the end must be accepted"
    _t, _u, point = hit
    assert pt_approx(point, (10.000005, 0.0)), "got %r" % (point,)
    # u ~ -2e-7 (within -1e-6) -> accepted
    hit2 = segment_intersection((0.0, 0.0), (10.0, 0.0), (5.0, 0.000001), (5.0, 5.0))
    assert hit2 is not None, "u within tolerance below 0 must be accepted"
    assert pt_approx(hit2[2], (5.0, 0.0)), "got %r" % (hit2[2],)


@test_case
def test_range_predicate_exact_boundary():
    # exact 1e-6 boundaries of the t/u acceptance predicate (cpp:678)
    eps = 1e-6
    assert in_parameter_range(1.0 + eps, 0.5)          # t == 1+1e-6 accepted
    assert not in_parameter_range(1.0 + eps + 1e-12, 0.5)  # strict >: beyond boundary -> rejected
    assert not in_parameter_range(1.0 + 2 * eps, 0.5)  # rejected
    assert in_parameter_range(-eps, 0.5)               # t == -1e-6 accepted
    assert not in_parameter_range(-eps - 1e-12, 0.5)   # rejected
    assert in_parameter_range(0.5, 1.0 + eps)          # u == 1+1e-6 accepted
    assert not in_parameter_range(0.5, 1.0 + 2 * eps)  # rejected
    assert in_parameter_range(0.5, -eps)               # u == -1e-6 accepted
    assert not in_parameter_range(0.5, -eps - 1e-12)   # rejected


@test_case
def test_dedup_same_vertex_multiple_edges():
    # three edges hitting the same point (5,0) plus one at (9,0)
    segments = [
        ((5.0, -5.0), (5.0, 5.0)),    # vertical through (5,0)
        ((5.0, -2.0), (5.0, 2.0)),    # overlapping vertical, same point
        ((0.0, 5.0), (10.0, -5.0)),   # diagonal through (5,0)
        ((9.0, -5.0), (9.0, 5.0)),    # distinct point
    ]
    points = collect_intersections((0.0, 0.0), (10.0, 0.0), segments)
    assert len(points) == 2, "expected 2 deduplicated intersections, got %r" % (points,)
    assert pt_approx(points[0], (5.0, 0.0)) and pt_approx(points[1], (9.0, 0.0)), points


@test_case
def test_dedup_distance_threshold():
    # The configured base threshold applies to ordinary-sized virtual lines.
    existing = [(5.0, 0.0)]
    assert is_duplicate_of_any((5.0, 0.0), existing, 10.0) is True
    assert is_duplicate_of_any((5.0 + 0.5e-4, 0.0), existing, 10.0) is True
    assert is_duplicate_of_any((5.0 + 1.5e-4, 0.0), existing, 10.0) is False


@test_case
def test_dedup_threshold_scales_with_virtual_line():
    # A 1,000,000-unit virtual line uses a 1-unit threshold, preventing almost
    # coincident intersections from becoming tiny dimension fragments.
    existing = [(100.0, 0.0)]
    assert is_duplicate_of_any((100.5, 0.0), existing, 1_000_000.0) is True
    assert is_duplicate_of_any((101.5, 0.0), existing, 1_000_000.0) is False


@test_case
def test_small_perpendicular_segments_are_not_parallel():
    # With the former absolute det <= 1e-6 check, det=1e-8 was rejected even
    # though the two 1e-4 segments cross at a right angle.
    hit = segment_intersection(
        (0.0, 0.0), (1e-4, 0.0),
        (5e-5, -5e-5), (5e-5, 5e-5),
    )
    assert hit is not None, "small perpendicular segments must intersect"
    t, u, point = hit
    assert approx(t, 0.5) and approx(u, 0.5)
    assert pt_approx(point, (5e-5, 0.0))


@test_case
def test_sorting_out_of_order():
    # unsorted input must be returned in projection ascending order
    segments = [
        ((8.0, -5.0), (8.0, 5.0)),
        ((2.0, -5.0), (2.0, 5.0)),
        ((5.0, -5.0), (5.0, 5.0)),
    ]
    points = collect_intersections((0.0, 0.0), (10.0, 0.0), segments)
    assert len(points) == 3, points
    for got, want in zip(points, [(2.0, 0.0), (5.0, 0.0), (8.0, 0.0)]):
        assert pt_approx(got, want), "expected %r, got %r" % (want, got)


@test_case
def test_sorting_reverse_direction():
    # virtual line first=(10,0) second=(0,0): V points left, so projection
    # (p-first).V is largest for the leftmost point -> descending x order
    segments = [
        ((2.0, -5.0), (2.0, 5.0)),
        ((8.0, -5.0), (8.0, 5.0)),
        ((5.0, -5.0), (5.0, 5.0)),
    ]
    points = collect_intersections((10.0, 0.0), (0.0, 0.0), segments)
    assert len(points) == 3, points
    for got, want in zip(points, [(8.0, 0.0), (5.0, 0.0), (2.0, 0.0)]):
        assert pt_approx(got, want), "expected %r, got %r" % (want, got)


@test_case
def test_pairwise_dimensions_between_consecutive():
    # consecutive pairs feed the aligned dimension creation (cpp:704-706)
    segments = [
        ((1.0, -5.0), (1.0, 5.0)),
        ((4.0, -5.0), (4.0, 5.0)),
        ((7.0, -5.0), (7.0, 5.0)),
        ((9.0, -5.0), (9.0, 5.0)),
    ]
    points = collect_intersections((0.0, 0.0), (10.0, 0.0), segments)
    pairs = pairwise_dimensions(points)
    assert len(pairs) == 3, pairs
    expected = [((1.0, 0.0), (4.0, 0.0)), ((4.0, 0.0), (7.0, 0.0)), ((7.0, 0.0), (9.0, 0.0))]
    for got, want in zip(pairs, expected):
        assert pt_approx(got[0], want[0]) and pt_approx(got[1], want[1]), "got %r want %r" % (got, want)


@test_case
def test_insufficient_intersections_returns_empty():
    # fewer than 2 surviving intersections -> no dimensions (cpp:689-692)
    assert collect_intersections((0.0, 0.0), (10.0, 0.0),
                                 [((5.0, -5.0), (5.0, 5.0))]) == []
    assert collect_intersections((0.0, 0.0), (10.0, 0.0), []) == []


@test_case
def test_degenerate_virtual_line_returns_empty():
    # BuildLineMeasurement fails for first == second -> 0 dimensions (cpp:644-647)
    assert build_virtual_line((3.0, 3.0), (3.0, 3.0)) is None
    assert collect_intersections((3.0, 3.0), (3.0, 3.0),
                                 [((0.0, 0.0), (6.0, 6.0))]) == []


@test_case
def test_convert_horizontal_projection():
    # |dx| >= |dy| -> horizontal projection end (end.x, start.y)
    assert convert_projection_end((1.0, 2.0), (6.0, 3.0)) == (6.0, 2.0)


@test_case
def test_convert_vertical_projection():
    # |dx| < |dy| -> vertical projection end (start.x, end.y)
    assert convert_projection_end((1.0, 2.0), (3.0, 7.0)) == (1.0, 7.0)


@test_case
def test_convert_equal_axes_picks_horizontal():
    # |dx| == |dy| uses the >= branch -> horizontal (end.x, start.y)
    assert convert_projection_end((0.0, 0.0), (5.0, 5.0)) == (5.0, 0.0)


@test_case
def test_convert_negative_direction():
    # direction sign does not change the rule
    assert convert_projection_end((6.0, 2.0), (1.0, 3.0)) == (1.0, 2.0)   # horizontal
    assert convert_projection_end((2.0, 7.0), (1.0, 1.0)) == (2.0, 1.0)   # vertical


# --- runner -------------------------------------------------------------------

def main():
    print("verifying geometry replica of AutoDimensionObj.cpp (virtual-line intersections)")
    failures = 0
    for fn in _TESTS:
        try:
            fn()
        except AssertionError as exc:
            failures += 1
            print("FAIL %s: %s" % (fn.__name__, exc))
        except Exception as exc:  # noqa: BLE001 - report any unexpected error
            failures += 1
            print("ERROR %s: %r" % (fn.__name__, exc))
        else:
            print("PASS %s" % fn.__name__)
    print("")
    passed = len(_TESTS) - failures
    print("%d/%d tests passed" % (passed, len(_TESTS)))
    if failures:
        print("%d test(s) FAILED" % failures)
        return 1
    print("ALL GREEN")
    return 0


if __name__ == "__main__":
    sys.exit(main())
