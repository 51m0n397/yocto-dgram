//
// # Yocto/Dgram shape: Dgram shape utilities
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

// -----------------------------------------------------------------------------
// INCLUDES
// -----------------------------------------------------------------------------

#include "yocto_dgram_shape.h"

#include <yocto/yocto_geometry.h>
#include <yocto/yocto_shape.h>

#include <future>

#include "ext/earcut.hpp"

// -----------------------------------------------------------------------------
// USING DIRECTIVES
// -----------------------------------------------------------------------------
namespace yocto {

  // using directives
  using std::vector;

  using Point = std::array<float, 2>;
  using mapbox::earcut;

}  // namespace yocto

// -----------------------------------------------------------------------------
// PARALLEL HELPERS
// -----------------------------------------------------------------------------
namespace yocto {

  // Simple parallel for used since our target platforms do not yet support
  // parallel algorithms. `Func` takes the integer index.
  template <typename T, typename Func>
  inline void parallel_for(T num, Func&& func) {
    auto              futures  = vector<std::future<void>>{};
    auto              nthreads = std::thread::hardware_concurrency();
    std::atomic<T>    next_idx(0);
    std::atomic<bool> has_error(false);
    for (auto thread_id = 0; thread_id < (int)nthreads; thread_id++) {
      futures.emplace_back(
          std::async(std::launch::async, [&func, &next_idx, &has_error, num]() {
            try {
              while (true) {
                auto idx = next_idx.fetch_add(1);
                if (idx >= num) break;
                if (has_error) break;
                func(idx);
              }
            } catch (...) {
              has_error = true;
              throw;
            }
          }));
    }
    for (auto& f : futures) f.get();
  }

}  // namespace yocto

// -----------------------------------------------------------------------------
// SHAPES BUILD
// -----------------------------------------------------------------------------
namespace yocto {

  static vec3f intersect_plane(
      const ray3f& ray, const vec3f& p, const vec3f& n) {
    auto o = ray.o - p;

    auto den = dot(ray.d, n);
    if (den == 0) return vec3f{0, 0, 0};

    auto t = -dot(n, o) / den;

    auto q = ray.o + ray.d * t;

    return q;
  }

  static vector<vec3i> triangularize_cclips(const vector<vec3f>& cclips) {
    auto points = vector<Point>{};
    for (auto& cclip : cclips) {
      points.push_back({cclip.x, cclip.y});
    }

    auto polygon = vector<vector<Point>>{points};

    vector<int> indices = earcut<int>(polygon);

    auto triangles = vector<vec3i>{};

    for (auto idx = 0; idx + 2 < indices.size(); idx += 3) {
      triangles.push_back(
          vec3i{indices[idx], indices[idx + 1], indices[idx + 2]});
    }

    return triangles;
  }

