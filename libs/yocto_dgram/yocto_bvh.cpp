//
// Implementation for Yocto/Bvh
//

//
// LICENSE:
//
// Copyright (c) 2016 -- 2021 Fabio Pellacini
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

#include "yocto_bvh.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "yocto_geometry.h"

// -----------------------------------------------------------------------------
// USING DIRECTIVES
// -----------------------------------------------------------------------------
namespace yocto {

  // using directives
  using std::array;
  using std::atomic;
  using std::pair;
  using std::string;
  using namespace std::string_literals;

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
// IMPLEMENTATION FOR BVH BUILD
// -----------------------------------------------------------------------------
namespace yocto {

  // Splits a BVH node using the SAH heuristic. Returns split position and axis.
  static pair<int, int> split_sah(vector<int>& primitives,
      const vector<bbox3f>& bboxes, const vector<vec3f>& centers, int start,
      int end) {
    // compute primintive bounds and size
    auto cbbox = invalidb3f;
    for (auto i = start; i < end; i++)
      cbbox = merge(cbbox, centers[primitives[i]]);
    auto csize = cbbox.max - cbbox.min;
    if (csize == vec3f{0, 0, 0}) return {(start + end) / 2, 0};

    // consider N bins, compute their cost and keep the minimum
    auto      axis      = 0;
    const int nbins     = 16;
    auto      split     = 0.0f;
    auto      min_cost  = flt_max;
    auto      bbox_area = [](const bbox3f& b) {
      auto size = b.max - b.min;
      return 1e-12f + 2 * size.x * size.y + 2 * size.x * size.z +
             2 * size.y * size.z;
    };
    for (auto saxis = 0; saxis < 3; saxis++) {
      for (auto b = 1; b < nbins; b++) {
        auto bsplit    = cbbox.min[saxis] + b * csize[saxis] / nbins;
        auto left_bbox = invalidb3f, right_bbox = invalidb3f;
        auto left_nprims = 0, right_nprims = 0;
        for (auto i = start; i < end; i++) {
          if (centers[primitives[i]][saxis] < bsplit) {
            left_bbox = merge(left_bbox, bboxes[primitives[i]]);
            left_nprims += 1;
          } else {
            right_bbox = merge(right_bbox, bboxes[primitives[i]]);
            right_nprims += 1;
          }
        }
        auto cost = 1 + left_nprims * bbox_area(left_bbox) / bbox_area(cbbox) +
                    right_nprims * bbox_area(right_bbox) / bbox_area(cbbox);
        if (cost < min_cost) {
          min_cost = cost;
          split    = bsplit;
          axis     = saxis;
        }
      }
    }
    // split
    auto middle =
        (int)(std::partition(primitives.data() + start, primitives.data() + end,
                  [axis, split, &centers](auto primitive) {
                    return centers[primitive][axis] < split;
                  }) -
              primitives.data());

    // if we were not able to split, just break the primitives in half
    if (middle == start || middle == end) return {(start + end) / 2, axis};

    // done
    return {middle, axis};
  }

  // Splits a BVH node using the balance heuristic. Returns split position and
  // axis.
  [[maybe_unused]] static pair<int, int> split_balanced(vector<int>& primitives,
      const vector<bbox3f>& bboxes, const vector<vec3f>& centers, int start,
      int end) {
    // compute primitives bounds and size
    auto cbbox = invalidb3f;
    for (auto i = start; i < end; i++)
      cbbox = merge(cbbox, centers[primitives[i]]);
    auto csize = cbbox.max - cbbox.min;
    if (csize == vec3f{0, 0, 0}) return {(start + end) / 2, 0};

    // split along largest
    auto axis = 0;
    if (csize.x >= csize.y && csize.x >= csize.z) axis = 0;
    if (csize.y >= csize.x && csize.y >= csize.z) axis = 1;
    if (csize.z >= csize.x && csize.z >= csize.y) axis = 2;

    // balanced tree split: find the largest axis of the
    // bounding box and split along this one right in the middle
    auto middle = (start + end) / 2;
    std::nth_element(primitives.data() + start, primitives.data() + middle,
        primitives.data() + end,
        [axis, &centers](auto primitive_a, auto primitive_b) {
          return centers[primitive_a][axis] < centers[primitive_b][axis];
        });

    // if we were not able to split, just break the primitives in half
    if (middle == start || middle == end) return {(start + end) / 2, axis};

    // done
    return {middle, axis};
  }

