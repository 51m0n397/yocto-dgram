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

#include "yocto_dgram_geometry.h"

// -----------------------------------------------------------------------------
// USING DIRECTIVES
// -----------------------------------------------------------------------------
namespace yocto {

  // using directives
  using std::vector;

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

  // Computes ordered triangle mesh boundary
  static vector<vec2i> get_boundary(vector<vec3i> triangles, int num_vertices) {
    auto boundary   = vector<vec2i>{};
    auto adiac      = face_adjacencies(triangles);
    auto ord_bounds = ordered_boundaries(triangles, adiac, num_vertices);
    if (!ord_bounds.empty()) {
      auto ord_bound = ord_bounds[0];
      for (auto idx = 0; idx < ord_bound.size() - 1; idx++) {
        boundary.push_back(vec2i{ord_bound[idx], ord_bound[idx + 1]});
      }
      boundary.push_back(vec2i{ord_bound[ord_bound.size() - 1], ord_bound[0]});
    }

    return boundary;
  }

  // Computes ordered quad mesh boundary
  static vector<vec2i> get_boundary(vector<vec4i> quads, int num_vertices) {
    auto triangles = quads_to_triangles(quads);
    return get_boundary(triangles, num_vertices);
  }

  trace_shape make_shape(const dgram_scene& scene, const dgram_object& object,
      const frame3f& camera_frame, const float camera_distance,
      const bool orthographic, const vec2f& film, const float lens,
      const vec2f& size, const float scale) {
    auto shape = trace_shape{};

    auto& dshape   = scene.shapes[object.shape];
    auto& material = scene.materials[object.material];

    auto radius = orthographic ? material.thickness * film.x * camera_distance /
                                     (2 * lens * scale)
                               : material.thickness * film.x / (2 * size.x);
    auto plane_distance = -lens * scale / size.x;

    for (auto& pos : dshape.positions) {
      // position
      auto& p = shape.positions.emplace_back();
      p       = transform_point(object.frame, pos);

      // radius
      if (orthographic)
        shape.radii.push_back(radius);
      else {
        // compuing world-space radius from screen-space radius using triangles
        // similarities
        auto camera_p = transform_point(inverse(camera_frame), p);

        // fix for when point is behind the camera
        if (camera_p.z > 0) camera_p.z = 0;

        shape.radii.push_back(radius * abs(camera_p.z / plane_distance));
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
        // culling triangles
        for (auto& triangle : dshape.triangles) {
          auto p0 = shape.positions[triangle.x];
          auto p1 = shape.positions[triangle.y];
          auto p2 = shape.positions[triangle.z];

          auto dir = camera_frame.z;
          if (!orthographic) {
            auto fcenter = (p0 + p1 + p2) / 3;
            dir          = camera_frame.o - fcenter;
          }

          if (dot(dir, cross(p1 - p0, p2 - p0)) < 0) continue;
          shape.triangles.push_back(triangle);
        }
      }

      // computing triangles borders
      auto borders = dshape.boundary ? get_boundary(shape.triangles,
                                           (int)shape.positions.size())
                                     : get_edges(shape.triangles);

      shape.borders.insert(shape.borders.end(), borders.begin(), borders.end());
    }

    // quads
    if (!dshape.quads.empty()) {
      if (!dshape.cull) {
        shape.quads = dshape.quads;
        shape.fills = dshape.fills;
      } else {
        // culling quads
        for (auto idx = 0; idx < dshape.quads.size(); idx++) {
          auto& quad = dshape.quads[idx];
          auto  p0   = shape.positions[quad.x];
          auto  p1   = shape.positions[quad.y];
          auto  p2   = shape.positions[quad.z];
          auto  p3   = shape.positions[quad.w];

          auto dir = camera_frame.z;
          if (!orthographic) {
            auto fcenter = (p0 + p1 + p2 + p3) / 4;
            dir          = camera_frame.o - fcenter;
          }

          if (dot(dir, cross(p1 - p0, p2 - p0)) < 0) continue;
          shape.quads.push_back(quad);
          if (!dshape.fills.empty()) shape.fills.push_back(dshape.fills[idx]);
        }
      }

      // computing quads borders
      auto borders = dshape.boundary ? get_boundary(shape.quads,
                                           (int)shape.positions.size())
                                     : get_edges(shape.quads);

      shape.borders.insert(shape.borders.end(), borders.begin(), borders.end());
    }

    // arrow dirs
    for (auto& line : shape.lines) {
      auto& p0 = shape.positions[line.x];
      auto& p1 = shape.positions[line.y];

      auto camera_p0 = transform_point(inverse(camera_frame), p0);
      auto camera_p1 = transform_point(inverse(camera_frame), p1);

      if (orthographic) {
        // computing the line scree-space length
        auto screen_camera_p0 = vec3f{camera_p0.x, camera_p0.y, plane_distance};
        auto screen_camera_p1 = vec3f{camera_p1.x, camera_p1.y, plane_distance};

        auto screen_p0 = transform_point(camera_frame, screen_camera_p0);
        auto screen_p1 = transform_point(camera_frame, screen_camera_p1);

        auto screen_length = distance(screen_p0, screen_p1);

        shape.line_lengths.push_back(screen_length);

        // computing the line direction in screen-space
        auto screen_camera_dir = normalize(screen_camera_p1 - screen_camera_p0);
        auto screen_dir        = normalize(screen_p1 - screen_p0);
        auto screen_dir_45_0   = transform_direction(
              camera_frame, vec3f{screen_camera_dir.x + screen_camera_dir.y,
                              screen_camera_dir.y - screen_camera_dir.x, 0});
        auto screen_dir_45_1 = transform_direction(
            camera_frame, vec3f{screen_camera_dir.x - screen_camera_dir.y,
                              screen_camera_dir.y + screen_camera_dir.x, 0});

        shape.screen_line_dirs.push_back(screen_dir);
        shape.screen_line_dirs_45_0.push_back(screen_dir_45_0);
        shape.screen_line_dirs_45_1.push_back(screen_dir_45_1);

        // computing the arrow-head base center
        auto camera_arrow_center0 = line_point(
            camera_p0, camera_p1, 8 * radius / screen_length);
        auto camera_arrow_center1 = line_point(
            camera_p1, camera_p0, 8 * radius / screen_length);

        auto arrow_center0 = transform_point(
            camera_frame, camera_arrow_center0);
        auto arrow_center1 = transform_point(
            camera_frame, camera_arrow_center1);

        shape.arrow_centers0.push_back(arrow_center0);
        shape.arrow_centers1.push_back(arrow_center1);

        // computing the arrow-head base radius
        auto arrow_radius0 = radius * 8 / 3;
        auto arrow_radius1 = radius * 8 / 3;

        shape.arrow_radii0.push_back(arrow_radius0);
        shape.arrow_radii1.push_back(arrow_radius1);
      } else {
        // fix for when point is behind the camera
        if (camera_p0.z >= 0) camera_p0.z = -ray_eps;
        if (camera_p1.z >= 0) camera_p1.z = -ray_eps;

        // computing the line scree-space length
        auto screen_camera_p0 = screen_space_point(camera_p0, plane_distance);
        auto screen_camera_p1 = screen_space_point(camera_p1, plane_distance);

        auto screen_p0 = transform_point(camera_frame, screen_camera_p0);
        auto screen_p1 = transform_point(camera_frame, screen_camera_p1);

        auto screen_length = distance(screen_p0, screen_p1);

        shape.line_lengths.push_back(screen_length);

        // computing the line direction in screen-space
        auto screen_camera_dir = normalize(screen_camera_p1 - screen_camera_p0);
        auto screen_dir        = normalize(screen_p1 - screen_p0);
        auto screen_dir_45_0   = transform_direction(
              camera_frame, vec3f{screen_camera_dir.x + screen_camera_dir.y,
                              screen_camera_dir.y - screen_camera_dir.x, 0});
        auto screen_dir_45_1 = transform_direction(
            camera_frame, vec3f{screen_camera_dir.x - screen_camera_dir.y,
                              screen_camera_dir.y + screen_camera_dir.x, 0});

        shape.screen_line_dirs.push_back(screen_dir);
        shape.screen_line_dirs_45_0.push_back(screen_dir_45_0);
        shape.screen_line_dirs_45_1.push_back(screen_dir_45_1);

        // computing the arrow-head base center
        auto camera_arrow_center0 = perspective_line_point(
            camera_p0, camera_p1, 8 * radius / screen_length);
        auto camera_arrow_center1 = perspective_line_point(
            camera_p1, camera_p0, 8 * radius / screen_length);

        auto arrow_center0 = transform_point(
            camera_frame, camera_arrow_center0);
        auto arrow_center1 = transform_point(
            camera_frame, camera_arrow_center1);

        shape.arrow_centers0.push_back(arrow_center0);
        shape.arrow_centers1.push_back(arrow_center1);

        // computing the arrow-head base radius
        auto arrow_radius0 = radius * 8 / 3 *
                             abs(camera_arrow_center0.z / plane_distance);
        auto arrow_radius1 = radius * 8 / 3 *
                             abs(camera_arrow_center1.z / plane_distance);

        shape.arrow_radii0.push_back(arrow_radius0);
        shape.arrow_radii1.push_back(arrow_radius1);
      }
    }

    for (auto& border : shape.borders) {
      auto& p0 = shape.positions[border.x];
      auto& p1 = shape.positions[border.y];

      auto camera_p0 = transform_point(inverse(camera_frame), p0);
      auto camera_p1 = transform_point(inverse(camera_frame), p1);

      if (orthographic) {
        auto screen_p0 = transform_point(
            camera_frame, vec3f{camera_p0.x, camera_p0.y, 0});
        auto screen_p1 = transform_point(
            camera_frame, vec3f{camera_p1.x, camera_p1.y, 0});

        shape.border_lengths.push_back(distance(screen_p0, screen_p1));
      } else {
        // fix for when point is behind the camera
        if (camera_p0.z >= 0) camera_p0.z = -ray_eps;
        if (camera_p1.z >= 0) camera_p1.z = -ray_eps;

        auto screen_p0 = transform_point(
            camera_frame, screen_space_point(camera_p0, plane_distance));
        auto screen_p1 = transform_point(
            camera_frame, screen_space_point(camera_p1, plane_distance));

        shape.border_lengths.push_back(distance(screen_p0, screen_p1));
      }
    }

    return shape;
  }