  trace_shape make_shape(const dgram_scene& scene, const dgram_object& object,
      const frame3f& camera_frame, const vec3f& camera_origin,
      const float camera_distance, const bool orthographic, const vec2f& film,
      const float lens, const vec3f& plane_point, const vec3f& plane_dir,
      const vec2f& size, const float scale) {
    auto shape = trace_shape{};

    auto& dshape   = scene.shapes[object.shape];
    auto& material = scene.materials[object.material];

    for (auto& pos : dshape.positions) {
      // position
      auto& p = shape.positions.emplace_back();
      p       = transform_point(object.frame, pos);

      // radius
      if (orthographic)
        shape.radii.push_back(
            material.thickness * film.x * camera_distance / (scale * 2 * lens));
      else {
        auto thickness = material.thickness / size.x * film.x / 2;

        auto po = normalize(p - camera_origin);

        auto p_on_plane = intersect_plane(
            ray3f{camera_origin, po}, plane_point, plane_dir);

        auto trans_p = transform_point(inverse(camera_frame), p_on_plane);

        auto radius = 0.0f;

        auto trasl_p        = trans_p + vec3f{0, thickness, 0};
        auto traslated_p    = transform_point(camera_frame, trasl_p);
        auto translated_dir = normalize(traslated_p - camera_origin);
        auto new_p          = intersect_plane(
                     ray3f{camera_origin, translated_dir}, p, plane_dir);
        radius += distance(p, new_p);

        trasl_p        = trans_p + vec3f{0, -thickness, 0};
        traslated_p    = transform_point(camera_frame, trasl_p);
        translated_dir = normalize(traslated_p - camera_origin);
        new_p          = intersect_plane(
                     ray3f{camera_origin, translated_dir}, p, plane_dir);
        radius += distance(p, new_p);

        trasl_p        = trans_p + vec3f{thickness, 0, 0};
        traslated_p    = transform_point(camera_frame, trasl_p);
        translated_dir = normalize(traslated_p - camera_origin);
        new_p          = intersect_plane(
                     ray3f{camera_origin, translated_dir}, p, plane_dir);
        radius += distance(p, new_p);

        trasl_p        = trans_p + vec3f{-thickness, 0, 0};
        traslated_p    = transform_point(camera_frame, trasl_p);
        translated_dir = normalize(traslated_p - camera_origin);
        new_p          = intersect_plane(
                     ray3f{camera_origin, translated_dir}, p, plane_dir);
        radius += distance(p, new_p);

        radius /= 4;

        shape.radii.push_back(radius);
      }
    }

    shape.points = dshape.points;

    shape.lines = dshape.lines;
    shape.ends  = dshape.ends;

    shape.material = object.material;

    // triangles
    if (!dshape.triangles.empty()) {
      if (!dshape.cull)
        shape.triangles = dshape.triangles;
      else {
        for (auto& triangle : dshape.triangles) {
          auto p0 = shape.positions[triangle.x];
          auto p1 = shape.positions[triangle.y];
          auto p2 = shape.positions[triangle.z];

          auto dir = plane_dir;
          if (!orthographic) {
            auto fcenter = (p0 + p1 + p2) / 3;
            dir          = camera_origin - fcenter;
          }

          if (dot(dir, cross(p1 - p0, p2 - p0)) < 0) continue;
          shape.triangles.push_back(triangle);
        }
      }

      // triangle borders
      auto emap    = make_edge_map(shape.triangles);
      auto borders = dshape.boundary ? vector<vec2i>{} : get_edges(emap);

      if (dshape.boundary) {
        auto adiac     = face_adjacencies(shape.triangles);
        auto ord_bound = ordered_boundaries(
            shape.triangles, adiac, (int)shape.positions.size())[0];
        for (auto idx = 0; idx < ord_bound.size() - 1; idx++) {
          borders.push_back(vec2i{ord_bound[idx], ord_bound[idx + 1]});
        }
        borders.push_back(vec2i{ord_bound[ord_bound.size() - 1], ord_bound[0]});
      }

      shape.borders.insert(shape.borders.end(), borders.begin(), borders.end());
    }

    // quads
    if (!dshape.quads.empty()) {
      if (!dshape.cull) {
        shape.quads = dshape.quads;
        shape.fills = dshape.fills;
      } else {
        for (auto idx = 0; idx < dshape.quads.size(); idx++) {
          auto& quad = dshape.quads[idx];
          auto  p0   = shape.positions[quad.x];
          auto  p1   = shape.positions[quad.y];
          auto  p2   = shape.positions[quad.z];
          auto  p3   = shape.positions[quad.w];

          auto dir = plane_dir;
          if (!orthographic) {
            auto fcenter = (p0 + p1 + p2 + p3) / 4;
            dir          = camera_origin - fcenter;
          }

          if (dot(dir, cross(p1 - p0, p2 - p0)) < 0) continue;
          shape.quads.push_back(quad);
          if (!dshape.fills.empty()) shape.fills.push_back(dshape.fills[idx]);
        }
      }

      // quad borders
      auto emap    = make_edge_map(shape.quads);
      auto borders = dshape.boundary ? vector<vec2i>{} : get_edges(emap);

      if (dshape.boundary) {
        auto tri       = quads_to_triangles(shape.quads);
        auto adiac     = face_adjacencies(tri);
        auto ord_bound = ordered_boundaries(
            tri, adiac, (int)shape.positions.size())[0];
        for (auto idx = 0; idx < ord_bound.size() - 1; idx++) {
          borders.push_back(vec2i{ord_bound[idx], ord_bound[idx + 1]});
        }
        borders.push_back(vec2i{ord_bound[ord_bound.size() - 1], ord_bound[0]});
      }

      shape.borders.insert(shape.borders.end(), borders.begin(), borders.end());
    }

    // cclips
    if (!dshape.cclips.empty()) {
      shape.cclip_indices   = triangularize_cclips(dshape.cclips);
      shape.cclip_positions = dshape.cclips;

      /*for (auto& pos : dshape.cclips) {
        auto& p = shape.cclip_positions.emplace_back();
        p       = transform_point(object.frame, pos);
      }*/
    }

    // arrow dirs
    for (auto& line : shape.lines) {
      auto& p0 = shape.positions[line.x];
      auto& p1 = shape.positions[line.y];

      if (orthographic) {
        auto dir      = normalize(p1 - p0);
        auto line_dir = transform_direction(inverse(camera_frame), dir);

        auto arrow_dir = transform_direction(
            camera_frame, vec3f{line_dir.x, line_dir.y, 0});
        auto arrow_dir0 = transform_direction(camera_frame,
            vec3f{line_dir.x + line_dir.y, line_dir.y - line_dir.x, 0});
        auto arrow_dir1 = transform_direction(camera_frame,
            vec3f{line_dir.x - line_dir.y, line_dir.y + line_dir.x, 0});

        shape.arrow_dirs.push_back(arrow_dir);
        shape.arrow_dirs0.push_back(arrow_dir0);
        shape.arrow_dirs1.push_back(arrow_dir1);

        auto thickness = material.thickness * film.x * camera_distance /
                         (scale * 2 * lens);

        shape.arrow_points0.push_back(p0 + 8 * thickness * dir);
        shape.arrow_points1.push_back(p1 - 8 * thickness * dir);

        shape.arrow_rads0.push_back(
            4 / sqrt(2.0) * thickness * dot(dir, arrow_dir));
        shape.arrow_rads1.push_back(
            4 / sqrt(2.0) * thickness * dot(dir, arrow_dir));

        auto p0_camera = transform_point(inverse(camera_frame), p0);
        auto p1_camera = transform_point(inverse(camera_frame), p1);

        auto p0_on_plane = transform_point(
            camera_frame, vec3f{p0_camera.x, p0_camera.y, 0});
        auto p1_on_plane = transform_point(
            camera_frame, vec3f{p1_camera.x, p1_camera.y, 0});

        shape.line_lengths.push_back(distance(p0_on_plane, p1_on_plane));
      } else {
        auto p0_on_plane = intersect_plane(
            ray3f{camera_origin, p0 - camera_origin}, plane_point, plane_dir);
        auto p1_on_plane = intersect_plane(
            ray3f{camera_origin, p1 - camera_origin}, plane_point, plane_dir);

        auto arrow_dir  = normalize(p0_on_plane - p1_on_plane);
        auto dir_camera = transform_direction(inverse(camera_frame), arrow_dir);
        auto arrow_dir0 = transform_direction(camera_frame,
            vec3f{dir_camera.x + dir_camera.y, dir_camera.y - dir_camera.x, 0});
        auto arrow_dir1 = transform_direction(camera_frame,
            vec3f{dir_camera.x - dir_camera.y, dir_camera.y + dir_camera.x, 0});

        shape.arrow_dirs.push_back(arrow_dir);
        shape.arrow_dirs0.push_back(arrow_dir0);
        shape.arrow_dirs1.push_back(arrow_dir1);

        auto ap0_on_p = p0_on_plane + 8 * material.thickness / size.x * film.x /
                                          2 *
                                          normalize(p1_on_plane - p0_on_plane);
        auto ap1_on_p = p1_on_plane + 8 * material.thickness / size.x * film.x /
                                          2 *
                                          normalize(p0_on_plane - p1_on_plane);

        auto rp0 = ray3f{camera_origin, ap0_on_p - camera_origin};
        auto ap0 = intersect_plane(
            rp0, p0, orthonormalize(rp0.d, normalize(p1 - p0)));

        auto rp1 = ray3f{camera_origin, ap1_on_p - camera_origin};
        auto ap1 = intersect_plane(
            rp1, p1, orthonormalize(rp1.d, normalize(p0 - p1)));

        shape.arrow_points0.push_back(ap0);
        shape.arrow_points1.push_back(ap1);

        auto ap0c = transform_point(inverse(camera_frame), ap0_on_p);
        auto ap1c = transform_point(inverse(camera_frame), ap1_on_p);

        auto ad0c = transform_direction(
            inverse(camera_frame), p1_on_plane - p0_on_plane);
        auto ad1c = transform_direction(
            inverse(camera_frame), p0_on_plane - p1_on_plane);

        auto adp0c = vec3f{ad0c.y, -ad0c.x, 0};
        auto adp1c = vec3f{ad1c.y, -ad1c.x, 0};

        auto ar0_on_p = transform_point(
            camera_frame, ap0c + 4 / sqrt(2.0) * material.thickness / size.x *
                                     film.x / 2 * adp0c);
        auto ar1_on_p = transform_point(
            camera_frame, ap1c + 4 / sqrt(2.0) * material.thickness / size.x *
                                     film.x / 2 * adp1c);

        auto rr0 = ray3f{camera_origin, ar0_on_p - camera_origin};
        auto ar0 = intersect_plane(rr0, ap0, plane_dir);

        auto rr1 = ray3f{camera_origin, ar1_on_p - camera_origin};
        auto ar1 = intersect_plane(rr1, ap1, plane_dir);

        shape.arrow_rads0.push_back(distance(ap0, ar0));
        shape.arrow_rads1.push_back(distance(ap1, ar1));

        shape.line_lengths.push_back(distance(p0_on_plane, p1_on_plane));
      }
    }

    for (auto& border : shape.borders) {
      auto& p0 = shape.positions[border.x];
      auto& p1 = shape.positions[border.y];

      if (orthographic) {
        auto p0_camera = transform_point(inverse(camera_frame), p0);
        auto p1_camera = transform_point(inverse(camera_frame), p1);

        auto p0_on_plane = transform_point(
            camera_frame, vec3f{p0_camera.x, p0_camera.y, 0});
        auto p1_on_plane = transform_point(
            camera_frame, vec3f{p1_camera.x, p1_camera.y, 0});

        shape.border_lengths.push_back(distance(p0_on_plane, p1_on_plane));
      } else {
        auto p0_on_plane = intersect_plane(
            ray3f{camera_origin, p0 - camera_origin}, plane_point, plane_dir);
        auto p1_on_plane = intersect_plane(
            ray3f{camera_origin, p1 - camera_origin}, plane_point, plane_dir);

        shape.border_lengths.push_back(distance(p0_on_plane, p1_on_plane));
      }
    }

    return shape;
  }