  // Splits a BVH node using the middle heuristic. Returns split position and
  // axis.
  static pair<int, int> split_middle(vector<int>& primitives,
      const vector<bbox3f>& bboxes, const vector<vec3f>& centers, int start,
      int end) {
    // compute primintive bounds and size
    auto cbbox = invalidb3f;
    for (auto i = start; i < end; i++)
      cbbox = merge(cbbox, centers[primitives[i]]);
    auto csize = cbbox.max - cbbox.min;
    if (csize == vec3f{0, 0, 0}) return {(start + end) / 2, 0};

    // split along largest
    auto axis = 0;
    if (csize.x >= csize.y && csize.x >= csize.z) axis = 0;
    if (csize.y >= csize.x && csize.y >= csize.z) axis = 1;
    if (csize.z >= csize.x && csize.z >= csize.y) axis = 2;

    // split the space in the middle along the largest axis
    auto split = center(cbbox)[axis];
    auto middle =
        (int)(std::partition(primitives.data() + start, primitives.data() + end,
                  [axis, split, &centers](auto primitive) {
                    return centers[primitive][axis] < split;
                  }) -
              primitives.data());

    // if we were not able to split, just break the primitives in half
    if (middle == start || middle == end) return {(start + end) / 2, axis};

    // done
    return {middle, axis};
  }

  // Maximum number of primitives per BVH node.
  const int bvh_max_prims = 4;

  // Build BVH nodes
  static void build_bvh(vector<bvh_node>& nodes, vector<int>& primitives,
      const vector<bbox3f>& bboxes, bool highquality) {
    // prepare to build nodes
    nodes.clear();
    nodes.reserve(bboxes.size() * 2);

    // prepare primitives
    primitives.resize(bboxes.size());
    for (auto idx = 0; idx < bboxes.size(); idx++) primitives[idx] = idx;

    // prepare centers
    auto centers = vector<vec3f>(bboxes.size());
    for (auto idx = 0; idx < bboxes.size(); idx++)
      centers[idx] = center(bboxes[idx]);

    // push first node onto the stack
    auto stack = vector<vec3i>{{0, 0, (int)bboxes.size()}};
    nodes.emplace_back();

    // create nodes until the stack is empty
    while (!stack.empty()) {
      // grab node to work on
      auto [nodeid, start, end] = stack.back();
      stack.pop_back();

      // grab node
      auto& node = nodes[nodeid];

      // compute bounds
      node.bbox = invalidb3f;
      for (auto i = start; i < end; i++)
        node.bbox = merge(node.bbox, bboxes[primitives[i]]);

      // split into two children
      if (end - start > bvh_max_prims) {
        // get split
        auto [mid, axis] =
            highquality ? split_sah(primitives, bboxes, centers, start, end)
                        : split_middle(primitives, bboxes, centers, start, end);

        // make an internal node
        node.internal = true;
        node.axis     = (uint8_t)axis;
        node.num      = 2;
        node.start    = (int)nodes.size();
        nodes.emplace_back();
        nodes.emplace_back();
        stack.push_back({node.start + 0, start, mid});
        stack.push_back({node.start + 1, mid, end});
      } else {
        // Make a leaf node
        node.internal = false;
        node.num      = (int16_t)(end - start);
        node.start    = start;
      }
    }

    // cleanup
    nodes.shrink_to_fit();
  }

  // Update bvh
  static void refit_bvh(vector<bvh_node>& nodes, const vector<int>& primitives,
      const vector<bbox3f>& bboxes) {
    for (auto nodeid = (int)nodes.size() - 1; nodeid >= 0; nodeid--) {
      auto& node = nodes[nodeid];
      node.bbox  = invalidb3f;
      if (node.internal) {
        for (auto idx = 0; idx < 2; idx++) {
          node.bbox = merge(node.bbox, nodes[node.start + idx].bbox);
        }
      } else {
        for (auto idx = 0; idx < node.num; idx++) {
          node.bbox = merge(node.bbox, bboxes[primitives[node.start + idx]]);
        }
      }
    }
  }

