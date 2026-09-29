// Computational geometry used by drawing tools, buoyancy and picking.
#pragma once

#include "Item.h"

#include <array>
#include <vector>

namespace kn::geom {

double signedArea(const Vec2* p, int n);
Vec2 polygonCentroid(const Vec2* p, int n); // area-weighted; falls back to vertex mean
void makeCcw(PointList& p);
// Moment of inertia of a uniform polygon about the origin, per unit density.
double polygonInertiaAboutOrigin(const Vec2* p, int n);

// Ramer–Douglas–Peucker simplification of an open polyline.
PointList simplify(const PointList& pts, double tolerance);
// Removes consecutive points closer than minDist and near-collinear vertices.
PointList cleanPolygon(const PointList& pts, double minDist);
bool isSimplePolygon(const PointList& p);
PointList convexHull(PointList pts);
// Ear-clipping triangulation of a simple CCW polygon.
bool triangulate(const PointList& poly, std::vector<std::array<int, 3>>& tris);
// Convex decomposition (ear clipping followed by Hertel–Mehlhorn merging) into
// pieces with at most maxVerts vertices. Self-intersecting input falls back to
// its convex hull.
std::vector<PointList> convexDecompose(const PointList& poly, int maxVerts = 8);

// Clips a polygon to the half-plane dot(n, p) <= offset. Returns the output
// vertex count; `out` must hold n + 1 vertices.
int clipHalfPlane(const Vec2* in, int n, Vec2 normal, double offset, Vec2* out);

struct AreaMoment {
    double area = 0.0;
    Vec2 centroid;
};
// Area and centroid of the part of a circle below the horizontal line y = level.
AreaMoment circleBelow(Vec2 c, double r, double level);
// Area and centroid of the part of a convex polygon inside the axis-aligned box
// [lo.x, hi.x] x [lo.y, hi.y]. Exact (Sutherland–Hodgman against four planes).
AreaMoment convexInBox(const Vec2* p, int n, Vec2 lo, Vec2 hi);
// Area and centroid of the part of a world-space shape inside the box. Circles
// fully between the box walls use the exact circular-segment formula; other
// circles and capsules are polygonized with 64 edges per full turn.
AreaMoment shapeInBox(const Shape& s, const Pose& pose, Vec2 lo, Vec2 hi);
// Width of the shape's projection onto an axis (unit vector).
double projectedWidth(const Shape& s, const Pose& pose, Vec2 axis);
// Widens [lo, hi] to include the shape's projection onto the axis.
void projectedRange(const Shape& s, const Pose& pose, Vec2 axis, double& lo, double& hi);

double shapeArea(const Shape& s);
Aabb shapeBounds(const Shape& s, const Pose& pose);
bool shapeContains(const Shape& s, Vec2 local, double tolerance);

double distanceToSegment(Vec2 p, Vec2 a, Vec2 b);
bool pointInPolygon(const Vec2* poly, int n, Vec2 p);
// Proper intersection test for segments ab and cd.
bool segmentsIntersect(Vec2 a, Vec2 b, Vec2 c, Vec2 d);
// Regular polygon with `sides` vertices on a circle of radius r (CCW, first vertex at angle a0).
PointList regularPolygon(int sides, double r, double a0 = 0.0);

} // namespace kn::geom
