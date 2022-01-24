//
// # Yocto/Dgram BVH: Accelerated ray-intersections
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

#include "yocto_dgram_bvh.h"

#include <yocto/yocto_geometry.h>

#include <algorithm>
#include <future>

#include "yocto_dgram_intersection.h"

// -----------------------------------------------------------------------------
// USING DIRECTIVES
// -----------------------------------------------------------------------------
namespace yocto {

  // using directives
  using std::array;
  using std::sort;

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
// BVH BUILD
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
  static void build_bvh(vector<dgram_bvh_node>& nodes, vector<int>& primitives,
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

  dgram_shape_bvh make_bvh(const trace_shape& shape, bool highquality) {
    auto bvh = dgram_shape_bvh{};

    auto bboxes = vector<bbox3f>{};

    for (auto& point : shape.points) {
      auto& bbox = bboxes.emplace_back();
      bbox       = point_bounds(shape.positions[point], shape.radii[point] * 3);
    }

    for (auto idx = 0; idx < shape.lines.size(); idx++) {
      auto& line = shape.lines[idx];
      auto& end  = shape.ends[idx];
      auto& bbox = bboxes.emplace_back();
      bbox       = line_bounds(shape.positions[line.x], shape.positions[line.y],
                shape.radii[line.x], shape.radii[line.y], end.a, end.b);
    }

    for (auto& triangle : shape.triangles) {
      auto& bbox = bboxes.emplace_back();
      bbox       = triangle_bounds(shape.positions[triangle.x],
                shape.positions[triangle.y], shape.positions[triangle.z]);
    }

    for (auto& quad : shape.quads) {
      auto& bbox = bboxes.emplace_back();
      bbox       = quad_bounds(shape.positions[quad.x], shape.positions[quad.y],
                shape.positions[quad.z], shape.positions[quad.w]);
    }

    for (auto& border : shape.borders) {
      auto& bbox = bboxes.emplace_back();
      bbox = line_bounds(shape.positions[border.x], shape.positions[border.y],
          shape.radii[border.x], shape.radii[border.y], line_end::cap,
          line_end::cap);
    }

    build_bvh(bvh.nodes, bvh.primitives, bboxes, highquality);

    return bvh;
  }

  dgram_scene_bvh make_bvh(
      const trace_shapes& shapes, bool highquality, bool noparallel) {
    auto bvh    = dgram_scene_bvh{};
    auto bboxes = vector<bbox3f>{};

    bvh.shapes.resize(shapes.shapes.size());
    bboxes.resize(shapes.shapes.size());
    if (noparallel) {
      for (auto idx = (size_t)0; idx < shapes.shapes.size(); idx++) {
        auto dgram_shape_bvh = make_bvh(shapes.shapes[idx], highquality);
        bvh.shapes[idx]      = dgram_shape_bvh;
        bboxes[idx]          = dgram_shape_bvh.nodes[0].bbox;
      }
    } else {
      parallel_for(shapes.shapes.size(), [&](size_t idx) {
        auto dgram_shape_bvh = make_bvh(shapes.shapes[idx], highquality);
        bvh.shapes[idx]      = dgram_shape_bvh;
        bboxes[idx]          = dgram_shape_bvh.nodes[0].bbox;
      });
    }

    build_bvh(bvh.nodes, bvh.primitives, bboxes, highquality);

    return bvh;
  }
}  // namespace yocto

// -----------------------------------------------------------------------------
// BVH INTERSECTION
// -----------------------------------------------------------------------------
namespace yocto {