  shape_bvh make_bvh(const shape_data& shape, bool highquality) {
    // bvh
    auto bvh = shape_bvh{};

    // build primitives
    auto bboxes = vector<bbox3f>{};
    if (!shape.points.empty()) {
      bboxes = vector<bbox3f>(shape.points.size());
      for (auto idx = 0; idx < shape.points.size(); idx++) {
        auto& point = shape.points[idx];
        bboxes[idx] = point_bounds(shape.positions[point], shape.radius[point]);
      }
    } else if (!shape.lines.empty()) {
      bboxes = vector<bbox3f>(shape.lines.size());
      for (auto idx = 0; idx < shape.lines.size(); idx++) {
        auto& line  = shape.lines[idx];
        bboxes[idx] = line_bounds(shape.positions[line.x],
            shape.positions[line.y], shape.radius[line.x], shape.radius[line.y],
            shape.ends[line.x], shape.ends[line.y]);
      }
    } else if (!shape.triangles.empty()) {
      bboxes = vector<bbox3f>(shape.triangles.size());
      for (auto idx = 0; idx < shape.triangles.size(); idx++) {
        auto& triangle = shape.triangles[idx];
        bboxes[idx]    = triangle_bounds(shape.positions[triangle.x],
               shape.positions[triangle.y], shape.positions[triangle.z]);

        if (shape.border_radius > 0) {
          expand(bboxes[idx],
              line_bounds(shape.positions[triangle.x],
                  shape.positions[triangle.y], shape.border_radius,
                  shape.border_radius, line_end::cap, line_end::cap));
          expand(bboxes[idx],
              line_bounds(shape.positions[triangle.y],
                  shape.positions[triangle.z], shape.border_radius,
                  shape.border_radius, line_end::cap, line_end::cap));
          expand(bboxes[idx],
              line_bounds(shape.positions[triangle.z],
                  shape.positions[triangle.x], shape.border_radius,
                  shape.border_radius, line_end::cap, line_end::cap));
        }
      }
    } else if (!shape.quads.empty()) {
      bboxes = vector<bbox3f>(shape.quads.size());
      for (auto idx = 0; idx < shape.quads.size(); idx++) {
        auto& quad  = shape.quads[idx];
        bboxes[idx] = quad_bounds(shape.positions[quad.x],
            shape.positions[quad.y], shape.positions[quad.z],
            shape.positions[quad.w]);

        if (shape.border_radius > 0) {
          expand(bboxes[idx],
              line_bounds(shape.positions[quad.x], shape.positions[quad.y],
                  shape.border_radius, shape.border_radius, line_end::cap,
                  line_end::cap));
          expand(bboxes[idx],
              line_bounds(shape.positions[quad.y], shape.positions[quad.z],
                  shape.border_radius, shape.border_radius, line_end::cap,
                  line_end::cap));
          expand(bboxes[idx],
              line_bounds(shape.positions[quad.z], shape.positions[quad.w],
                  shape.border_radius, shape.border_radius, line_end::cap,
                  line_end::cap));
          expand(bboxes[idx],
              line_bounds(shape.positions[quad.w], shape.positions[quad.x],
                  shape.border_radius, shape.border_radius, line_end::cap,
                  line_end::cap));
        }
      }
    }

    // build nodes
    build_bvh(bvh.nodes, bvh.primitives, bboxes, highquality);

    // done
    return bvh;
  }

  scene_bvh make_bvh(
      const scene_data& scene, bool highquality, bool noparallel) {
    // bvh
    auto bvh = scene_bvh{};

    // build shape bvh
    bvh.shapes.resize(scene.shapes.size());
    if (noparallel) {
      for (auto idx = (size_t)0; idx < scene.shapes.size(); idx++) {
        bvh.shapes[idx] = make_bvh(scene.shapes[idx], highquality);
      }
    } else {
      parallel_for(scene.shapes.size(), [&](size_t idx) {
        bvh.shapes[idx] = make_bvh(scene.shapes[idx], highquality);
      });
    }

    // instance bboxes
    auto bboxes = vector<bbox3f>(scene.instances.size());
    for (auto idx = 0; idx < bboxes.size(); idx++) {
      auto& instance = scene.instances[idx];
      auto& sbvh     = bvh.shapes[instance.shape];
      bboxes[idx]    = sbvh.nodes.empty()
                           ? invalidb3f
                           : transform_bbox(instance.frame, sbvh.nodes[0].bbox);
    }

    // build nodes
    build_bvh(bvh.nodes, bvh.primitives, bboxes, highquality);

    // done
    return bvh;
  }