  trace_shapes make_shapes(const dgram_scene& scene, const int& cam,
      const vec2f& size, const float& scale, const bool noparallel) {
    auto& camera          = scene.cameras[cam];
    auto  camera_frame    = lookat_frame(camera.from, camera.to, {0, 1, 0});
    auto  camera_origin   = camera.from;
    auto  camera_distance = length(camera.from - camera.to);
    auto  plane_point     = transform_point(
             camera_frame, {0, 0, camera.lens / size.x * scale});
    auto plane_dir = transform_normal(camera_frame, {0, 0, 1});
    auto aspect    = size.x / size.y;
    auto film      = aspect >= 1 ? vec2f{camera.film, camera.film / aspect}
                                 : vec2f{camera.film * aspect, camera.film};

    auto shapes = trace_shapes{};

    if (noparallel) {
      for (auto idx = 0; idx < scene.objects.size(); idx++) {
        auto& object = scene.objects[idx];
        if (object.shape != -1) {
          auto shape = make_shape(scene, object, camera_frame, camera_origin,
              camera_distance, camera.orthographic, film, camera.lens,
              plane_point, plane_dir, size, scale);
          shapes.shapes.push_back(shape);
        }
      }
    } else {
      auto idxs = vector<int>{};

      for (auto i = 0; i < scene.objects.size(); i++) {
        auto& object = scene.objects[i];
        if (object.shape != -1) {
          idxs.push_back(i);
        }
      }

      shapes.shapes.resize(idxs.size());
      parallel_for(idxs.size(), [&](size_t i) {
        auto  idx    = idxs[i];
        auto& object = scene.objects[idx];
        if (object.shape != -1) {
          auto shape = make_shape(scene, object, camera_frame, camera_origin,
              camera_distance, camera.orthographic, film, camera.lens,
              plane_point, plane_dir, size, scale);
          shapes.shapes[i] = shape;
        }
      });
    }

    return shapes;
  }

}  // namespace yocto

