//
// # Yocto/Dgram intersections: geometry intersection functions
//

//
// LICENSE:
//
// Copyright (c) 2021 -- 2022 Simone Bartolini
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

#ifndef _YOCTO_DGRAM_GEOMETRY_H_
#define _YOCTO_DGRAM_GEOMETRY_H_

// -----------------------------------------------------------------------------
// INCLUDES
// -----------------------------------------------------------------------------

#include "yocto_dgram.h"

// -----------------------------------------------------------------------------
// USING DIRECTIVES
// -----------------------------------------------------------------------------
namespace yocto {

  // using directives

}  // namespace yocto

// -----------------------------------------------------------------------------
// GEOMETRY UTILITIES
// -----------------------------------------------------------------------------
namespace yocto {
  // Interpolates values over a line from p0 to p1 parameterized from a to b by
  // u using perspective correct interpolation. The coordinates p0 and p1 must
  // be in camera coordinates.
  template <typename T>
  inline T perspective_line_interpolation(
      const vec3f& p0, const vec3f& p1, const T& a, const T& b, float u);

  // Interpolates position over a line from p0 to p1 parameterized from p0 to p1
  // by u using perspective correct interpolation. The coordinates p0 and p1
  // must be in camera coordinates.
  inline vec3f perspective_line_point(
      const vec3f& p0, const vec3f& p1, float u);

  // Computes screen-space position using triangles similarity, d is the
  // distance of the image plane from the camera. p must be in camera
  // coordinates.
  inline vec3f screen_space_point(const vec3f& p, const float d);

  // Computes world-space position from screen-space using triangles similarity,
  // d is the distance of the depth of the result point. p must be in camera
  // coordinates.
  inline vec3f world_space_point(const vec3f& p, const float d);
}  // namespace yocto

// -----------------------------------------------------------------------------
// BOUNDS
// -----------------------------------------------------------------------------
namespace yocto {

  inline bbox3f line_bounds(const vec3f& p0, const vec3f& p1, float r0,
      float r1, line_end e0, line_end e1);

}  // namespace yocto

// -----------------------------------------------------------------------------
// INTERSECTIONS
// -----------------------------------------------------------------------------
namespace yocto {

  // Intersect a ray with a point
  inline bool intersect_point(const ray3f& ray, const vec3f& pc, float r,
      vec2f& uv, float& dist, vec3f& pos, vec3f& norm);

  // Intersect a ray with a line
  inline bool intersect_line(const ray3f& ray, const vec3f& p0, const vec3f& p1,
      float r0, float r1, line_end e0, line_end e1, const vec3f& pn0,
      const vec3f& pn1, const vec3f& p45an0, const vec3f& p45an1,
      const vec3f& p45bn0, const vec3f& p45bn1, const vec3f& ap0,
      const vec3f& ap1, float ar0, float ar1, vec2f& uv, float& dist,
      vec3f& pos, vec3f& norm, bool& hit_arrow);
  inline bool intersect_line(const ray3f& ray, const vec3f& p0, const vec3f& p1,
      float r0, float r1, vec2f& uv, float& dist, vec3f& pos, vec3f& norm);

  // Intersect a ray with a triangle
  inline bool intersect_triangle(const ray3f& ray, const vec3f& p0,
      const vec3f& p1, const vec3f& p2, vec2f& uv, float& dist, vec3f& pos,
      vec3f& norm);

  // Intersect a ray with a quad.
  inline bool intersect_quad(const ray3f& ray, const vec3f& p0, const vec3f& p1,
      const vec3f& p2, const vec3f& p3, vec2f& uv, float& dist, vec3f& pos,
      vec3f& norm);

}  // namespace yocto

// -----------------------------------------------------------------------------
//
//
// IMPLEMENTATION
//
//
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// GEOMETRY UTILITIES
// -----------------------------------------------------------------------------
namespace yocto {
  template <typename T>
  inline T perspective_line_interpolation(
      const vec3f& p0, const vec3f& p1, const T& a, const T& b, float u) {
    auto z = 1 / (1 / p0.z + u * (1 / p1.z - 1 / p0.z));
    return z * (a / p0.z + u * (b / p1.z - a / p0.z));
  }