  static void refit_bvh(shape_bvh& bvh, const shape_data& shape) {
    // build primitives
    auto bboxes = vector<bbox3f>{};
    if (!shape.points.empty()) {
      bboxes = vector<bbox3f>(shape.points.size());
      for (auto idx = 0; idx < shape.points.size(); idx++) {
        auto& point = shape.points[idx];
        bboxes[idx] = point_bounds(shape.positions[point], shape.radius[point]);
      }
    } else if (!shape.lines.empty()) {
      bboxes = vector<bbox3f>(shape.lines.size());
      for (auto idx = 0; idx < shape.lines.size(); idx++) {
        auto& line  = shape.lines[idx];
        bboxes[idx] = line_bounds(shape.positions[line.x],
            shape.positions[line.y], shape.radius[line.x], shape.radius[line.y],
            shape.ends[line.x], shape.ends[line.y]);
      }
    } else if (!shape.triangles.empty()) {
      bboxes = vector<bbox3f>(shape.triangles.size());
      for (auto idx = 0; idx < shape.triangles.size(); idx++) {
        auto& triangle = shape.triangles[idx];
        bboxes[idx]    = triangle_bounds(shape.positions[triangle.x],
               shape.positions[triangle.y], shape.positions[triangle.z]);

        if (shape.border_radius > 0) {
          expand(bboxes[idx],
              line_bounds(shape.positions[triangle.x],
                  shape.positions[triangle.y], shape.border_radius,
                  shape.border_radius, line_end::cap, line_end::cap));
          expand(bboxes[idx],
              line_bounds(shape.positions[triangle.y],
                  shape.positions[triangle.z], shape.border_radius,
                  shape.border_radius, line_end::cap, line_end::cap));
          expand(bboxes[idx],
              line_bounds(shape.positions[triangle.z],
                  shape.positions[triangle.x], shape.border_radius,
                  shape.border_radius, line_end::cap, line_end::cap));
        }
      }
    } else if (!shape.quads.empty()) {
      bboxes = vector<bbox3f>(shape.quads.size());
      for (auto idx = 0; idx < shape.quads.size(); idx++) {
        auto& quad  = shape.quads[idx];
        bboxes[idx] = quad_bounds(shape.positions[quad.x],
            shape.positions[quad.y], shape.positions[quad.z],
            shape.positions[quad.w]);

        if (shape.border_radius > 0) {
          expand(bboxes[idx],
              line_bounds(shape.positions[quad.x], shape.positions[quad.y],
                  shape.border_radius, shape.border_radius, line_end::cap,
                  line_end::cap));
          expand(bboxes[idx],
              line_bounds(shape.positions[quad.y], shape.positions[quad.z],
                  shape.border_radius, shape.border_radius, line_end::cap,
                  line_end::cap));
          expand(bboxes[idx],
              line_bounds(shape.positions[quad.z], shape.positions[quad.w],
                  shape.border_radius, shape.border_radius, line_end::cap,
                  line_end::cap));
          expand(bboxes[idx],
              line_bounds(shape.positions[quad.w], shape.positions[quad.x],
                  shape.border_radius, shape.border_radius, line_end::cap,
                  line_end::cap));
        }
      }
    }

    // update nodes
    refit_bvh(bvh.nodes, bvh.primitives, bboxes);
  }

  void refit_bvh(scene_bvh& bvh, const scene_data& scene,
      const vector<int>& updated_instances) {
    // build primitives
    auto bboxes = vector<bbox3f>(scene.instances.size());
    for (auto idx = 0; idx < bboxes.size(); idx++) {
      auto& instance = scene.instances[idx];
      auto& sbvh     = bvh.shapes[instance.shape];
      bboxes[idx]    = transform_bbox(instance.frame, sbvh.nodes[0].bbox);
    }

    // update nodes
    refit_bvh(bvh.nodes, bvh.primitives, bboxes);
  }

  void update_bvh(shape_bvh& bvh, const shape_data& shape) {
    // handle instances
    refit_bvh(bvh, shape);
  }

  void update_bvh(scene_bvh& bvh, const scene_data& scene,
      const vector<int>& updated_instances, const vector<int>& updated_shapes) {
    // update shapes
    for (auto shape : updated_shapes) {
      refit_bvh(bvh.shapes[shape], scene.shapes[shape]);
    }

    // handle instances
    refit_bvh(bvh, scene, updated_instances);
  }

}  // namespace yocto

// -----------------------------------------------------------------------------
// IMPLEMENTATION FOR BVH INTERSECTION
// -----------------------------------------------------------------------------
namespace yocto {