// -----------------------------------------------------------------------------
// SHAPE PROPERTY EVALUATION
// -----------------------------------------------------------------------------
namespace yocto {

  vec4f eval_material(const trace_shape& shape, const dgram_material& material,
      const shape_element& element, const vec2f& uv) {
    auto color = zero4f;

    switch (element.primitive) {
      case primitive_type::point: color = material.stroke; break;
      case primitive_type::line: color = material.stroke; break;
      case primitive_type::triangle: color = material.fill; break;
      case primitive_type::quad:
        color = shape.fills.empty() ? material.fill
                                    : shape.fills[element.index];
        break;
      case primitive_type::border: color = material.stroke; break;
    }

    return color;
  }

  bool eval_dashes(const vec3f& p, const trace_shape& shape,
      const dgram_material& material, const shape_element& element,
      const dgram_camera& camera, const vec2f& size, const float& scale) {
    auto camera_frame    = lookat_frame(camera.from, camera.to, {0, 1, 0});
    auto camera_origin   = camera.from;
    auto camera_distance = length(camera.from - camera.to);
    auto plane_point     = transform_point(
            camera_frame, {0, 0, camera.lens / size.x * scale});
    auto plane_dir = transform_normal(camera_frame, {0, 0, 1});
    auto aspect    = size.x / size.y;
    auto film      = aspect >= 1 ? vec2f{camera.film, camera.film / aspect}
                                 : vec2f{camera.film * aspect, camera.film};

    auto dist = 0.0f;

    auto& lines   = element.primitive == primitive_type::line ? shape.lines
                                                              : shape.borders;
    auto& lengths = element.primitive == primitive_type::line
                        ? shape.line_lengths
                        : shape.border_lengths;

    auto& p0 = shape.positions[lines[element.index].x];
    auto& p1 = shape.positions[lines[element.index].y];

    for (auto idx = 0; idx < element.index; idx++) {
      dist += lengths[idx];
    }

    if (camera.orthographic) {
      auto p_camera  = transform_point(inverse(camera_frame), p);
      auto p0_camera = transform_point(inverse(camera_frame), p0);
      auto p1_camera = transform_point(inverse(camera_frame), p1);

      auto p_on_plane = transform_point(
          camera_frame, vec3f{p_camera.x, p_camera.y, 0});
      auto p0_on_plane = transform_point(
          camera_frame, vec3f{p0_camera.x, p0_camera.y, 0});
      auto p1_on_plane = transform_point(
          camera_frame, vec3f{p1_camera.x, p1_camera.y, 0});

      auto dir       = normalize(p1_on_plane - p0_on_plane);
      auto p_on_line = p0_on_plane + dot(p_on_plane - p0_on_plane, dir) * dir;

      auto p_sign = sign(
          dot(p1_on_plane - p0_on_plane, p_on_line - p0_on_plane));

      dist += p_sign * distance(p_on_line, p0_on_plane);
      dist *= scale * camera.lens / (camera_distance * film.x);
    } else {
      auto p_on_plane = intersect_plane(
          ray3f{camera_origin, p - camera_origin}, plane_point, plane_dir);
      auto p0_on_plane = intersect_plane(
          ray3f{camera_origin, p0 - camera_origin}, plane_point, plane_dir);
      auto p1_on_plane = intersect_plane(
          ray3f{camera_origin, p1 - camera_origin}, plane_point, plane_dir);

      auto dir       = normalize(p1_on_plane - p0_on_plane);
      auto p_on_line = p0_on_plane + dot(p_on_plane - p0_on_plane, dir) * dir;

      auto p_sign = sign(
          dot(p1_on_plane - p0_on_plane, p_on_line - p0_on_plane));

      dist += p_sign * distance(p_on_line, p0_on_plane);
      dist *= size.x / film.x;
    }

    return fmod(dist + material.dash_phase, material.dash_period) <
           material.dash_on;
  }

}  // namespace yocto