  inline vec3f perspective_line_point(
      const vec3f& p0, const vec3f& p1, float u) {
    return perspective_line_interpolation(p0, p1, p0, p1, u);
  }

  inline vec3f screen_space_point(const vec3f& p, const float d) {
    return vec3f{p.x / p.z * d, p.y / p.z * d, d};
  }

  inline vec3f world_space_point(const vec3f& p, const float d) {
    return vec3f{p.x / p.z * d, p.y / p.z * d, d};
  }
}  // namespace yocto

// -----------------------------------------------------------------------------
// BOUNDS
// -----------------------------------------------------------------------------
namespace yocto {

  inline bbox3f line_bounds(const vec3f& p0, const vec3f& p1, float r0,
      float r1, line_end e0, line_end e1) {
    auto pa = p0;
    auto ra = r0;
    auto ea = e0;
    auto pb = p1;
    auto rb = r1;
    auto eb = e1;

    if (r1 < r0) {
      pa = p1;
      ra = r1;
      ea = e1;
      pb = p0;
      rb = r0;
      eb = e0;
    }

    auto dir = normalize(pb - pa);     // The direction of the line
    auto l   = distance(pb, pa);       // Distance between the two ends
    auto oa  = ra * l / (rb - ra);     // Distance of the apex of the cone
                                       // along the cone's axis, from pa
    auto ob = oa + l;                  // Distance of the apex of the cone
                                       // along the cone's axis, from pb
    auto tga   = (rb - ra) / l;        // Tangent of Cone's angle
    auto cosa2 = 1 / (1 + tga * tga);  // Consine^2 of Cone's angle

    if (cosa2 > 0.999999) {
      ra = (r0 + r1) / 2;
      rb = ra;
    }

    auto rac = ra;
    auto rbc = rb;
    auto pac = pa;
    auto pbc = pb;

    // Computing ends' parameters
    if (ea == line_end::stealth_arrow) {
      rac = ra * 4;
    } else if (ea == line_end::triangle_arrow) {
      rac = ra * 8 / 3;
    }

    if (eb == line_end::stealth_arrow) {
      rbc = rb * 4;
    } else if (eb == line_end::triangle_arrow) {
      rbc = rb * 8 / 3;
    }

    if (ra != rb) {  // Cone
      auto cosa = sqrt(ob * ob - rb * rb) / ob;

      // Computing ends' parameters
      if (ea == line_end::cap) {
        rac = ra / cosa;
        pac = pa + dir * (tga * rac);
      }

      if (eb == line_end::cap) {
        rbc = rb / cosa;
        pbc = pb + dir * (tga * rbc);
      }
    }

    return {min(pac - rac, pbc - rbc), max(pac + rac, pbc + rbc)};
  }

}  // namespace yocto

// -----------------------------------------------------------------------------
// INTERSECTIONS
// -----------------------------------------------------------------------------
namespace yocto {

  inline bool solve_quadratic(
      const float& a, const float& b, const float& c, float& x0, float& x1) {
    // https://www.scratchapixel.com/lessons/3d-basic-rendering/minimal-ray-tracer-rendering-simple-shapes/ray-sphere-intersection
    float discr = b * b - 4 * a * c;
    if (discr < 0)
      return false;
    else if (discr == 0)
      x0 = x1 = -0.5 * b / a;
    else {
      float q = (b > 0) ? -0.5 * (b + sqrt(discr)) : -0.5 * (b - sqrt(discr));
      x0      = q / a;
      x1      = c / q;
    }

    return true;
  }

  // Intersect a ray with a point
  inline bool intersect_point(const ray3f& ray, const vec3f& pc, float r,
      vec2f& uv, float& dist, vec3f& pos, vec3f& norm) {
    auto a = dot(ray.d, ray.d);
    auto b = 2 * dot(ray.d, ray.o - pc);
    auto c = dot(ray.o - pc, ray.o - pc) - r * r;

    auto t1 = 0.0f;
    auto t2 = 0.0f;

    if (!solve_quadratic(a, b, c, t1, t2)) return false;

    auto hit = false;
    auto t   = ray.tmax;
    if (t1 >= ray.tmin && t1 <= t) {
      t   = t1;
      hit = true;
    }

    if (t2 >= ray.tmin && t2 <= t) {
      t   = t2;
      hit = true;
    }

    if (!hit) return false;

    auto p = ray.o + t * ray.d;
    auto n = normalize(p - pc);
    auto u = (pif - atan2(n.z, n.x)) / (2 * pif);
    auto v = (pif - 2 * asin(n.y)) / (2 * pif);

    // intersection occurred: set params and exit
    uv   = {u, v};
    dist = t;
    pos  = p;
    norm = n;
    return true;
  }