  static void intersect_bvh(const dgram_shape_bvh& bvh,
      const trace_shape& shape, const int& shape_id, ray3f& ray,
      bvh_intersections& intersections) {
    // check empty
    if (bvh.nodes.empty()) return;

    // node stack
    auto node_stack        = array<int, 128>{};
    auto node_cur          = 0;
    node_stack[node_cur++] = 0;

    // shared variables
    vec2f uv   = {0, 0};
    float dist = 0;
    vec3f pos  = {0, 0, 0};
    vec3f norm = {0, 0, 0};

    // prepare ray for fast queries
    auto ray_dinv  = vec3f{1 / ray.d.x, 1 / ray.d.y, 1 / ray.d.z};
    auto ray_dsign = vec3i{(ray_dinv.x < 0) ? 1 : 0, (ray_dinv.y < 0) ? 1 : 0,
        (ray_dinv.z < 0) ? 1 : 0};

    // walking stack
    while (node_cur != 0) {
      // grab node
      auto& node = bvh.nodes[node_stack[--node_cur]];

      // intersect bbox
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
            if (intersect_point(ray, shape.positions[p], shape.radii[p] * 3, uv,
                    dist, pos, norm)) {
              if (dist < ray.tmax - ray_eps)
                intersections.intersections.clear();
              ray.tmax = dist;

              auto intersection = bvh_intersection{
                  .shape    = shape_id,
                  .element  = shape_element{primitive_type::point, i},
                  .uv       = uv,
                  .distance = dist,
                  .position = pos,
                  .normal   = norm,
              };
              intersections.intersections.push_back(intersection);
            }
          } else if (i -= shape.points.size(), size += shape.lines.size();
                     prim < size) {
            auto& l   = shape.lines[i];
            auto& end = shape.ends[i];
            if (intersect_line(ray, shape.positions[l.x], shape.positions[l.y],
                    shape.radii[l.x], shape.radii[l.y], end.a, end.b, uv, dist,
                    pos, norm)) {
              if (dist < ray.tmax - ray_eps)
                intersections.intersections.clear();
              ray.tmax = dist;

              auto intersection = bvh_intersection{
                  .shape    = shape_id,
                  .element  = shape_element{primitive_type::line, i},
                  .uv       = uv,
                  .distance = dist,
                  .position = pos,
                  .normal   = norm,
              };
              intersections.intersections.push_back(intersection);
            }
          } else if (i -= shape.lines.size(), size += shape.triangles.size();
                     prim < size) {
            auto& t = shape.triangles[i];
            if (intersect_triangle(ray, shape.positions[t.x],
                    shape.positions[t.y], shape.positions[t.z], uv, dist, pos,
                    norm)) {
              if (dist < ray.tmax - ray_eps)
                intersections.intersections.clear();
              ray.tmax = dist;

              auto intersection = bvh_intersection{
                  .shape    = shape_id,
                  .element  = shape_element{primitive_type::triangle, i},
                  .uv       = uv,
                  .distance = dist,
                  .position = pos,
                  .normal   = norm,
              };
              intersections.intersections.push_back(intersection);
            }
          } else if (i -= shape.triangles.size(), size += shape.quads.size();
                     prim < size) {
            auto& q = shape.quads[i];
            if (intersect_quad(ray, shape.positions[q.x], shape.positions[q.y],
                    shape.positions[q.z], shape.positions[q.w], uv, dist, pos,
                    norm)) {
              if (dist < ray.tmax - ray_eps)
                intersections.intersections.clear();
              ray.tmax = dist;

              auto intersection = bvh_intersection{
                  .shape    = shape_id,
                  .element  = shape_element{primitive_type::quad, i},
                  .uv       = uv,
                  .distance = dist,
                  .position = pos,
                  .normal   = norm,
              };
              intersections.intersections.push_back(intersection);
            }
          } else if (i -= shape.quads.size(), size += shape.borders.size();
                     prim < size) {
            auto& b = shape.borders[i];
            if (intersect_line(ray, shape.positions[b.x], shape.positions[b.y],
                    shape.radii[b.x], shape.radii[b.y], line_end::cap,
                    line_end::cap, uv, dist, pos, norm)) {
              if (dist < ray.tmax - ray_eps)
                intersections.intersections.clear();
              ray.tmax = dist;

              auto intersection = bvh_intersection{
                  .shape    = shape_id,
                  .element  = shape_element{primitive_type::border, i},
                  .uv       = uv,
                  .distance = dist,
                  .position = pos,
                  .normal   = norm,
              };
              intersections.intersections.push_back(intersection);
            }
          }
        }
      }
    }
  }

  bvh_intersections intersect_bvh(const dgram_scene_bvh& bvh,
      const trace_shapes& shapes, const ray3f& ray_) {
    auto intersections = bvh_intersections{};

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
          auto  id    = bvh.primitives[idx];
          auto& shape = shapes.shapes[id];

          auto skip = false;
          auto uv   = zero2f;
          auto dist = 0.0f;

          for (auto& cclip : shape.cclip_indices) {
            if (!intersect_triangle(ray, shape.cclip_positions[cclip.x],
                    shape.cclip_positions[cclip.y],
                    shape.cclip_positions[cclip.z], uv, dist)) {
              skip = true;
            }
          }

          if (!skip) {
            intersect_bvh(
                bvh.shapes[id], shapes.shapes[id], id, ray, intersections);
          }
        }
      }
    }

    // sort
    sort(
        intersections.intersections.begin(), intersections.intersections.end());

    return intersections;
  }

}  // namespace yocto