  // Intersect ray with a bvh.
  static bool intersect_bvh(const shape_bvh& bvh, const shape_data& shape,
      const ray3f& ray_, int& element, vec2f& uv, float& distance, vec3f& pos,
      vec3f& norm, bool ignore_borders, bool find_any, bool& border) {
    // check empty
    if (bvh.nodes.empty()) return false;

    // node stack
    auto node_stack        = array<int, 128>{};
    auto node_cur          = 0;
    node_stack[node_cur++] = 0;

    // shared variables
    auto hit = false;

    // copy ray to modify it
    auto ray = ray_;

    // prepare ray for fast queries
    auto ray_dinv  = vec3f{1 / ray.d.x, 1 / ray.d.y, 1 / ray.d.z};
    auto ray_dsign = vec3i{(ray_dinv.x < 0) ? 1 : 0, (ray_dinv.y < 0) ? 1 : 0,
        (ray_dinv.z < 0) ? 1 : 0};

    // walking stack
    while (node_cur != 0) {
      // grab node
      auto& node = bvh.nodes[node_stack[--node_cur]];

      // intersect bbox
      // if (!intersect_bbox(ray, ray_dinv, ray_dsign, node.bbox)) continue;
      if (!intersect_bbox(ray, ray_dinv, node.bbox)) continue;

      // intersect node, switching based on node type
      // for each type, iterate over the the primitive list
      if (node.internal) {
        // for internal nodes, attempts to proceed along the
        // split axis from smallest to largest nodes
        if (ray_dsign[node.axis] != 0) {
          node_stack[node_cur++] = node.start + 0;
          node_stack[node_cur++] = node.start + 1;
        } else {
          node_stack[node_cur++] = node.start + 1;
          node_stack[node_cur++] = node.start + 0;
        }
      } else if (!shape.points.empty()) {
        for (auto idx = node.start; idx < node.start + node.num; idx++) {
          auto& p = shape.points[bvh.primitives[idx]];
          if (intersect_point(ray, shape.positions[p], shape.radius[p], uv,
                  distance, pos, norm)) {
            hit      = true;
            element  = bvh.primitives[idx];
            ray.tmax = distance;
            border   = false;
          }
        }
      } else if (!shape.lines.empty()) {
        for (auto idx = node.start; idx < node.start + node.num; idx++) {
          auto& l = shape.lines[bvh.primitives[idx]];
          if (intersect_line(ray, shape.positions[l.x], shape.positions[l.y],
                  shape.radius[l.x], shape.radius[l.y], shape.ends[l.x],
                  shape.ends[l.y], uv, distance, pos, norm)) {
            hit      = true;
            element  = bvh.primitives[idx];
            ray.tmax = distance;
            border   = false;
          }
        }
      } else if (!shape.triangles.empty()) {
        for (auto idx = node.start; idx < node.start + node.num; idx++) {
          auto& t = shape.triangles[bvh.primitives[idx]];
          if (intersect_triangle(ray, shape.positions[t.x],
                  shape.positions[t.y], shape.positions[t.z], uv, distance, pos,
                  norm)) {
            hit      = true;
            element  = bvh.primitives[idx];
            ray.tmax = distance;
            border   = false;
          }
          if (shape.border_radius > 0 && !ignore_borders) {
            if (intersect_line(ray, shape.positions[t.x], shape.positions[t.y],
                    shape.border_radius, shape.border_radius, line_end::cap,
                    line_end::cap, uv, distance, pos, norm)) {
              hit      = true;
              element  = bvh.primitives[idx];
              ray.tmax = distance;
              border   = true;
            }

            if (intersect_line(ray, shape.positions[t.y], shape.positions[t.z],
                    shape.border_radius, shape.border_radius, line_end::cap,
                    line_end::cap, uv, distance, pos, norm)) {
              hit      = true;
              element  = bvh.primitives[idx];
              ray.tmax = distance;
              border   = true;
            }
            if (intersect_line(ray, shape.positions[t.z], shape.positions[t.x],
                    shape.border_radius, shape.border_radius, line_end::cap,
                    line_end::cap, uv, distance, pos, norm)) {
              hit      = true;
              element  = bvh.primitives[idx];
              ray.tmax = distance;
              border   = true;
            }
          }
        }
      } else if (!shape.quads.empty()) {
        for (auto idx = node.start; idx < node.start + node.num; idx++) {
          auto& q = shape.quads[bvh.primitives[idx]];
          if (intersect_quad(ray, shape.positions[q.x], shape.positions[q.y],
                  shape.positions[q.z], shape.positions[q.w], uv, distance, pos,
                  norm)) {
            hit      = true;
            element  = bvh.primitives[idx];
            ray.tmax = distance;
            border   = false;
          }
          if (shape.border_radius > 0 && !ignore_borders) {
            if (intersect_line(ray, shape.positions[q.x], shape.positions[q.y],
                    shape.border_radius, shape.border_radius, line_end::cap,
                    line_end::cap, uv, distance, pos, norm)) {
              hit      = true;
              element  = bvh.primitives[idx];
              ray.tmax = distance;
              border   = true;
            }
            if (intersect_line(ray, shape.positions[q.y], shape.positions[q.z],
                    shape.border_radius, shape.border_radius, line_end::cap,
                    line_end::cap, uv, distance, pos, norm)) {
              hit      = true;
              element  = bvh.primitives[idx];
              ray.tmax = distance;
              border   = true;
            }
            if (intersect_line(ray, shape.positions[q.z], shape.positions[q.w],
                    shape.border_radius, shape.border_radius, line_end::cap,
                    line_end::cap, uv, distance, pos, norm)) {
              hit      = true;
              element  = bvh.primitives[idx];
              ray.tmax = distance;
              border   = true;
            }
            if (intersect_line(ray, shape.positions[q.w], shape.positions[q.x],
                    shape.border_radius, shape.border_radius, line_end::cap,
                    line_end::cap, uv, distance, pos, norm)) {
              hit      = true;
              element  = bvh.primitives[idx];
              ray.tmax = distance;
              border   = true;
            }
          }
        }
      }

      // check for early exit
      if (find_any && hit) return hit;
    }

    return hit;
  }