  inline bool intersect_cylinder(const ray3f& ray, const vec3f& p0,
      const vec3f& p1, float r, const vec3f& dir, float& dist, vec3f& pos,
      vec3f& norm) {
    auto av = ray.d - dot(ray.d, dir) * dir;
    auto a  = dot(av, av);
    auto dp = ray.o - p0;
    auto b  = 2 * dot(ray.d - dot(ray.d, dir) * dir, dp - dot(dp, dir) * dir);
    auto cv = dp - dot(dp, dir) * dir;
    auto c  = dot(cv, cv) - r * r;

    auto t1 = 0.0f;
    auto t2 = 0.0f;

    if (!solve_quadratic(a, b, c, t1, t2)) return false;

    auto q1 = ray.o + t1 * ray.d;
    auto q2 = ray.o + t2 * ray.d;

    auto hit = false;
    auto t   = dist;
    auto p   = vec3f{0, 0, 0};

    if (t1 >= ray.tmin && t1 <= t && dot(dir, q1 - p0) > 0 &&
        dot(dir, q1 - p1) < 0) {
      t   = t1;
      p   = q1;
      hit = true;
    }

    if (t2 >= ray.tmin && t2 <= t && dot(dir, q2 - p0) > 0 &&
        dot(dir, q2 - p1) < 0) {
      t   = t2;
      p   = q2;
      hit = true;
    }

    if (!hit) return false;

    auto d  = dot(p - p0, dir);
    auto pt = p0 + d * dir;
    auto n  = normalize(p - pt);

    // intersection occurred: set params and exit
    dist = t;
    pos  = p;
    norm = n;
    return true;
  }

  inline bool intersect_cone(const ray3f& ray, const vec3f& p0, const vec3f& p1,
      float r0, float r1, const vec3f& dir, float& dist, vec3f& pos,
      vec3f& norm) {
    auto ab    = distance(p1, p0);  // Distance between the two ends
    auto pc    = p0 - dir * r0 * ab / (r1 - r0);  // Cone's apex point
    auto tga   = (r1 - r0) / ab;
    auto cosa2 = 1 / (1 + tga * tga);

    auto co = ray.o - pc;

    auto a = dot(ray.d, dir) * dot(ray.d, dir) - cosa2;
    auto b = 2 * (dot(ray.d, dir) * dot(co, dir) - dot(ray.d, co) * cosa2);
    auto c = dot(co, dir) * dot(co, dir) - dot(co, co) * cosa2;

    auto t1 = 0.0f;
    auto t2 = 0.0f;

    if (!solve_quadratic(a, b, c, t1, t2)) return false;

    auto q1 = ray.o + t1 * ray.d;
    auto q2 = ray.o + t2 * ray.d;

    auto hit = false;
    auto t   = dist;
    auto p   = vec3f{0, 0, 0};

    if (t1 >= ray.tmin && t1 <= t && dot(dir, q1 - p0) > 0 &&
        dot(dir, q1 - p1) < 0) {
      t   = t1;
      p   = q1;
      hit = true;
    }

    if (t2 >= ray.tmin && t2 <= t && dot(dir, q2 - p0) > 0 &&
        dot(dir, q2 - p1) < 0) {
      t   = t2;
      p   = q2;
      hit = true;
    }

    if (!hit) return false;

    auto ppc = p - pc;
    auto n   = normalize(ppc * dot(dir, ppc) / dot(ppc, ppc) - dir);

    // intersection occurred: set params and exit
    dist = t;
    pos  = p;
    norm = n;
    return true;
  }