  trace_shapes make_shapes(const dgram_scene& scene, const int& cam,
      const vec2f& size, const float& scale, const bool noparallel) {
    auto& camera          = scene.cameras[cam];
    auto  camera_frame    = lookat_frame(camera.from, camera.to, {0, 1, 0});
    auto  camera_distance = length(camera.from - camera.to);
    auto  aspect          = size.x / size.y;
    auto  film = aspect >= 1 ? vec2f{camera.film, camera.film / aspect}
                             : vec2f{camera.film * aspect, camera.film};

    auto shapes = trace_shapes{};

    if (noparallel) {
      for (auto idx = 0; idx < scene.objects.size(); idx++) {
        auto& object = scene.objects[idx];
        if (object.shape != -1) {
          auto shape = make_shape(scene, object, camera_frame, camera_distance,
              camera.orthographic, film, camera.lens, size, scale);
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
          auto shape = make_shape(scene, object, camera_frame, camera_distance,
              camera.orthographic, film, camera.lens, size, scale);
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
    auto camera_distance = length(camera.from - camera.to);
    auto aspect          = size.x / size.y;
    auto film = aspect >= 1 ? vec2f{camera.film, camera.film / aspect}
                            : vec2f{camera.film * aspect, camera.film};

    auto plane_distance = -camera.lens * scale / size.x;

    auto u = 0.0f;
    auto v = 0.0f;

    auto& lines   = element.primitive == primitive_type::line ? shape.lines
                                                              : shape.borders;
    auto& lengths = element.primitive == primitive_type::line
                        ? shape.line_lengths
                        : shape.border_lengths;

    auto& p0 = shape.positions[lines[element.index].x];
    auto& p1 = shape.positions[lines[element.index].y];

    auto camera_p  = transform_point(inverse(camera_frame), p);
    auto camera_p0 = transform_point(inverse(camera_frame), p0);
    auto camera_p1 = transform_point(inverse(camera_frame), p1);

    for (auto idx = 0; idx < element.index; idx++) {
      u += lengths[idx];
    }

    if (camera.orthographic) {
      auto screen_p = transform_point(
          camera_frame, vec3f{camera_p.x, camera_p.y, 0});
      auto screen_p0 = transform_point(
          camera_frame, vec3f{camera_p0.x, camera_p0.y, 0});
      auto screen_p1 = transform_point(
          camera_frame, vec3f{camera_p1.x, camera_p1.y, 0});

      auto screen_dir = normalize(screen_p1 - screen_p0);
      auto line_p     = screen_p0 +
                    dot(screen_p - screen_p0, screen_dir) * screen_dir;

      auto p_sign = sign(dot(screen_p1 - screen_p0, line_p - screen_p0));

      u += p_sign * distance(line_p, screen_p0);
      u *= scale * camera.lens / (camera_distance * film.x);

      v = distance(line_p, screen_p);
      v *= scale * camera.lens / (camera_distance * film.x);
    } else {
      // fix for when point is behind the camera
      if (camera_p.z >= 0) camera_p.z = -ray_eps;
      if (camera_p0.z >= 0) camera_p0.z = -ray_eps;
      if (camera_p1.z >= 0) camera_p1.z = -ray_eps;

      auto screen_p = transform_point(
          camera_frame, screen_space_point(camera_p, plane_distance));
      auto screen_p0 = transform_point(
          camera_frame, screen_space_point(camera_p0, plane_distance));
      auto screen_p1 = transform_point(
          camera_frame, screen_space_point(camera_p1, plane_distance));

      auto screen_dir = normalize(screen_p1 - screen_p0);
      auto line_p     = screen_p0 +
                    dot(screen_p - screen_p0, screen_dir) * screen_dir;

      auto p_sign = sign(dot(screen_p1 - screen_p0, line_p - screen_p0));

      u += p_sign * distance(line_p, screen_p0);
      u *= size.x / film.x;

      v = distance(line_p, screen_p);
      v *= size.x / film.x;
    }

    auto r  = material.thickness / 2;
    auto on = material.dash_on;
    if (material.dash_cap == dash_cap_type::round) on = max(on, 2 * r);

    if (material.dash_period < on) return true;

    auto fm = fmod(u + material.dash_phase, material.dash_period);

    if (material.dash_cap == dash_cap_type::square) return fm < on;

    if (fm < r) {
      auto x = r - fm;
      auto y = v;
      return pow(x, 2) + pow(y, 2) < pow(r, 2);
    } else if (fm < on && fm > on - r) {
      auto x = r - on + fm;
      auto y = v;
      return pow(x, 2) + pow(y, 2) < pow(r, 2);
    } else
      return fm < on;
  }

}  // namespace yocto