  // Intersect ray with a bvh.
  static bool intersect_bvh(const scene_bvh& bvh, const scene_data& scene,
      const ray3f& ray_, int& instance, int& element, vec2f& uv,
      float& distance, vec3f& pos, vec3f& norm, bool ignore_borders,
      bool find_any, bool& border) {
    // check empty
    if (bvh.nodes.empty()) return false;

    // node stack
    auto node_stack        = array<int, 128>{};
    auto node_cur          = 0;
    node_stack[node_cur++] = 0;

    // shared variables
    auto hit = false;

    // copy ray to modify it
    auto ray = ray_;

    // prepare ray for fast queries
    auto ray_dinv  = vec3f{1 / ray.d.x, 1 / ray.d.y, 1 / ray.d.z};
    auto ray_dsign = vec3i{(ray_dinv.x < 0) ? 1 : 0, (ray_dinv.y < 0) ? 1 : 0,
        (ray_dinv.z < 0) ? 1 : 0};

    // walking stack
    while (node_cur != 0) {
      // grab node
      auto& node = bvh.nodes[node_stack[--node_cur]];

      // intersect bbox
      // if (!intersect_bbox(ray, ray_dinv, ray_dsign, node.bbox)) continue;
      if (!intersect_bbox(ray, ray_dinv, node.bbox)) continue;

      // intersect node, switching based on node type
      // for each type, iterate over the the primitive list
      if (node.internal) {
        // for internal nodes, attempts to proceed along the
        // split axis from smallest to largest nodes
        if (ray_dsign[node.axis] != 0) {
          node_stack[node_cur++] = node.start + 0;
          node_stack[node_cur++] = node.start + 1;
        } else {
          node_stack[node_cur++] = node.start + 1;
          node_stack[node_cur++] = node.start + 0;
        }
      } else {
        for (auto idx = node.start; idx < node.start + node.num; idx++) {
          auto& instance_ = scene.instances[bvh.primitives[idx]];
          auto  inv_ray   = transform_ray(inverse(instance_.frame, true), ray);
          if (intersect_bvh(bvh.shapes[instance_.shape],
                  scene.shapes[instance_.shape], inv_ray, element, uv, distance,
                  pos, norm, ignore_borders, find_any, border)) {
            hit      = true;
            instance = bvh.primitives[idx];
            ray.tmax = distance;
          }
        }
      }

      // check for early exit
      if (find_any && hit) return hit;
    }

    return hit;
  }

  // Intersect ray with a bvh.
  static bool intersect_bvh(const scene_bvh& bvh, const scene_data& scene,
      int instance_, const ray3f& ray, int& element, vec2f& uv, float& distance,
      vec3f& pos, vec3f& norm, bool ignore_borders, bool find_any,
      bool& border) {
    auto& instance = scene.instances[instance_];
    auto  inv_ray  = transform_ray(inverse(instance.frame, true), ray);
    return intersect_bvh(bvh.shapes[instance.shape],
        scene.shapes[instance.shape], inv_ray, element, uv, distance, pos, norm,
        ignore_borders, find_any, border);
  }