  inline bool intersect_arrow(const ray3f& ray, const vec3f& p0,
      const vec3f& p1, float r, const vec3f& dir, const vec3f& pn0,
      const vec3f& pn1, float& dist, vec3f& pos, vec3f& norm) {
    auto ab    = distance(p1, p0);
    auto tga   = r / ab;
    auto cosa2 = 1 / (1 + tga * tga);

    auto co = ray.o - p0;

    auto a = dot(ray.d, dir) * dot(ray.d, dir) - cosa2;
    auto b = 2 * (dot(ray.d, dir) * dot(co, dir) - dot(ray.d, co) * cosa2);
    auto c = dot(co, dir) * dot(co, dir) - dot(co, co) * cosa2;

    auto t1 = 0.0f;
    auto t2 = 0.0f;

    if (!solve_quadratic(a, b, c, t1, t2)) return false;

    auto q1 = ray.o + t1 * ray.d;
    auto q2 = ray.o + t2 * ray.d;

    auto hit = false;
    auto t   = dist;
    auto p   = vec3f{0, 0, 0};

    if (t1 >= ray.tmin && t1 <= t && dot(dir, q1 - p0) > 0 &&
        (dot(pn0, q1 - p1) < 0 || dot(pn1, q1 - p1) < 0)) {
      t   = t1;
      p   = q1;
      hit = true;
    }

    if (t2 >= ray.tmin && t2 <= t && dot(dir, q2 - p0) > 0 &&
        (dot(pn0, q2 - p1) < 0 || dot(pn1, q2 - p1) < 0)) {
      t   = t2;
      p   = q2;
      hit = true;
    }

    if (!hit) return false;

    auto cp = p - p0;
    auto n  = normalize(cp * dot(dir, cp) / dot(cp, cp) - dir);

    // intersection occurred: set params and exit
    dist = t;
    pos  = p;
    norm = n;
    return true;
  }

  inline bool intersect_cap(const ray3f& ray, const vec3f& pl, const vec3f& pc,
      float r, const vec3f& dir, float& dist, vec3f& pos, vec3f& norm) {
    auto a = dot(ray.d, ray.d);
    auto b = 2 * dot(ray.d, ray.o - pc);
    auto c = dot(ray.o - pc, ray.o - pc) - r * r;

    auto t1 = 0.0f;
    auto t2 = 0.0f;

    if (!solve_quadratic(a, b, c, t1, t2)) return false;

    auto p1 = ray.o + t1 * ray.d;
    auto p2 = ray.o + t2 * ray.d;

    auto hit = false;
    auto t   = dist;
    auto p   = vec3f{0, 0, 0};

    if (t1 >= ray.tmin && t1 <= t && dot(p1 - pl, dir) < 0) {
      t   = t1;
      p   = p1;
      hit = true;
    }

    if (t2 >= ray.tmin && t2 <= t && dot(p2 - pl, dir) < 0) {
      t   = t2;
      p   = p2;
      hit = true;
    }

    if (!hit) return false;

    auto n = normalize(p - pc);

    // intersection occurred: set params and exit
    dist = t;
    pos  = p;
    norm = n;
    return true;
  }

