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

          auto fcenter = (p0 + p1 + p2) / 3;
          if (dot(camera_origin - fcenter, cross(p1 - p0, p2 - p0)) < 0)
            continue;
          shape.triangles.push_back(triangle);
        }
      }

      // triangle borders
      auto emap    = make_edge_map(shape.triangles);
      auto borders = dshape.boundary ? get_boundary(emap) : get_edges(emap);
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

          auto fcenter = (p0 + p1 + p2 + p3) / 4;
          if (dot(camera_origin - fcenter, cross(p1 - p0, p2 - p0)) < 0)
            continue;
          shape.quads.push_back(quad);
          if (!dshape.fills.empty()) shape.fills.push_back(dshape.fills[idx]);
        }
      }

      // quad borders
      auto emap    = make_edge_map(shape.quads);
      auto borders = dshape.boundary ? get_boundary(emap) : get_edges(emap);
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
    auto plane_dir = transform_normal(
        camera_frame, {0, 0, camera.lens / size.x * scale});
    auto aspect = size.x / size.y;
    auto film   = aspect >= 1 ? vec2f{camera.film, camera.film / aspect}
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

}  // namespace yocto