  shape_intersection intersect_shape(const shape_bvh& bvh,
      const shape_data& shape, const ray3f& ray, bool ignore_borders,
      bool find_any) {
    auto intersection = shape_intersection{};
    intersection.hit  = intersect_bvh(bvh, shape, ray, intersection.element,
         intersection.uv, intersection.distance, intersection.position,
         intersection.normal, ignore_borders, find_any, intersection.border);
    return intersection;
  }
  scene_intersection intersect_scene(const scene_bvh& bvh,
      const scene_data& scene, const ray3f& ray, bool ignore_borders,
      bool find_any) {
    auto intersection = scene_intersection{};
    intersection.hit  = intersect_bvh(bvh, scene, ray, intersection.instance,
         intersection.element, intersection.uv, intersection.distance,
         intersection.position, intersection.normal, ignore_borders, find_any,
         intersection.border);
    return intersection;
  }
  scene_intersection intersect_instance(const scene_bvh& bvh,
      const scene_data& scene, int instance, const ray3f& ray,
      bool ignore_borders, bool find_any) {
    auto intersection     = scene_intersection{};
    intersection.hit      = intersect_bvh(bvh, scene, instance, ray,
             intersection.element, intersection.uv, intersection.distance,
             intersection.position, intersection.normal, ignore_borders, find_any,
             intersection.border);
    intersection.instance = instance;
    return intersection;
  }

}  // namespace yocto

// -----------------------------------------------------------------------------
// IMPLEMENTATION FOR BVH OVERLAP
// -----------------------------------------------------------------------------
namespace yocto {

  // Intersect ray with a bvh.
  static bool overlap_bvh(const shape_bvh& bvh, const shape_data& shape,
      const vec3f& pos, float max_distance, int& element, vec2f& uv,
      float& distance, bool find_any) {
    // check if empty
    if (bvh.nodes.empty()) return false;

    // node stack
    auto node_stack        = array<int, 64>{};
    auto node_cur          = 0;
    node_stack[node_cur++] = 0;

    // hit
    auto hit = false;

    // walking stack
    while (node_cur != 0) {
      // grab node
      auto& node = bvh.nodes[node_stack[--node_cur]];

      // intersect bbox
      if (!overlap_bbox(pos, max_distance, node.bbox)) continue;

      // intersect node, switching based on node type
      // for each type, iterate over the the primitive list
      if (node.internal) {
        // internal node
        node_stack[node_cur++] = node.start + 0;
        node_stack[node_cur++] = node.start + 1;
      } else if (!shape.points.empty()) {
        for (auto idx = 0; idx < node.num; idx++) {
          auto  primitive = bvh.primitives[node.start + idx];
          auto& p         = shape.points[primitive];
          if (overlap_point(pos, max_distance, shape.positions[p],
                  shape.radius[p], uv, distance)) {
            hit          = true;
            element      = primitive;
            max_distance = distance;
          }
        }
      } else if (!shape.lines.empty()) {
        for (auto idx = 0; idx < node.num; idx++) {
          auto  primitive = bvh.primitives[node.start + idx];
          auto& l         = shape.lines[primitive];
          if (overlap_line(pos, max_distance, shape.positions[l.x],
                  shape.positions[l.y], shape.radius[l.x], shape.radius[l.y],
                  uv, distance)) {
            hit          = true;
            element      = primitive;
            max_distance = distance;
          }
        }
      } else if (!shape.triangles.empty()) {
        for (auto idx = 0; idx < node.num; idx++) {
          auto  primitive = bvh.primitives[node.start + idx];
          auto& t         = shape.triangles[primitive];
          if (overlap_triangle(pos, max_distance, shape.positions[t.x],
                  shape.positions[t.y], shape.positions[t.z], shape.radius[t.x],
                  shape.radius[t.y], shape.radius[t.z], uv, distance)) {
            hit          = true;
            element      = primitive;
            max_distance = distance;
          }
        }
      } else if (!shape.quads.empty()) {
        for (auto idx = 0; idx < node.num; idx++) {
          auto  primitive = bvh.primitives[node.start + idx];
          auto& q         = shape.quads[primitive];
          if (overlap_quad(pos, max_distance, shape.positions[q.x],
                  shape.positions[q.y], shape.positions[q.z],
                  shape.positions[q.w], shape.radius[q.x], shape.radius[q.y],
                  shape.radius[q.z], shape.radius[q.w], uv, distance)) {
            hit          = true;
            element      = primitive;
            max_distance = distance;
          }
        }
      }

      // check for early exit
      if (find_any && hit) return hit;
    }

    return hit;
  }

