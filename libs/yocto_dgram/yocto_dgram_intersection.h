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

#ifndef _YOCTO_DGRAM_INTERSECTIONS_H_
#define _YOCTO_DGRAM_INTERSECTIONS_H_

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
  inline bool intersect_point(const ray3f& ray, const vec3f& p, float r,
      vec2f& uv, float& dist, vec3f& pos, vec3f& norm);

  // Intersect a ray with a line
  inline bool intersect_line(const ray3f& ray, const vec3f& p0, const vec3f& p1,
      float r0, float r1, line_end e0, line_end e1, const vec3f& ad,
      const vec3f& ap0, const vec3f& ap1, float ar0, float ar1, vec2f& uv,
      float& dist, vec3f& pos, vec3f& norm);
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

    auto dir = normalize(pb - pa);  // The direction of the line
    auto l   = distance(pb, pa);    // Distance between the two ends
    auto oa  = ra * l / (rb - ra);  // Distance of the apex of the cone
                                    // along the cone's axis, from pa
    auto ob = oa + l;               // Distance of the apex of the cone
                                    // along the cone's axis, from pb
    auto o   = pa - dir * oa;       // Cone's apex point
    auto tga = ((double)rb - (double)ra) /
               (double)l;              // Tangent of Cone's angle
    auto cosa2 = 1 / (1 + tga * tga);  // Consine^2 of Cone's angle

    if (cosa2 > 0.999995) {
      ra = (r0 + r1) / 2;
      rb = ra;
    }

    auto rac = ra;
    auto rbc = rb;
    auto pac = pa;
    auto pbc = pb;

    // Computing ends' parameters
    if (ea == line_end::arrow) {
      pa  = pa + 6 * ra * dir;
      rac = ra * 3;
    }
    if (eb == line_end::arrow) {
      pb  = pb - 6 * rb * dir;
      rbc = rb * 3;
    }

    if (ra != rb) {  // Cone
      auto cosa = sqrt(ob * ob - rb * rb) / ob;

      // Computing ends' parameters
      if (ea == line_end::cap) {
        rac = ra / cosa;
        pac = o + dir * ((double)oa + (double)tga * (double)rac);
      }

      if (eb == line_end::cap) {
        rbc = rb / cosa;
        pbc = o + dir * ((double)ob + (double)tga * (double)rbc);
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

  inline bool solve_quadratic(const double& a, const double& b, const double& c,
      double& x0, double& x1) {
    // https://www.scratchapixel.com/lessons/3d-basic-rendering/minimal-ray-tracer-rendering-simple-shapes/ray-sphere-intersection
    double discr = b * b - 4 * a * c;
    if (discr < 0)
      return false;
    else if (discr == 0)
      x0 = x1 = -0.5 * b / a;
    else {
      double q = (b > 0) ? -0.5 * (b + sqrt(discr)) : -0.5 * (b - sqrt(discr));
      x0       = q / a;
      x1       = c / q;
    }

    return true;
  }

  // Intersect a ray with a point
  inline bool intersect_point(const ray3f& ray, const vec3f& p, float r,
      vec2f& uv, float& dist, vec3f& pos, vec3f& norm) {
    auto a = dot(ray.d, ray.d);
    auto b = 2 * dot(ray.d, ray.o - p);
    auto c = dot(ray.o - p, ray.o - p) - r * r;

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

    // compute local point for uvs
    auto pl = ray.o + t * ray.d;
    auto n  = normalize(pl - p);
    auto u  = (pif - atan2(n.z, n.x)) / (2 * pif);
    auto v  = (pif - 2 * asin(n.y)) / (2 * pif);

    // intersection occurred: set params and exit
    uv   = {u, v};
    dist = t;
    pos  = pl;
    norm = n;
    return true;
  }

  inline bool intersect_cylinder(const ray3f& ray, const vec3f& p0,
      const vec3f& p1, float r, const vec3f& dir, const frame3f& frame,
      const int sign, vec2f& uv, float& dist, vec3f& pos, vec3f& norm) {
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

    auto tn = transform_normal(frame, n);
    auto u  = (pif - atan2(tn.y, tn.x)) / (2 * pif);
    auto v  = distance(pt, p1) / distance(p0, p1);
    if (sign < 0) v = 1 - v;

    // intersection occurred: set params and exit
    uv   = {u, v};
    dist = t;
    pos  = p;
    norm = n;
    return true;
  }

  inline bool intersect_cone(const ray3f& ray, const vec3f& p0, const vec3f& p1,
      float r0, float r1, const vec3f& dir, const frame3f& frame,
      const int sign, vec2f& uv, float& dist, vec3f& pos, vec3f& norm) {
    auto ab = distance(p1, p0);    // Distance between the two ends
    auto o = r0 * ab / (r1 - r0);  // Distance of the apex of the cone along the
                                   // cone's axis, from p0
    auto pa    = p0 - dir * o;     // Cone's apex point
    auto tga   = ((double)r1 - (double)r0) / (double)ab;
    auto cosa2 = 1 / (1 + tga * tga);

    auto co = ray.o - pa;

    auto a = (double)dot(ray.d, dir) * (double)dot(ray.d, dir) - cosa2;
    auto b = 2 * ((double)dot(ray.d, dir) * (double)dot(co, dir) -
                     (double)dot(ray.d, co) * cosa2);
    auto c = (double)dot(co, dir) * (double)dot(co, dir) -
             (double)dot(co, co) * cosa2;

    auto t1 = 0.0;
    auto t2 = 0.0;

    if (!solve_quadratic(a, b, c, t1, t2)) return false;

    auto q1 = ray.o + (float)t1 * ray.d;
    auto q2 = ray.o + (float)t2 * ray.d;

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

    auto cp = p - pa;
    auto n  = normalize(cp * dot(dir, cp) / dot(cp, cp) - dir);

    auto d  = dot(p - p0, dir);
    auto pt = p0 + d * dir;

    auto tn = transform_normal(frame, n);
    auto u  = (pif - atan2(tn.y, tn.x)) / (2 * pif);
    auto v  = distance(pt, p1) / distance(p0, p1);
    if (sign < 0) v = 1 - v;

    // intersection occurred: set params and exit
    uv   = {u, v};
    dist = t;
    pos  = p;
    norm = n;
    return true;
  }

  inline bool intersect_arrow(const ray3f& ray, const vec3f& p0,
      const vec3f& p1, float r, const vec3f& dir, const frame3f& frame,
      const int sign, vec2f& uv, float& dist, vec3f& pos, vec3f& norm) {
    auto ab    = distance(p1, p0);
    auto tga   = ((double)r) / (double)ab;
    auto cosa2 = 1 / (1 + tga * tga);

    auto co = ray.o - p0;

    auto a = (double)dot(ray.d, dir) * (double)dot(ray.d, dir) - cosa2;
    auto b = 2 * ((double)dot(ray.d, dir) * (double)dot(co, dir) -
                     (double)dot(ray.d, co) * cosa2);
    auto c = (double)dot(co, dir) * (double)dot(co, dir) -
             (double)dot(co, co) * cosa2;

    auto t1 = 0.0;
    auto t2 = 0.0;

    if (!solve_quadratic(a, b, c, t1, t2)) return false;

    auto q1 = ray.o + (float)t1 * ray.d;
    auto q2 = ray.o + (float)t2 * ray.d;

    auto hit = false;
    auto t   = dist;
    auto p   = vec3f{0, 0, 0};

    if (t1 >= ray.tmin && t1 <= t && dot(dir, q1 - p0) > 0) {
      t   = t1;
      p   = q1;
      hit = true;
    }

    if (t2 >= ray.tmin && t2 <= t && dot(dir, q2 - p0) > 0) {
      t   = t2;
      p   = q2;
      hit = true;
    }

    if (!hit) return false;

    auto cp = p - p0;
    auto n  = normalize(cp * dot(dir, cp) / dot(cp, cp) - dir);

    auto d  = dot(p - p0, dir);
    auto pt = p0 + d * dir;

    auto tn = transform_normal(frame, n);
    auto u  = (pif - atan2(tn.y, tn.x)) / (2 * pif);
    auto v  = distance(pt, p1) / distance(p0, p1);
    if (sign < 0) v = 1 - v;

    // intersection occurred: set params and exit
    uv   = {u, v};
    dist = t;
    pos  = p;
    norm = n;
    return true;
  }

  inline bool intersect_cap(const ray3f& ray, const vec3f& pa, const vec3f& pb,
      const vec3f& pc, float r, const vec3f& dir, const frame3f& frame,
      const int sign, vec2f& uv, float& dist, vec3f& pos, vec3f& norm) {
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

    if (t1 >= ray.tmin && t1 <= t && dot(p1 - pa, dir) < 0) {
      t   = t1;
      p   = p1;
      hit = true;
    }

    if (t2 >= ray.tmin && t2 <= t && dot(p2 - pa, dir) < 0) {
      t   = t2;
      p   = p2;
      hit = true;
    }

    if (!hit) return false;

    auto n = normalize(p - pc);

    auto tn = transform_normal(frame, p - pa);
    auto u  = (pif - atan2(tn.y, tn.x)) / (2 * pif);
    auto v  = (pif - 2 * asin(tn.z)) / (2 * pif);
    if (transform_direction(frame, pb - pa).z < 0) v = 1 - v;
    if (sign < 0) v = 1 - v;

    // intersection occurred: set params and exit
    uv   = {u, v};
    dist = t;
    pos  = p;
    norm = n;
    return true;
  }

  inline bool intersect_disk(const ray3f& ray, const vec3f& p, float r1,
      float r2, const vec3f& n, const frame3f& frame, const int sign, vec2f& uv,
      float& dist, vec3f& pos, vec3f& norm) {
    auto o = ray.o - p;

    auto den = dot(ray.d, n);
    if (den == 0) return false;

    auto t = -dot(n, o) / den;

    auto q  = o + ray.d * t;
    auto q2 = sqrt(dot(q, q));
    if (q2 <= r1 || q2 >= r2) return false;

    if (t < ray.tmin || t > dist) return false;

    auto d = normalize(ray.o + ray.d * t - p);

    auto tn = transform_normal(frame, d);
    auto u  = (pif - atan2(tn.y, tn.x)) / (2 * pif);
    auto v  = (q2 - r1) / (r2 - r1);
    if (sign < 0) v = 1 - v;

    // intersection occurred: set params and exit
    uv   = {u, v};
    dist = t;
    pos  = ray.o + t * ray.d;
    norm = n;
    return true;
  }

  inline static bool intersect_arrow_plane(
      const ray3f& ray, const vec3f& p, const vec3f& dir) {
    auto o   = ray.o - p;
    auto den = dot(ray.d, ray.d);
    auto t   = -dot(ray.d, o) / den;
    auto q   = ray.o + ray.d * t;

    if (dot(dir, q - p) < 0) return true;

    return false;
  }

  // Intersect a ray with a line
  inline bool intersect_line(const ray3f& ray, const vec3f& p0, const vec3f& p1,
      float r0, float r1, line_end e0, line_end e1, const vec3f& ad,
      const vec3f& ap0, const vec3f& ap1, float ar0, float ar1, vec2f& uv,
      float& dist, vec3f& pos, vec3f& norm) {
    // TODO(simone): cleanup

    if (p0 == p1) return false;

    auto pa   = p0;
    auto ra   = r0;
    auto ea   = e0;
    auto pb   = p1;
    auto rb   = r1;
    auto eb   = e1;
    auto sign = 1;
    auto adir = ad;
    auto raa  = ar0;
    auto rba  = ar1;
    auto paa  = ap0;
    auto pba  = ap1;

    if (r1 < r0) {
      pa   = p1;
      ra   = r1;
      ea   = e1;
      pb   = p0;
      rb   = r0;
      eb   = e0;
      sign = -1;
      adir = -ad;
      raa  = ar1;
      rba  = ar0;
      paa  = ap1;
      pba  = ap0;
    }

    auto dir = normalize(pb - pa);  // The direction of the line
    auto l   = distance(pb, pa);    // Distance between the two ends
    auto oa  = ra * l / (rb - ra);  // Distance of the apex of the cone
                                    // along the cone's axis, from pa
    auto ob = oa + l;               // Distance of the apex of the cone
                                    // along the cone's axis, from pb
    auto o   = pa - dir * oa;       // Cone's apex point
    auto tga = ((double)rb - (double)ra) /
               (double)l;              // Tangent of Cone's angle
    auto cosa2 = 1 / (1 + tga * tga);  // Consine^2 of Cone's angle

    if (cosa2 > 0.999995) {
      ra = (r0 + r1) / 2;
      rb = ra;
    }

    auto hit = false;
    auto t   = ray.tmax;
    auto p   = vec3f{0, 0, 0};
    auto n   = vec3f{0, 0, 0};
    auto tuv = vec2f{0, 0};

    auto rac = ra;
    auto rbc = rb;
    auto pac = pa;
    auto pbc = pb;

    auto pap = pa + 6 * ra * adir;
    auto pbp = pb - 6 * rb * adir;

    auto frame = frame_fromz({0, 0, 0}, normalize(p1 - p0));
    frame.y    = -frame.y;
    frame.x    = frame.z.z < 0 ? frame.x : -frame.x;

    auto la = 0.0f;
    auto lc = distance(pa, pb);
    auto lb = 0.0f;

    if (ea == line_end::cap) {
      la = sqrt(ra * ra + rac * rac);
    }

    if (eb == line_end::cap) {
      lb = sqrt(rb * rb + rbc * rbc);
    }

    if ((ea == line_end::cap || !intersect_arrow_plane(ray, pap, adir)) &&
        (eb == line_end::cap || !intersect_arrow_plane(ray, pbp, -adir))) {
      if (ra == rb) {
        if (intersect_cylinder(
                ray, pa, pb, ra, dir, frame, sign, tuv, t, p, n)) {
          if (sign > 0) {
            tuv = {tuv.x, (lb + tuv.y * lc) / (lb + lc + la)};
          } else {
            tuv = {tuv.x, (la + tuv.y * lc) / (lb + lc + la)};
          }
          hit = true;
        }
      } else {
        auto cosa = sqrt(ob * ob - rb * rb) / ob;

        // Computing ends' parameters
        if (ea == line_end::cap) {
          rac = ra / cosa;
          pac = o + dir * ((double)oa + (double)tga * (double)rac);
          la  = sqrt(ra * ra + rac * rac);
        }

        if (eb == line_end::cap) {
          rbc = rb / cosa;
          pbc = o + dir * ((double)ob + (double)tga * (double)rbc);
          lb  = sqrt(rb * rb + rbc * rbc);
        }

        if (intersect_cone(
                ray, pa, pb, ra, rb, dir, frame, sign, tuv, t, p, n)) {
          if (sign > 0) {
            tuv = {tuv.x, (lb + tuv.y * lc) / (lb + lc + la)};
          } else {
            tuv = {tuv.x, (la + tuv.y * lc) / (lb + lc + la)};
          }
          hit = true;
        }
      }
    }

    if (ea == line_end::cap) {
      if (intersect_cap(
              ray, pa, pb, pac, rac, dir, frame, sign, tuv, t, p, n)) {
        if (sign > 0) {
          tuv = {tuv.x, (lb + lc + 2 * (tuv.y - 0.5f) * la) / (lb + lc + la)};
        } else {
          tuv = {tuv.x, (2 * tuv.y * la) / (lb + lc + la)};
        }
        hit = true;
      }

    } else {
      if (intersect_arrow_plane(ray, pap, adir) &&
          intersect_arrow(ray, pa, paa, raa, dir, frame, sign, tuv, t, p, n)) {
        auto laa = distance(pa, paa);
        if (sign > 0) {
          tuv = {tuv.x, (lb + lc - (1 - tuv.y) * laa) / (lb + lc + la)};
        } else {
          tuv = {tuv.x, (tuv.y * laa) / (lb + lc + la)};
        }
        hit = true;
      }
    }

    if (eb == line_end::cap) {
      if (intersect_cap(
              ray, pb, pa, pbc, rbc, -dir, frame, -sign, tuv, t, p, n)) {
        if (sign > 0) {
          tuv = {tuv.x, (2 * tuv.y * lb) / (lb + lc + la)};
        } else {
          tuv = {tuv.x, (la + lc + 2 * (tuv.y - 0.5f) * lb) / (lb + lc + la)};
        }
        hit = true;
      }
    } else {
      if (intersect_arrow_plane(ray, pbp, -adir) &&
          intersect_arrow(
              ray, pb, pba, rba, -dir, frame, -sign, tuv, t, p, n)) {
        auto lba = distance(pb, pba);
        if (sign > 0) {
          tuv = {tuv.x, (tuv.y * lba) / (lb + lc + la)};
        } else {
          tuv = {tuv.x, (la + lc - (1 - tuv.y) * lba) / (lb + lc + la)};
        }
        hit = true;
      }
    }

    if (!hit) return false;

    // intersection occurred: set params and exit
    uv   = tuv;
    dist = t;
    pos  = p;
    norm = n;
    return true;
  }

  inline bool intersect_line(const ray3f& ray, const vec3f& p0, const vec3f& p1,
      float r0, float r1, vec2f& uv, float& dist, vec3f& pos, vec3f& norm) {
    if (p0 == p1) return false;

    auto pa   = p0;
    auto pb   = p1;
    auto ra   = r0;
    auto rb   = r1;
    auto sign = 1;

    if (r1 < r0) {
      pa   = p1;
      pb   = p0;
      ra   = r1;
      rb   = r0;
      sign = -1;
    }

    auto dir = normalize(pb - pa);  // The direction of the line
    auto l   = distance(pb, pa);    // Distance between the two ends
    auto oa  = ra * l / (rb - ra);  // Distance of the apex of the cone
                                    // along the cone's axis, from pa
    auto ob = oa + l;               // Distance of the apex of the cone
                                    // along the cone's axis, from pb
    auto o   = pa - dir * oa;       // Cone's apex point
    auto tga = ((double)rb - (double)ra) /
               (double)l;              // Tangent of Cone's angle
    auto cosa2 = 1 / (1 + tga * tga);  // Consine^2 of Cone's angle

    if (cosa2 > 0.999995) {
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
    auto tuv = vec2f{0, 0};

    auto frame = frame_fromz({0, 0, 0}, normalize(p1 - p0));
    frame.y    = -frame.y;
    frame.x    = frame.z.z < 0 ? frame.x : -frame.x;

    auto la = sqrt(ra * ra + rac * rac);
    auto lc = distance(pa, pb);
    auto lb = sqrt(rb * rb + rbc * rbc);

    if (ra == rb) {
      if (intersect_cylinder(ray, pa, pb, ra, dir, frame, sign, tuv, t, p, n)) {
        if (sign > 0) {
          tuv = {tuv.x, (lb + tuv.y * lc) / (lb + lc + la)};
        } else {
          tuv = {tuv.x, (la + tuv.y * lc) / (lb + lc + la)};
        }
        hit = true;
      }
    } else {
      auto cosa = sqrt(ob * ob - rb * rb) / ob;

      // Computing ends' parameters
      rac = ra / cosa;
      pac = o + dir * ((double)oa + (double)tga * (double)rac);
      la  = sqrt(ra * ra + rac * rac);

      rbc = rb / cosa;
      pbc = o + dir * ((double)ob + (double)tga * (double)rbc);
      lb  = sqrt(rb * rb + rbc * rbc);

      if (intersect_cone(ray, pa, pb, ra, rb, dir, frame, sign, tuv, t, p, n)) {
        if (sign > 0) {
          tuv = {tuv.x, (lb + tuv.y * lc) / (lb + lc + la)};
        } else {
          tuv = {tuv.x, (la + tuv.y * lc) / (lb + lc + la)};
        }
        hit = true;
      }
    }

    if (intersect_cap(ray, pa, pb, pac, rac, dir, frame, sign, tuv, t, p, n)) {
      if (sign > 0) {
        tuv = {tuv.x, (lb + lc + 2 * (tuv.y - 0.5f) * la) / (lb + lc + la)};
      } else {
        tuv = {tuv.x, (2 * tuv.y * la) / (lb + lc + la)};
      }
      hit = true;
    }

    if (intersect_cap(
            ray, pb, pa, pbc, rbc, -dir, frame, -sign, tuv, t, p, n)) {
      if (sign > 0) {
        tuv = {tuv.x, (2 * tuv.y * lb) / (lb + lc + la)};
      } else {
        tuv = {tuv.x, (la + lc + 2 * (tuv.y - 0.5f) * lb) / (lb + lc + la)};
      }
      hit = true;
    }

    if (!hit) return false;

    // intersection occurred: set params and exit
    uv   = tuv;
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