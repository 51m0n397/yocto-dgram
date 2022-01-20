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

  instance_bvh make_bvh(
      const shape_data& shape, const frame3f& frame, bool highquality) {
    // bvh
    auto bvh = instance_bvh{};

    // build primitives
    auto bboxes = vector<bbox3f>{};
    if (!shape.points.empty()) {
      bboxes.resize(shape.points.size());
      for (auto idx = 0; idx < shape.points.size(); idx++) {
        auto& point = shape.points[idx];
        bboxes[idx] = point_bounds(
            transform_point(frame, shape.positions[point]),
            shape.radius[point] * 3);
      }
    }
    if (!shape.lines.empty()) {
      auto current_size = bboxes.size();
      bboxes.resize(current_size + shape.lines.size());
      for (auto idx = 0; idx < shape.lines.size(); idx++) {
        auto& line                 = shape.lines[idx];
        bboxes[current_size + idx] = line_bounds(
            transform_point(frame, shape.positions[line.x]),
            transform_point(frame, shape.positions[line.y]),
            shape.radius[line.x], shape.radius[line.y], shape.ends[line.x],
            shape.ends[line.y]);
      }
    }
    if (!shape.triangles.empty()) {
      auto current_size = bboxes.size();
      bboxes.resize(current_size + shape.triangles.size());
      for (auto idx = 0; idx < shape.triangles.size(); idx++) {
        auto& triangle             = shape.triangles[idx];
        bboxes[current_size + idx] = triangle_bounds(
            transform_point(frame, shape.positions[triangle.x]),
            transform_point(frame, shape.positions[triangle.y]),
            transform_point(frame, shape.positions[triangle.z]));
      }
    }
    if (!shape.quads.empty()) {
      auto current_size = bboxes.size();
      bboxes.resize(current_size + shape.quads.size());
      for (auto idx = 0; idx < shape.quads.size(); idx++) {
        auto& quad                 = shape.quads[idx];
        bboxes[current_size + idx] = quad_bounds(
            transform_point(frame, shape.positions[quad.x]),
            transform_point(frame, shape.positions[quad.y]),
            transform_point(frame, shape.positions[quad.z]),
            transform_point(frame, shape.positions[quad.w]));
      }
    }
    if (!shape.borders.empty()) {
      auto current_size = bboxes.size();
      bboxes.resize(current_size + shape.borders.size());
      for (auto idx = 0; idx < shape.borders.size(); idx++) {
        auto& border               = shape.borders[idx];
        bboxes[current_size + idx] = line_bounds(
            transform_point(frame, shape.positions[border.x]),
            transform_point(frame, shape.positions[border.y]),
            shape.radius[border.x], shape.radius[border.y], line_end::cap,
            line_end::cap);
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

    auto bboxes = vector<bbox3f>{};
    for (auto& instance : scene.instances) {
      auto& shape        = scene.shapes[instance.shape];
      auto  instance_bvh = make_bvh(shape, instance.frame, highquality);
      bvh.instances.push_back(instance_bvh);
      auto& bbox = instance_bvh.nodes[0].bbox;
      bboxes.push_back(bbox);
    }

    // build nodes
    build_bvh(bvh.nodes, bvh.primitives, bboxes, highquality);

    // done
    return bvh;
  }

}  // namespace yocto

// -----------------------------------------------------------------------------
// IMPLEMENTATION FOR BVH INTERSECTION
// -----------------------------------------------------------------------------
namespace yocto {

  // Intersect ray with a bvh.
  static bool intersect_bvh(const instance_bvh& bvh, const frame3f& frame,
      const shape_data& shape, const ray3f& ray_, int& element, vec2f& uv,
      float& distance, vec3f& pos, vec3f& norm, bool find_any,
      primitive_type& primitive) {
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
          auto prim = bvh.primitives[idx];
          auto i    = prim;
          auto size = shape.points.size();
          if (prim < size) {
            auto& p = shape.points[i];
            if (intersect_point(ray, transform_point(frame, shape.positions[p]),
                    shape.radius[p] * 3, uv, distance, pos, norm)) {
              hit       = true;
              element   = i;
              ray.tmax  = distance;
              primitive = primitive_type::point;
            }
          } else if (i -= shape.points.size(), size += shape.lines.size();
                     prim < size) {
            auto& l = shape.lines[i];
            if (intersect_line(ray,
                    transform_point(frame, shape.positions[l.x]),
                    transform_point(frame, shape.positions[l.y]),
                    shape.radius[l.x], shape.radius[l.y], shape.ends[l.x],
                    shape.ends[l.y], uv, distance, pos, norm)) {
              hit       = true;
              element   = i;
              ray.tmax  = distance;
              primitive = primitive_type::line;
            }
          } else if (i -= shape.lines.size(), size += shape.triangles.size();
                     prim < size) {
            auto& t = shape.triangles[i];
            if (intersect_triangle(ray,
                    transform_point(frame, shape.positions[t.x]),
                    transform_point(frame, shape.positions[t.y]),
                    transform_point(frame, shape.positions[t.z]), uv, distance,
                    pos, norm)) {
              hit       = true;
              element   = i;
              ray.tmax  = distance;
              primitive = primitive_type::triangle;
            }
          } else if (i -= shape.triangles.size(), size += shape.quads.size();
                     prim < size) {
            auto& q = shape.quads[i];
            if (intersect_quad(ray,
                    transform_point(frame, shape.positions[q.x]),
                    transform_point(frame, shape.positions[q.y]),
                    transform_point(frame, shape.positions[q.z]),
                    transform_point(frame, shape.positions[q.w]), uv, distance,
                    pos, norm)) {
              hit       = true;
              element   = i;
              ray.tmax  = distance;
              primitive = primitive_type::quad;
            }
          } else if (i -= shape.quads.size(), size += shape.borders.size();
                     prim < size) {
            auto& b = shape.borders[i];
            if (intersect_line(ray,
                    transform_point(frame, shape.positions[b.x]),
                    transform_point(frame, shape.positions[b.y]),
                    shape.radius[b.x], shape.radius[b.y], line_end::cap,
                    line_end::cap, uv, distance, pos, norm)) {
              hit       = true;
              element   = i;
              ray.tmax  = distance;
              primitive = primitive_type::border;
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
  static set<instance_intersection> intersect_bvh(const scene_bvh& bvh,
      const scene_data& scene, const ray3f& ray_, bool find_any) {
    auto intersections = set<instance_intersection>{};

    // check empty
    if (bvh.nodes.empty()) return intersections;

    // node stack
    auto node_stack        = array<int, 128>{};
    auto node_cur          = 0;
    node_stack[node_cur++] = 0;

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
          auto& shape_    = scene.shapes[instance_.shape];

          auto intersection = instance_intersection{};
          if (!shape_.cclips.empty()) {
            if (!intersect_triangle(ray, shape_.cclips[0], shape_.cclips[1],
                    shape_.cclips[2], intersection.uv, intersection.distance,
                    intersection.position, intersection.normal))
              continue;
          }
          if (intersect_bvh(bvh.instances[bvh.primitives[idx]], instance_.frame,
                  scene.shapes[instance_.shape], ray, intersection.element,
                  intersection.uv, intersection.distance, intersection.position,
                  intersection.normal, find_any, intersection.primitive)) {
            if (intersection.distance < ray.tmax-ray_eps) intersections.clear();
            intersection.hit      = true;
            intersection.instance = bvh.primitives[idx];
            ray.tmax              = intersection.distance;
            intersections.insert(intersection);
            if (find_any) return intersections;
          }
        }
      }
    }

    return intersections;
  }

  // Intersect ray with a bvh.
  shape_intersection intersect_shape(const instance_bvh& bvh,
      const shape_data& shape, const ray3f& ray, bool find_any) {
    auto intersection = shape_intersection{};
    intersection.hit  = intersect_bvh(bvh, identity3x4f, shape, ray, intersection.element,
         intersection.uv, intersection.distance, intersection.position,
         intersection.normal, find_any, intersection.primitive);
    return intersection;
  }

  /*instance_intersection intersect_instance(const scene_bvh& bvh,
      const scene_data& scene, int instance_, const ray3f& ray, bool find_any) {
    auto  intersection    = instance_intersection{};
    auto& instance        = scene.instances[instance_];
    auto  inv_ray         = transform_ray(inverse(instance.frame, true), ray);
    intersection.hit      = intersect_bvh(bvh.shapes[instance.shape],
             scene.shapes[instance.shape], inv_ray, intersection.element,
             intersection.uv, intersection.distance, intersection.position,
             intersection.normal, find_any, intersection.primitive);
    intersection.instance = instance_;
    return intersection;
  }*/

  set<instance_intersection> intersect_scene(const scene_bvh& bvh,
      const scene_data& scene, const ray3f& ray, bool find_any) {
    return intersect_bvh(bvh, scene, ray, find_any);
  }

}  // namespace yocto