  // Intersect a ray with a line
  inline bool intersect_line(const ray3f& ray, const vec3f& p0, const vec3f& p1,
      float r0, float r1, line_end e0, line_end e1, const vec3f& pn0,
      const vec3f& pn1, const vec3f& p45an0, const vec3f& p45an1,
      const vec3f& p45bn0, const vec3f& p45bn1, const vec3f& ap0,
      const vec3f& ap1, float ar0, float ar1, vec2f& uv, float& dist,
      vec3f& pos, vec3f& norm, bool& hit_arrow) {
    if (p0 == p1) return false;

    auto pa     = p0;
    auto ra     = r0;
    auto ea     = e0;
    auto pb     = p1;
    auto rb     = r1;
    auto eb     = e1;
    auto pna    = pn0;
    auto pnb    = pn1;
    auto p45ana = p45an0;
    auto p45anb = p45an1;
    auto p45bna = p45bn0;
    auto p45bnb = p45bn1;
    auto raa    = ar0;
    auto rba    = ar1;
    auto paa    = ap0;
    auto pba    = ap1;

    if (r1 < r0) {
      pa     = p1;
      ra     = r1;
      ea     = e1;
      pb     = p0;
      rb     = r0;
      eb     = e0;
      pna    = pn1;
      pnb    = pn0;
      p45ana = p45an1;
      p45anb = p45an0;
      p45bna = p45bn1;
      p45bnb = p45bn0;
      raa    = ar1;
      rba    = ar0;
      paa    = ap1;
      pba    = ap0;
    }

    auto dir = normalize(pb - pa);     // The direction of the line
    auto l   = distance(pb, pa);       // Distance between the two ends
    auto oa  = ra * l / (rb - ra);     // Distance of the apex of the cone
                                       // along the cone's axis, from pa
    auto ob = oa + l;                  // Distance of the apex of the cone
                                       // along the cone's axis, from pb
    auto tga   = (rb - ra) / l;        // Tangent of Cone's angle
    auto cosa2 = 1 / (1 + tga * tga);  // Consine^2 of Cone's angle

    if (cosa2 > 0.999999) {
      ra = (r0 + r1) / 2;
      rb = ra;
    }

    hit_arrow = false;
    auto hit  = false;
    auto t    = ray.tmax;
    auto p    = vec3f{0, 0, 0};
    auto n    = vec3f{0, 0, 0};

    auto rac = ra;
    auto rbc = rb;
    auto pac = pa;
    auto pbc = pb;

    if (ra == rb) {
      hit += intersect_cylinder(ray, pa, pb, ra, dir, t, p, n);
    } else {
      auto cosa = sqrt(ob * ob - rb * rb) / ob;

      // Computing ends' parameters
      if (ea == line_end::cap) {
        rac = ra / cosa;
        pac = pa + dir * (tga * rac);
      }

      if (eb == line_end::cap) {
        rbc = rb / cosa;
        pbc = pb + dir * (tga * rbc);
      }

      hit += intersect_cone(ray, pa, pb, ra, rb, dir, t, p, n);
    }

    if (ea != line_end::cap && dot(p - paa, pna) < 0) {
      hit = false;
      t   = ray.tmax;
    }
    if (eb != line_end::cap && dot(p - pba, pnb) < 0) {
      hit = false;
      t   = ray.tmax;
    }

    if (ea == line_end::cap) {
      if (intersect_cap(ray, pa, pac, rac, dir, t, p, n)) {
        hit       = true;
        hit_arrow = false;
      }
    } else if (ea == line_end::triangle_arrow) {
      if (intersect_arrow(ray, pa, paa, raa, dir, pna, pna, t, p, n)) {
        hit       = true;
        hit_arrow = true;
      }
    } else {  // stealth_arrow
      if (intersect_arrow(ray, pa, paa, raa, dir, p45ana, p45bna, t, p, n)) {
        hit       = true;
        hit_arrow = true;
      }
    }

    if (eb == line_end::cap) {
      if (intersect_cap(ray, pb, pbc, rbc, -dir, t, p, n)) {
        hit       = true;
        hit_arrow = false;
      }
    } else if (eb == line_end::triangle_arrow) {
      if (intersect_arrow(ray, pb, pba, rba, -dir, pnb, pnb, t, p, n)) {
        hit       = true;
        hit_arrow = true;
      }
    } else {  // stealth_arrow
      if (intersect_arrow(ray, pb, pba, rba, -dir, p45anb, p45bnb, t, p, n)) {
        hit       = true;
        hit_arrow = true;
      }
    }

    if (!hit) return false;

    auto d  = dot(p - p0, normalize(p1 - p0));
    auto pt = p0 + d * normalize(p1 - p0);
    auto u  = clamp(sign(d) * distance(pt, p0) / l, 0.0f, 1.0f);

    // intersection occurred: set params and exit
    uv   = {u, 0};
    dist = t;
    pos  = p;
    norm = n;
    return true;
  }