  // Intersect ray with a bvh.
  static bool overlap_bvh(const scene_bvh& bvh, const scene_data& scene,
      const vec3f& pos, float max_distance, int& instance, int& element,
      vec2f& uv, float& distance, bool find_any) {
    // check if empty
    if (bvh.nodes.empty()) return false;

    // node stack
    auto node_stack        = array<int, 64>{};
    auto node_cur          = 0;
    node_stack[node_cur++] = 0;

    // hit
    auto hit = false;

    // walking stack
    while (node_cur != 0) {
      // grab node
      auto& node = bvh.nodes[node_stack[--node_cur]];

      // intersect bbox
      if (!overlap_bbox(pos, max_distance, node.bbox)) continue;

      // intersect node, switching based on node type
      // for each type, iterate over the the primitive list
      if (node.internal) {
        // internal node
        node_stack[node_cur++] = node.start + 0;
        node_stack[node_cur++] = node.start + 1;
      } else {
        for (auto idx = 0; idx < node.num; idx++) {
          auto  primitive = bvh.primitives[node.start + idx];
          auto& instance_ = scene.instances[primitive];
          auto& shape     = scene.shapes[instance_.shape];
          auto& sbvh      = bvh.shapes[instance_.shape];
          auto  inv_pos = transform_point(inverse(instance_.frame, true), pos);
          if (overlap_bvh(sbvh, shape, inv_pos, max_distance, element, uv,
                  distance, find_any)) {
            hit          = true;
            instance     = primitive;
            max_distance = distance;
          }
        }
      }

      // check for early exit
      if (find_any && hit) return hit;
    }

    return hit;
  }

#if 0
// Finds the overlap between BVH leaf nodes.
template <typename OverlapElem>
void overlap_bvh_elems(const bvh_data& bvh1, const bvh_data& bvh2,
                bool skip_duplicates, bool skip_self, vector<vec2i>& overlaps,
                const OverlapElem& overlap_elems) {
    // node stack
    vec2i node_stack[128];
    auto node_cur = 0;
    node_stack[node_cur++] = {0, 0};

    // walking stack
    while (node_cur) {
        // grab node
        auto node_idx = node_stack[--node_cur];
        const auto node1 = bvh1->nodes[node_idx.x];
        const auto node2 = bvh2->nodes[node_idx.y];

        // intersect bbox
        if (!overlap_bbox(node1.bbox, node2.bbox)) continue;

        // check for leaves
        if (node1.isleaf && node2.isleaf) {
            // collide primitives
            for (auto i1 = node1.start; i1 < node1.start + node1.count; i1++) {
                for (auto i2 = node2.start; i2 < node2.start + node2.count;
                      i2++) {
                    auto idx1 = bvh1->sorted_prim[i1];
                    auto idx2 = bvh2->sorted_prim[i2];
                    if (skip_duplicates && idx1 > idx2) continue;
                    if (skip_self && idx1 == idx2) continue;
                    if (overlap_elems(idx1, idx2))
                        overlaps.push_back({idx1, idx2});
                }
            }
        } else {
            // descend
            if (node1.isleaf) {
                for (auto idx2 = node2.start; idx2 < node2.start + node2.count;
                      idx2++) {
                    node_stack[node_cur++] = {node_idx.x, (int)idx2};
                }
            } else if (node2.isleaf) {
                for (auto idx1 = node1.start; idx1 < node1.start + node1.count;
                      idx1++) {
                    node_stack[node_cur++] = {(int)idx1, node_idx.y};
                }
            } else {
                for (auto idx2 = node2.start; idx2 < node2.start + node2.count;
                      idx2++) {
                    for (auto idx1 = node1.start;
                          idx1 < node1.start + node1.count; idx1++) {
                        node_stack[node_cur++] = {(int)idx1, (int)idx2};
                    }
                }
            }
        }
    }
}
#endif

  scene_intersection overlap_scene(const scene_bvh& bvh,
      const scene_data& scene, const vec3f& pos, float max_distance,
      bool find_any) {
    auto intersection = scene_intersection{};
    intersection.hit  = overlap_bvh(bvh, scene, pos, max_distance,
         intersection.instance, intersection.element, intersection.uv,
         intersection.distance, find_any);
    return intersection;
  }

}  // namespace yocto