  inline bool intersect_line(const ray3f& ray, const vec3f& p0, const vec3f& p1,
      float r0, float r1, vec2f& uv, float& dist, vec3f& pos, vec3f& norm) {
    if (p0 == p1) return false;

    auto pa = p0;
    auto pb = p1;
    auto ra = r0;
    auto rb = r1;

    if (r1 < r0) {
      pa = p1;
      pb = p0;
      ra = r1;
      rb = r0;
    }

    auto dir = normalize(pb - pa);     // The direction of the line
    auto l   = distance(pb, pa);       // Distance between the two ends
    auto oa  = ra * l / (rb - ra);     // Distance of the apex of the cone
                                       // along the cone's axis, from pa
    auto ob = oa + l;                  // Distance of the apex of the cone
                                       // along the cone's axis, from pb
    auto tga   = (rb - ra) / l;        // Tangent of Cone's angle
    auto cosa2 = 1 / (1 + tga * tga);  // Consine^2 of Cone's angle

    if (cosa2 > 0.999999) {
      ra = (r0 + r1) / 2;
      rb = ra;
    }

    auto rac = ra;
    auto rbc = rb;
    auto pac = pa;
    auto pbc = pb;

    auto hit = false;
    auto t   = ray.tmax;
    auto p   = vec3f{0, 0, 0};
    auto n   = vec3f{0, 0, 0};

    if (ra == rb) {
      hit += intersect_cylinder(ray, pa, pb, ra, dir, t, p, n);
    } else {
      auto cosa = sqrt(ob * ob - rb * rb) / ob;

      // Computing ends' parameters
      rac = ra / cosa;
      pac = pa + dir * (tga * rac);

      rbc = rb / cosa;
      pbc = pb + dir * (tga * rbc);

      hit += intersect_cone(ray, pa, pb, ra, rb, dir, t, p, n);
    }

    hit += intersect_cap(ray, pa, pac, rac, dir, t, p, n);
    hit += intersect_cap(ray, pb, pbc, rbc, -dir, t, p, n);

    if (!hit) return false;

    auto d  = dot(p - p0, normalize(p1 - p0));
    auto pt = p0 + d * normalize(p1 - p0);
    auto u  = clamp(sign(d) * distance(pt, p0) / l, 0.0f, 1.0f);

    // intersection occurred: set params and exit
    uv   = {u, 0};
    dist = t;
    pos  = p;
    norm = n;
    return true;
  }

  // Intersect a ray with a triangle
  inline bool intersect_triangle(const ray3f& ray, const vec3f& p0,
      const vec3f& p1, const vec3f& p2, vec2f& uv, float& dist, vec3f& pos,
      vec3f& norm) {
    // compute triangle edges
    auto edge1 = p1 - p0;
    auto edge2 = p2 - p0;

    // compute determinant to solve a linear system
    auto pvec = cross(ray.d, edge2);
    auto det  = dot(edge1, pvec);

    // check determinant and exit if triangle and ray are parallel
    // (could use EPSILONS if desired)
    if (det == 0) return false;
    auto inv_det = 1.0f / det;

    // compute and check first bricentric coordinated
    auto tvec = ray.o - p0;
    auto u    = dot(tvec, pvec) * inv_det;
    if (u < 0 || u > 1) return false;

    // compute and check second bricentric coordinated
    auto qvec = cross(tvec, edge1);
    auto v    = dot(ray.d, qvec) * inv_det;
    if (v < 0 || u + v > 1) return false;

    // compute and check ray parameter
    auto t = dot(edge2, qvec) * inv_det;
    if (t < ray.tmin || t > ray.tmax) return false;

    // intersection occurred: set params and exit
    uv   = {u, v};
    dist = t;
    pos  = ray.o + t * ray.d;
    norm = normalize(cross(edge1, edge2));
    return true;
  }

  // Intersect a ray with a quad.
  inline bool intersect_quad(const ray3f& ray, const vec3f& p0, const vec3f& p1,
      const vec3f& p2, const vec3f& p3, vec2f& uv, float& dist, vec3f& pos,
      vec3f& norm) {
    if (p2 == p3) {
      return intersect_triangle(ray, p0, p1, p3, uv, dist, pos, norm);
    }
    auto hit  = false;
    auto tray = ray;
    if (intersect_triangle(tray, p0, p1, p3, uv, dist, pos, norm)) {
      hit       = true;
      tray.tmax = dist;
    }
    if (intersect_triangle(tray, p2, p3, p1, uv, dist, pos, norm)) {
      hit       = true;
      uv        = 1 - uv;
      tray.tmax = dist;
    }
    return hit;
  }

}  // namespace yocto

#endif