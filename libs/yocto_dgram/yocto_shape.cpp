//
// Implementation for Yocto/Shape
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

#include "yocto_shape.h"

#include <algorithm>
#include <deque>
#include <memory>
#include <stdexcept>
#include <string>

#include "yocto_geometry.h"
#include "yocto_modelio.h"
#include "yocto_noise.h"
#include "yocto_sampling.h"

// -----------------------------------------------------------------------------
// USING DIRECTIVES
// -----------------------------------------------------------------------------
namespace yocto {

  // using directives
  using std::deque;
  using namespace std::string_literals;

}  // namespace yocto

// -----------------------------------------------------------------------------
// IMPLEMENTATION FO SHAPE PROPERTIES
// -----------------------------------------------------------------------------
namespace yocto {

  // Interpolate vertex data
  vec3f eval_position(const shape_data& shape, int element, const vec2f& uv) {
    if (!shape.points.empty()) {
      auto& point = shape.points[element];
      return shape.positions[point];
    } else if (!shape.lines.empty()) {
      auto& line = shape.lines[element];
      return interpolate_line(
          shape.positions[line.x], shape.positions[line.y], uv.x);
    } else if (!shape.triangles.empty()) {
      auto& triangle = shape.triangles[element];
      return interpolate_triangle(shape.positions[triangle.x],
          shape.positions[triangle.y], shape.positions[triangle.z], uv);
    } else if (!shape.quads.empty()) {
      auto& quad = shape.quads[element];
      return interpolate_quad(shape.positions[quad.x], shape.positions[quad.y],
          shape.positions[quad.z], shape.positions[quad.w], uv);
    } else {
      return {0, 0, 0};
    }
  }

  vec3f eval_normal(const shape_data& shape, int element, const vec2f& uv) {
    if (shape.normals.empty()) return eval_element_normal(shape, element);
    if (!shape.points.empty()) {
      auto& point = shape.points[element];
      return normalize(shape.normals[point]);
    } else if (!shape.lines.empty()) {
      auto& line = shape.lines[element];
      return normalize(
          interpolate_line(shape.normals[line.x], shape.normals[line.y], uv.x));
    } else if (!shape.triangles.empty()) {
      auto& triangle = shape.triangles[element];
      return normalize(interpolate_triangle(shape.normals[triangle.x],
          shape.normals[triangle.y], shape.normals[triangle.z], uv));
    } else if (!shape.quads.empty()) {
      auto& quad = shape.quads[element];
      return normalize(
          interpolate_quad(shape.normals[quad.x], shape.normals[quad.y],
              shape.normals[quad.z], shape.normals[quad.w], uv));
    } else {
      return {0, 0, 1};
    }
  }

  vec3f eval_tangent(const shape_data& shape, int element, const vec2f& uv) {
    return eval_normal(shape, element, uv);
  }

  vec2f eval_texcoord(const shape_data& shape, int element, const vec2f& uv) {
    if (shape.texcoords.empty()) return uv;
    if (!shape.points.empty()) {
      auto& point = shape.points[element];
      return shape.texcoords[point];
    } else if (!shape.lines.empty()) {
      auto& line = shape.lines[element];
      return interpolate_line(
          shape.texcoords[line.x], shape.texcoords[line.y], uv.x);
    } else if (!shape.triangles.empty()) {
      auto& triangle = shape.triangles[element];
      return interpolate_triangle(shape.texcoords[triangle.x],
          shape.texcoords[triangle.y], shape.texcoords[triangle.z], uv);
    } else if (!shape.quads.empty()) {
      auto& quad = shape.quads[element];
      return interpolate_quad(shape.texcoords[quad.x], shape.texcoords[quad.y],
          shape.texcoords[quad.z], shape.texcoords[quad.w], uv);
    } else {
      return uv;
    }
  }

  vec4f eval_color(const shape_data& shape, int element, const vec2f& uv) {
    if (shape.colors.empty()) return {1, 1, 1, 1};
    if (!shape.points.empty()) {
      auto& point = shape.points[element];
      return shape.colors[point];
    } else if (!shape.lines.empty()) {
      auto& line = shape.lines[element];
      return interpolate_line(shape.colors[line.x], shape.colors[line.y], uv.x);
    } else if (!shape.triangles.empty()) {
      auto& triangle = shape.triangles[element];
      return interpolate_triangle(shape.colors[triangle.x],
          shape.colors[triangle.y], shape.colors[triangle.z], uv);
    } else if (!shape.quads.empty()) {
      auto& quad = shape.quads[element];
      return interpolate_quad(shape.colors[quad.x], shape.colors[quad.y],
          shape.colors[quad.z], shape.colors[quad.w], uv);
    } else {
      return {0, 0};
    }
  }

  float eval_radius(const shape_data& shape, int element, const vec2f& uv) {
    if (shape.radius.empty()) return 0;
    if (!shape.points.empty()) {
      auto& point = shape.points[element];
      return shape.radius[point];
    } else if (!shape.lines.empty()) {
      auto& line = shape.lines[element];
      return interpolate_line(shape.radius[line.x], shape.radius[line.y], uv.x);
    } else if (!shape.triangles.empty()) {
      auto& triangle = shape.triangles[element];
      return interpolate_triangle(shape.radius[triangle.x],
          shape.radius[triangle.y], shape.radius[triangle.z], uv);
    } else if (!shape.quads.empty()) {
      auto& quad = shape.quads[element];
      return interpolate_quad(shape.radius[quad.x], shape.radius[quad.y],
          shape.radius[quad.z], shape.radius[quad.w], uv);
    } else {
      return 0;
    }
  }

  // Evaluate element normals
  vec3f eval_element_normal(const shape_data& shape, int element) {
    if (!shape.points.empty()) {
      return {0, 0, 1};
    } else if (!shape.lines.empty()) {
      auto& line = shape.lines[element];
      return line_tangent(shape.positions[line.x], shape.positions[line.y]);
    } else if (!shape.triangles.empty()) {
      auto& triangle = shape.triangles[element];
      return triangle_normal(shape.positions[triangle.x],
          shape.positions[triangle.y], shape.positions[triangle.z]);
    } else if (!shape.quads.empty()) {
      auto& quad = shape.quads[element];
      return quad_normal(shape.positions[quad.x], shape.positions[quad.y],
          shape.positions[quad.z], shape.positions[quad.w]);
    } else {
      return {0, 0, 0};
    }
  }

  // Compute per-vertex normals/tangents for lines/triangles/quads.
  vector<vec3f> compute_normals(const shape_data& shape) {
    if (!shape.points.empty()) {
      return vector<vec3f>(shape.positions.size(), {0, 0, 1});
    } else if (!shape.lines.empty()) {
      return lines_tangents(shape.lines, shape.positions);
    } else if (!shape.triangles.empty()) {
      return triangles_normals(shape.triangles, shape.positions);
    } else if (!shape.quads.empty()) {
      return quads_normals(shape.quads, shape.positions);
    } else {
      return vector<vec3f>(shape.positions.size(), {0, 0, 1});
    }
  }
  void compute_normals(vector<vec3f>& normals, const shape_data& shape) {
    if (!shape.points.empty()) {
      normals.assign(shape.positions.size(), {0, 0, 1});
    } else if (!shape.lines.empty()) {
      lines_tangents(normals, shape.lines, shape.positions);
    } else if (!shape.triangles.empty()) {
      triangles_normals(normals, shape.triangles, shape.positions);
    } else if (!shape.quads.empty()) {
      quads_normals(normals, shape.quads, shape.positions);
    } else {
      normals.assign(shape.positions.size(), {0, 0, 1});
    }
  }

  // Shape sampling
  vector<float> sample_shape_cdf(const shape_data& shape) {
    if (!shape.points.empty()) {
      return sample_points_cdf((int)shape.points.size());
    } else if (!shape.lines.empty()) {
      return sample_lines_cdf(shape.lines, shape.positions);
    } else if (!shape.triangles.empty()) {
      return sample_triangles_cdf(shape.triangles, shape.positions);
    } else if (!shape.quads.empty()) {
      return sample_quads_cdf(shape.quads, shape.positions);
    } else {
      return sample_points_cdf((int)shape.positions.size());
    }
  }

  void sample_shape_cdf(vector<float>& cdf, const shape_data& shape) {
    if (!shape.points.empty()) {
      sample_points_cdf(cdf, (int)shape.points.size());
    } else if (!shape.lines.empty()) {
      sample_lines_cdf(cdf, shape.lines, shape.positions);
    } else if (!shape.triangles.empty()) {
      sample_triangles_cdf(cdf, shape.triangles, shape.positions);
    } else if (!shape.quads.empty()) {
      sample_quads_cdf(cdf, shape.quads, shape.positions);
    } else {
      sample_points_cdf(cdf, (int)shape.positions.size());
    }
  }

  shape_point sample_shape(const shape_data& shape, const vector<float>& cdf,
      float rn, const vec2f& ruv) {
    if (!shape.points.empty()) {
      auto element = sample_points(cdf, rn);
      return {element, {0, 0}};
    } else if (!shape.lines.empty()) {
      auto [element, u] = sample_lines(cdf, rn, ruv.x);
      return {element, {u, 0}};
    } else if (!shape.triangles.empty()) {
      auto [element, uv] = sample_triangles(cdf, rn, ruv);
      return {element, uv};
    } else if (!shape.quads.empty()) {
      auto [element, uv] = sample_quads(cdf, rn, ruv);
      return {element, uv};
    } else {
      auto element = sample_points(cdf, rn);
      return {element, {0, 0}};
    }
  }

  vector<shape_point> sample_shape(
      const shape_data& shape, int num_samples, uint64_t seed) {
    auto cdf    = sample_shape_cdf(shape);
    auto points = vector<shape_point>(num_samples);
    auto rng    = make_rng(seed);
    for (auto& point : points) {
      point = sample_shape(shape, cdf, rand1f(rng), rand2f(rng));
    }
    return points;
  }

  // Conversions
  shape_data quads_to_triangles(const shape_data& shape) {
    auto result = shape;
    if (!shape.quads.empty()) {
      result.triangles = quads_to_triangles(shape.quads);
      result.quads     = {};
    }
    return result;
  }
  void quads_to_triangles_inplace(shape_data& shape) {
    if (shape.quads.empty()) return;
    shape.triangles = quads_to_triangles(shape.quads);
    shape.quads     = {};
  }

  vector<string> shape_stats(const shape_data& shape, bool verbose) {
    auto format = [](auto num) {
      auto str = std::to_string(num);
      while (str.size() < 13) str = " " + str;
      return str;
    };
    auto format3 = [](auto num) {
      auto str = std::to_string(num.x) + " " + std::to_string(num.y) + " " +
                 std::to_string(num.z);
      while (str.size() < 13) str = " " + str;
      return str;
    };

    auto bbox = invalidb3f;
    for (auto& pos : shape.positions) bbox = merge(bbox, pos);

    auto stats = vector<string>{};
    stats.push_back("points:       " + format(shape.points.size()));
    stats.push_back("lines:        " + format(shape.lines.size()));
    stats.push_back("triangles:    " + format(shape.triangles.size()));
    stats.push_back("quads:        " + format(shape.quads.size()));
    stats.push_back("positions:    " + format(shape.positions.size()));
    stats.push_back("normals:      " + format(shape.normals.size()));
    stats.push_back("texcoords:    " + format(shape.texcoords.size()));
    stats.push_back("colors:       " + format(shape.colors.size()));
    stats.push_back("radius:       " + format(shape.radius.size()));
    stats.push_back("center:       " + format3(center(bbox)));
    stats.push_back("size:         " + format3(size(bbox)));
    stats.push_back("min:          " + format3(bbox.min));
    stats.push_back("max:          " + format3(bbox.max));

    return stats;
  }

}  // namespace yocto

// -----------------------------------------------------------------------------
// IMPLEMENTATION OF COMPUTATION OF PER-VERTEX PROPERTIES
// -----------------------------------------------------------------------------
namespace yocto {

  // Compute per-vertex tangents for lines.
  vector<vec3f> lines_tangents(
      const vector<vec2i>& lines, const vector<vec3f>& positions) {
    auto tangents = vector<vec3f>{positions.size()};
    for (auto& tangent : tangents) tangent = {0, 0, 0};
    for (auto& l : lines) {
      auto tangent = line_tangent(positions[l.x], positions[l.y]);
      auto length  = line_length(positions[l.x], positions[l.y]);
      tangents[l.x] += tangent * length;
      tangents[l.y] += tangent * length;
    }
    for (auto& tangent : tangents) tangent = normalize(tangent);
    return tangents;
  }

  // Compute per-vertex normals for triangles.
  vector<vec3f> triangles_normals(
      const vector<vec3i>& triangles, const vector<vec3f>& positions) {
    auto normals = vector<vec3f>{positions.size()};
    for (auto& normal : normals) normal = {0, 0, 0};
    for (auto& t : triangles) {
      auto normal = triangle_normal(
          positions[t.x], positions[t.y], positions[t.z]);
      auto area = triangle_area(positions[t.x], positions[t.y], positions[t.z]);
      normals[t.x] += normal * area;
      normals[t.y] += normal * area;
      normals[t.z] += normal * area;
    }
    for (auto& normal : normals) normal = normalize(normal);
    return normals;
  }

  // Compute per-vertex normals for quads.
  vector<vec3f> quads_normals(
      const vector<vec4i>& quads, const vector<vec3f>& positions) {
    auto normals = vector<vec3f>{positions.size()};
    for (auto& normal : normals) normal = {0, 0, 0};
    for (auto& q : quads) {
      auto normal = quad_normal(
          positions[q.x], positions[q.y], positions[q.z], positions[q.w]);
      auto area = quad_area(
          positions[q.x], positions[q.y], positions[q.z], positions[q.w]);
      normals[q.x] += normal * area;
      normals[q.y] += normal * area;
      normals[q.z] += normal * area;
      if (q.z != q.w) normals[q.w] += normal * area;
    }
    for (auto& normal : normals) normal = normalize(normal);
    return normals;
  }

  // Compute per-vertex tangents for lines.
  void lines_tangents(vector<vec3f>& tangents, const vector<vec2i>& lines,
      const vector<vec3f>& positions) {
    if (tangents.size() != positions.size()) {
      throw std::out_of_range("array should be the same length");
    }
    for (auto& tangent : tangents) tangent = {0, 0, 0};
    for (auto& l : lines) {
      auto tangent = line_tangent(positions[l.x], positions[l.y]);
      auto length  = line_length(positions[l.x], positions[l.y]);
      tangents[l.x] += tangent * length;
      tangents[l.y] += tangent * length;
    }
    for (auto& tangent : tangents) tangent = normalize(tangent);
  }

  // Compute per-vertex normals for triangles.
  void triangles_normals(vector<vec3f>& normals, const vector<vec3i>& triangles,
      const vector<vec3f>& positions) {
    if (normals.size() != positions.size()) {
      throw std::out_of_range("array should be the same length");
    }
    for (auto& normal : normals) normal = {0, 0, 0};
    for (auto& t : triangles) {
      auto normal = triangle_normal(
          positions[t.x], positions[t.y], positions[t.z]);
      auto area = triangle_area(positions[t.x], positions[t.y], positions[t.z]);
      normals[t.x] += normal * area;
      normals[t.y] += normal * area;
      normals[t.z] += normal * area;
    }
    for (auto& normal : normals) normal = normalize(normal);
  }

  // Compute per-vertex normals for quads.
  void quads_normals(vector<vec3f>& normals, const vector<vec4i>& quads,
      const vector<vec3f>& positions) {
    if (normals.size() != positions.size()) {
      throw std::out_of_range("array should be the same length");
    }
    for (auto& normal : normals) normal = {0, 0, 0};
    for (auto& q : quads) {
      auto normal = quad_normal(
          positions[q.x], positions[q.y], positions[q.z], positions[q.w]);
      auto area = quad_area(
          positions[q.x], positions[q.y], positions[q.z], positions[q.w]);
      normals[q.x] += normal * area;
      normals[q.y] += normal * area;
      normals[q.z] += normal * area;
      if (q.z != q.w) normals[q.w] += normal * area;
    }
    for (auto& normal : normals) normal = normalize(normal);
  }

  // Compute per-vertex tangent frame for triangle meshes.
  // Tangent space is defined by a four component vector.
  // The first three components are the tangent with respect to the U texcoord.
  // The fourth component is the sign of the tangent wrt the V texcoord.
  // Tangent frame is useful in normal mapping.
  vector<vec4f> triangles_tangent_spaces(const vector<vec3i>& triangles,
      const vector<vec3f>& positions, const vector<vec3f>& normals,
      const vector<vec2f>& texcoords) {
    auto tangu = vector<vec3f>(positions.size(), vec3f{0, 0, 0});
    auto tangv = vector<vec3f>(positions.size(), vec3f{0, 0, 0});
    for (auto t : triangles) {
      auto tutv = triangle_tangents_fromuv(positions[t.x], positions[t.y],
          positions[t.z], texcoords[t.x], texcoords[t.y], texcoords[t.z]);
      for (auto vid : {t.x, t.y, t.z}) tangu[vid] += normalize(tutv.first);
      for (auto vid : {t.x, t.y, t.z}) tangv[vid] += normalize(tutv.second);
    }
    for (auto& t : tangu) t = normalize(t);
    for (auto& t : tangv) t = normalize(t);

    auto tangent_spaces = vector<vec4f>(positions.size());
    for (auto& tangent : tangent_spaces) tangent = zero4f;
    for (auto i = 0; i < positions.size(); i++) {
      tangu[i] = orthonormalize(tangu[i], normals[i]);
      auto s = (dot(cross(normals[i], tangu[i]), tangv[i]) < 0) ? -1.0f : 1.0f;
      tangent_spaces[i] = {tangu[i].x, tangu[i].y, tangu[i].z, s};
    }
    return tangent_spaces;
  }

  // Apply skinning
  pair<vector<vec3f>, vector<vec3f>> skin_vertices(
      const vector<vec3f>& positions, const vector<vec3f>& normals,
      const vector<vec4f>& weights, const vector<vec4i>& joints,
      const vector<frame3f>& xforms) {
    auto skinned_positions = vector<vec3f>{positions.size()};
    auto skinned_normals   = vector<vec3f>{positions.size()};
    for (auto i = 0; i < positions.size(); i++) {
      skinned_positions[i] =
          transform_point(xforms[joints[i].x], positions[i]) * weights[i].x +
          transform_point(xforms[joints[i].y], positions[i]) * weights[i].y +
          transform_point(xforms[joints[i].z], positions[i]) * weights[i].z +
          transform_point(xforms[joints[i].w], positions[i]) * weights[i].w;
    }
    for (auto i = 0; i < normals.size(); i++) {
      skinned_normals[i] = normalize(
          transform_direction(xforms[joints[i].x], normals[i]) * weights[i].x +
          transform_direction(xforms[joints[i].y], normals[i]) * weights[i].y +
          transform_direction(xforms[joints[i].z], normals[i]) * weights[i].z +
          transform_direction(xforms[joints[i].w], normals[i]) * weights[i].w);
    }
    return {skinned_positions, skinned_normals};
  }

  // Apply skinning as specified in Khronos glTF
  pair<vector<vec3f>, vector<vec3f>> skin_matrices(
      const vector<vec3f>& positions, const vector<vec3f>& normals,
      const vector<vec4f>& weights, const vector<vec4i>& joints,
      const vector<mat4f>& xforms) {
    auto skinned_positions = vector<vec3f>{positions.size()};
    auto skinned_normals   = vector<vec3f>{positions.size()};
    for (auto i = 0; i < positions.size(); i++) {
      auto xform = xforms[joints[i].x] * weights[i].x +
                   xforms[joints[i].y] * weights[i].y +
                   xforms[joints[i].z] * weights[i].z +
                   xforms[joints[i].w] * weights[i].w;
      skinned_positions[i] = transform_point(xform, positions[i]);
      skinned_normals[i]   = normalize(transform_direction(xform, normals[i]));
    }
    return {skinned_positions, skinned_normals};
  }

  // Apply skinning
  void skin_vertices(vector<vec3f>& skinned_positions,
      vector<vec3f>& skinned_normals, const vector<vec3f>& positions,
      const vector<vec3f>& normals, const vector<vec4f>& weights,
      const vector<vec4i>& joints, const vector<frame3f>& xforms) {
    if (skinned_positions.size() != positions.size() ||
        skinned_normals.size() != normals.size()) {
      throw std::out_of_range("arrays should be the same size");
    }
    for (auto i = 0; i < positions.size(); i++) {
      skinned_positions[i] =
          transform_point(xforms[joints[i].x], positions[i]) * weights[i].x +
          transform_point(xforms[joints[i].y], positions[i]) * weights[i].y +
          transform_point(xforms[joints[i].z], positions[i]) * weights[i].z +
          transform_point(xforms[joints[i].w], positions[i]) * weights[i].w;
    }
    for (auto i = 0; i < normals.size(); i++) {
      skinned_normals[i] = normalize(
          transform_direction(xforms[joints[i].x], normals[i]) * weights[i].x +
          transform_direction(xforms[joints[i].y], normals[i]) * weights[i].y +
          transform_direction(xforms[joints[i].z], normals[i]) * weights[i].z +
          transform_direction(xforms[joints[i].w], normals[i]) * weights[i].w);
    }
  }

  // Apply skinning as specified in Khronos glTF
  void skin_matrices(vector<vec3f>& skinned_positions,
      vector<vec3f>& skinned_normals, const vector<vec3f>& positions,
      const vector<vec3f>& normals, const vector<vec4f>& weights,
      const vector<vec4i>& joints, const vector<mat4f>& xforms) {
    if (skinned_positions.size() != positions.size() ||
        skinned_normals.size() != normals.size()) {
      throw std::out_of_range("arrays should be the same size");
    }
    for (auto i = 0; i < positions.size(); i++) {
      auto xform = xforms[joints[i].x] * weights[i].x +
                   xforms[joints[i].y] * weights[i].y +
                   xforms[joints[i].z] * weights[i].z +
                   xforms[joints[i].w] * weights[i].w;
      skinned_positions[i] = transform_point(xform, positions[i]);
      skinned_normals[i]   = normalize(transform_direction(xform, normals[i]));
    }
  }

}  // namespace yocto

// -----------------------------------------------------------------------------
// COMPUTATION OF PER VERTEX PROPETIES
// -----------------------------------------------------------------------------
namespace yocto {

  // Flip vertex normals
  vector<vec3f> flip_normals(const vector<vec3f>& normals) {
    auto flipped = normals;
    for (auto& n : flipped) n = -n;
    return flipped;
  }
  // Flip face orientation
  vector<vec3i> flip_triangles(const vector<vec3i>& triangles) {
    auto flipped = triangles;
    for (auto& t : flipped) swap(t.y, t.z);
    return flipped;
  }
  vector<vec4i> flip_quads(const vector<vec4i>& quads) {
    auto flipped = quads;
    for (auto& q : flipped) {
      if (q.z != q.w) {
        swap(q.y, q.w);
      } else {
        swap(q.y, q.z);
        q.w = q.z;
      }
    }
    return flipped;
  }

  // Align vertex positions. Alignment is 0: none, 1: min, 2: max, 3: center.
  vector<vec3f> align_vertices(
      const vector<vec3f>& positions, const vec3i& alignment) {
    auto bounds = invalidb3f;
    for (auto& p : positions) bounds = merge(bounds, p);
    auto offset = vec3f{0, 0, 0};
    switch (alignment.x) {
      case 1: offset.x = bounds.min.x; break;
      case 2: offset.x = (bounds.min.x + bounds.max.x) / 2; break;
      case 3: offset.x = bounds.max.x; break;
    }
    switch (alignment.y) {
      case 1: offset.y = bounds.min.y; break;
      case 2: offset.y = (bounds.min.y + bounds.max.y) / 2; break;
      case 3: offset.y = bounds.max.y; break;
    }
    switch (alignment.z) {
      case 1: offset.z = bounds.min.z; break;
      case 2: offset.z = (bounds.min.z + bounds.max.z) / 2; break;
      case 3: offset.z = bounds.max.z; break;
    }
    auto aligned = positions;
    for (auto& p : aligned) p -= offset;
    return aligned;
  }

}  // namespace yocto

// -----------------------------------------------------------------------------
// EDGEA AND ADJACENCIES
// -----------------------------------------------------------------------------
namespace yocto {

  // Initialize an edge map with elements.
  edge_map make_edge_map(const vector<vec3i>& triangles) {
    auto emap = edge_map{};
    for (auto& t : triangles) {
      insert_edge(emap, {t.x, t.y});
      insert_edge(emap, {t.y, t.z});
      insert_edge(emap, {t.z, t.x});
    }
    return emap;
  }
  edge_map make_edge_map(const vector<vec4i>& quads) {
    auto emap = edge_map{};
    for (auto& q : quads) {
      insert_edge(emap, {q.x, q.y});
      insert_edge(emap, {q.y, q.z});
      if (q.z != q.w) insert_edge(emap, {q.z, q.w});
      insert_edge(emap, {q.w, q.x});
    }
    return emap;
  }
  void insert_edges(edge_map& emap, const vector<vec3i>& triangles) {
    for (auto& t : triangles) {
      insert_edge(emap, {t.x, t.y});
      insert_edge(emap, {t.y, t.z});
      insert_edge(emap, {t.z, t.x});
    }
  }
  void insert_edges(edge_map& emap, const vector<vec4i>& quads) {
    for (auto& q : quads) {
      insert_edge(emap, {q.x, q.y});
      insert_edge(emap, {q.y, q.z});
      if (q.z != q.w) insert_edge(emap, {q.z, q.w});
      insert_edge(emap, {q.w, q.x});
    }
  }
  // Insert an edge and return its index
  int insert_edge(edge_map& emap, const vec2i& edge) {
    auto es = edge.x < edge.y ? edge : vec2i{edge.y, edge.x};
    auto it = emap.edges.find(es);
    if (it == emap.edges.end()) {
      auto data = edge_map::edge_data{(int)emap.edges.size(), 1};
      emap.edges.insert(it, {es, data});
      return data.index;
    } else {
      auto& data = it->second;
      data.nfaces += 1;
      return data.index;
    }
  }
  // Get number of edges
  int num_edges(const edge_map& emap) { return (int)emap.edges.size(); }
  // Get the edge index
  int edge_index(const edge_map& emap, const vec2i& edge) {
    auto es       = edge.x < edge.y ? edge : vec2i{edge.y, edge.x};
    auto iterator = emap.edges.find(es);
    if (iterator == emap.edges.end()) return -1;
    return iterator->second.index;
  }
  // Get a list of edges, boundary edges, boundary vertices
  vector<vec2i> get_edges(const edge_map& emap) {
    auto edges = vector<vec2i>(emap.edges.size());
    for (auto& [edge, data] : emap.edges) edges[data.index] = edge;
    return edges;
  }
  vector<vec2i> get_boundary(const edge_map& emap) {
    auto boundary = vector<vec2i>{};
    for (auto& [edge, data] : emap.edges) {
      if (data.nfaces < 2) boundary.push_back(edge);
    }
    return boundary;
  }
  vector<vec2i> get_edges(const vector<vec3i>& triangles) {
    return get_edges(make_edge_map(triangles));
  }
  vector<vec2i> get_edges(const vector<vec4i>& quads) {
    return get_edges(make_edge_map(quads));
  }
  vector<vec2i> get_edges(
      const vector<vec3i>& triangles, const vector<vec4i>& quads) {
    auto edges      = get_edges(triangles);
    auto more_edges = get_edges(quads);
    edges.insert(edges.end(), more_edges.begin(), more_edges.end());
    return edges;
  }

  // Build adjacencies between faces (sorted counter-clockwise)
  vector<vec3i> face_adjacencies(const vector<vec3i>& triangles) {
    auto get_edge = [](const vec3i& triangle, int i) -> vec2i {
      auto x = triangle[i], y = triangle[i < 2 ? i + 1 : 0];
      return x < y ? vec2i{x, y} : vec2i{y, x};
    };
    auto adjacencies = vector<vec3i>{triangles.size(), vec3i{-1, -1, -1}};
    auto edge_map    = unordered_map<vec2i, int>();
    edge_map.reserve((size_t)(triangles.size() * 1.5));
    for (auto i = 0; i < (int)triangles.size(); ++i) {
      for (auto k = 0; k < 3; ++k) {
        auto edge = get_edge(triangles[i], k);
        auto it   = edge_map.find(edge);
        if (it == edge_map.end()) {
          edge_map.insert(it, {edge, i});
        } else {
          auto neighbor     = it->second;
          adjacencies[i][k] = neighbor;
          for (auto kk = 0; kk < 3; ++kk) {
            auto edge2 = get_edge(triangles[neighbor], kk);
            if (edge2 == edge) {
              adjacencies[neighbor][kk] = i;
              break;
            }
          }
        }
      }
    }
    return adjacencies;
  }

  // Build adjacencies between vertices (sorted counter-clockwise)
  vector<vector<int>> vertex_adjacencies(
      const vector<vec3i>& triangles, const vector<vec3i>& adjacencies) {
    auto find_index = [](const vec3i& v, int x) {
      if (v.x == x) return 0;
      if (v.y == x) return 1;
      if (v.z == x) return 2;
      return -1;
    };

    // For each vertex, find any adjacent face.
    auto num_vertices     = 0;
    auto face_from_vertex = vector<int>(triangles.size() * 3, -1);

    for (auto i = 0; i < (int)triangles.size(); ++i) {
      for (auto k = 0; k < 3; k++) {
        face_from_vertex[triangles[i][k]] = i;
        num_vertices                      = max(num_vertices, triangles[i][k]);
      }
    }

    // Init result.
    auto result = vector<vector<int>>(num_vertices);

    // For each vertex, loop around it and build its adjacency.
    for (auto i = 0; i < num_vertices; ++i) {
      result[i].reserve(6);
      auto first_face = face_from_vertex[i];
      if (first_face == -1) continue;

      auto face = first_face;
      while (true) {
        auto k = find_index(triangles[face], i);
        k      = k != 0 ? k - 1 : 2;
        result[i].push_back(triangles[face][k]);
        face = adjacencies[face][k];
        if (face == -1) break;
        if (face == first_face) break;
      }
    }

    return result;
  }

  // Build adjacencies between each vertex and its adjacent faces.
  // Adjacencies are sorted counter-clockwise and have same starting points as
  // vertex_adjacencies()
  vector<vector<int>> vertex_to_faces_adjacencies(
      const vector<vec3i>& triangles, const vector<vec3i>& adjacencies) {
    auto find_index = [](const vec3i& v, int x) {
      if (v.x == x) return 0;
      if (v.y == x) return 1;
      if (v.z == x) return 2;
      return -1;
    };

    // For each vertex, find any adjacent face.
    auto num_vertices     = 0;
    auto face_from_vertex = vector<int>(triangles.size() * 3, -1);

    for (auto i = 0; i < (int)triangles.size(); ++i) {
      for (auto k = 0; k < 3; k++) {
        face_from_vertex[triangles[i][k]] = i;
        num_vertices                      = max(num_vertices, triangles[i][k]);
      }
    }

    // Init result.
    auto result = vector<vector<int>>(num_vertices);

    // For each vertex, loop around it and build its adjacency.
    for (auto i = 0; i < num_vertices; ++i) {
      result[i].reserve(6);
      auto first_face = face_from_vertex[i];
      if (first_face == -1) continue;

      auto face = first_face;
      while (true) {
        auto k = find_index(triangles[face], i);
        k      = k != 0 ? k - 1 : 2;
        face   = adjacencies[face][k];
        result[i].push_back(face);
        if (face == -1) break;
        if (face == first_face) break;
      }
    }

    return result;
  }

  // Compute boundaries as a list of loops (sorted counter-clockwise)
  vector<vector<int>> ordered_boundaries(const vector<vec3i>& triangles,
      const vector<vec3i>& adjacency, int num_vertices) {
    // map every boundary vertex to its next one
    auto next_vert = vector<int>(num_vertices, -1);
    for (auto i = 0; i < (int)triangles.size(); ++i) {
      for (auto k = 0; k < 3; ++k) {
        if (adjacency[i][k] == -1)
          next_vert[triangles[i][k]] = triangles[i][(k + 1) % 3];
      }
    }

    // result
    auto boundaries = vector<vector<int>>();

    // arrange boundary vertices in loops
    for (auto i = 0; i < (int)next_vert.size(); i++) {
      if (next_vert[i] == -1) continue;

      // add new empty boundary
      boundaries.emplace_back();
      auto current = i;

      while (true) {
        auto next = next_vert[current];
        if (next == -1) {
          return {};
        }
        next_vert[current] = -1;
        boundaries.back().push_back(current);

        // close loop if necessary
        if (next == i)
          break;
        else
          current = next;
      }
    }

    return boundaries;
  }

}  // namespace yocto

// -----------------------------------------------------------------------------
// HASH GRID AND NEAREST NEIGHBORS
// -----------------------------------------------------------------------------

namespace yocto {

  // Gets the cell index
  vec3i get_cell_index(const hash_grid& grid, const vec3f& position) {
    auto scaledpos = position * grid.cell_inv_size;
    return vec3i{(int)scaledpos.x, (int)scaledpos.y, (int)scaledpos.z};
  }

  // Create a hash_grid
  hash_grid make_hash_grid(float cell_size) {
    auto grid          = hash_grid{};
    grid.cell_size     = cell_size;
    grid.cell_inv_size = 1 / cell_size;
    return grid;
  }
  hash_grid make_hash_grid(const vector<vec3f>& positions, float cell_size) {
    auto grid          = hash_grid{};
    grid.cell_size     = cell_size;
    grid.cell_inv_size = 1 / cell_size;
    for (auto& position : positions) insert_vertex(grid, position);
    return grid;
  }
  // Inserts a point into the grid
  int insert_vertex(hash_grid& grid, const vec3f& position) {
    auto vertex_id = (int)grid.positions.size();
    auto cell      = get_cell_index(grid, position);
    grid.cells[cell].push_back(vertex_id);
    grid.positions.push_back(position);
    return vertex_id;
  }
  // Finds the nearest neighbors within a given radius
  void find_neighbors(const hash_grid& grid, vector<int>& neighbors,
      const vec3f& position, float max_radius, int skip_id) {
    auto cell        = get_cell_index(grid, position);
    auto cell_radius = (int)(max_radius * grid.cell_inv_size) + 1;
    neighbors.clear();
    auto max_radius_squared = max_radius * max_radius;
    for (auto k = -cell_radius; k <= cell_radius; k++) {
      for (auto j = -cell_radius; j <= cell_radius; j++) {
        for (auto i = -cell_radius; i <= cell_radius; i++) {
          auto ncell         = cell + vec3i{i, j, k};
          auto cell_iterator = grid.cells.find(ncell);
          if (cell_iterator == grid.cells.end()) continue;
          auto& ncell_vertices = cell_iterator->second;
          for (auto vertex_id : ncell_vertices) {
            if (distance_squared(grid.positions[vertex_id], position) >
                max_radius_squared)
              continue;
            if (vertex_id == skip_id) continue;
            neighbors.push_back(vertex_id);
          }
        }
      }
    }
  }
  void find_neighbors(const hash_grid& grid, vector<int>& neighbors,
      const vec3f& position, float max_radius) {
    find_neighbors(grid, neighbors, position, max_radius, -1);
  }
  void find_neighbors(const hash_grid& grid, vector<int>& neighbors, int vertex,
      float max_radius) {
    find_neighbors(grid, neighbors, grid.positions[vertex], max_radius, vertex);
  }

}  // namespace yocto

// -----------------------------------------------------------------------------
// IMPLEMENTATION OF SHAPE ELEMENT CONVERSION AND GROUPING
// -----------------------------------------------------------------------------
namespace yocto {

  // Convert quads to triangles
  vector<vec3i> quads_to_triangles(const vector<vec4i>& quads) {
    auto triangles = vector<vec3i>{};
    triangles.reserve(quads.size() * 2);
    for (auto& q : quads) {
      triangles.push_back({q.x, q.y, q.w});
      if (q.z != q.w) triangles.push_back({q.z, q.w, q.y});
    }
    return triangles;
  }

  // Convert triangles to quads by creating degenerate quads
  vector<vec4i> triangles_to_quads(const vector<vec3i>& triangles) {
    auto quads = vector<vec4i>{};
    quads.reserve(triangles.size());
    for (auto& t : triangles) quads.push_back({t.x, t.y, t.z, t.z});
    return quads;
  }

  // Convert beziers to lines using 3 lines for each bezier.
  vector<vec2i> bezier_to_lines(const vector<vec4i>& beziers) {
    auto lines = vector<vec2i>{};
    lines.reserve(beziers.size() * 3);
    for (auto b : beziers) {
      lines.push_back({b.x, b.y});
      lines.push_back({b.y, b.z});
      lines.push_back({b.z, b.w});
    }
    return lines;
  }

  // Weld vertices within a threshold.
  pair<vector<vec3f>, vector<int>> weld_vertices(
      const vector<vec3f>& positions, float threshold) {
    auto indices   = vector<int>(positions.size());
    auto welded    = vector<vec3f>{};
    auto grid      = make_hash_grid(threshold);
    auto neighbors = vector<int>{};
    for (auto vertex = 0; vertex < positions.size(); vertex++) {
      auto& position = positions[vertex];
      find_neighbors(grid, neighbors, position, threshold);
      if (neighbors.empty()) {
        welded.push_back(position);
        indices[vertex] = (int)welded.size() - 1;
        insert_vertex(grid, position);
      } else {
        indices[vertex] = neighbors.front();
      }
    }
    return {welded, indices};
  }
  pair<vector<vec3i>, vector<vec3f>> weld_triangles(
      const vector<vec3i>& triangles, const vector<vec3f>& positions,
      float threshold) {
    auto [wpositions, indices] = weld_vertices(positions, threshold);
    auto wtriangles            = triangles;
    for (auto& t : wtriangles) t = {indices[t.x], indices[t.y], indices[t.z]};
    return {wtriangles, wpositions};
  }
  pair<vector<vec4i>, vector<vec3f>> weld_quads(const vector<vec4i>& quads,
      const vector<vec3f>& positions, float threshold) {
    auto [wpositions, indices] = weld_vertices(positions, threshold);
    auto wquads                = quads;
    for (auto& q : wquads)
      q = {
          indices[q.x],
          indices[q.y],
          indices[q.z],
          indices[q.w],
      };
    return {wquads, wpositions};
  }

  // Merge shape elements
  void merge_lines(vector<vec2i>& lines, vector<vec3f>& positions,
      vector<vec3f>& tangents, vector<vec2f>& texcoords, vector<float>& radius,
      const vector<vec2i>& merge_lines, const vector<vec3f>& merge_positions,
      const vector<vec3f>& merge_tangents,
      const vector<vec2f>& merge_texturecoords,
      const vector<float>& merge_radius) {
    auto merge_verts = (int)positions.size();
    for (auto& l : merge_lines)
      lines.push_back({l.x + merge_verts, l.y + merge_verts});
    positions.insert(
        positions.end(), merge_positions.begin(), merge_positions.end());
    tangents.insert(
        tangents.end(), merge_tangents.begin(), merge_tangents.end());
    texcoords.insert(texcoords.end(), merge_texturecoords.begin(),
        merge_texturecoords.end());
    radius.insert(radius.end(), merge_radius.begin(), merge_radius.end());
  }
  void merge_triangles(vector<vec3i>& triangles, vector<vec3f>& positions,
      vector<vec3f>& normals, vector<vec2f>& texcoords,
      const vector<vec3i>& merge_triangles,
      const vector<vec3f>& merge_positions, const vector<vec3f>& merge_normals,
      const vector<vec2f>& merge_texturecoords) {
    auto merge_verts = (int)positions.size();
    for (auto& t : merge_triangles)
      triangles.push_back(
          {t.x + merge_verts, t.y + merge_verts, t.z + merge_verts});
    positions.insert(
        positions.end(), merge_positions.begin(), merge_positions.end());
    normals.insert(normals.end(), merge_normals.begin(), merge_normals.end());
    texcoords.insert(texcoords.end(), merge_texturecoords.begin(),
        merge_texturecoords.end());
  }
  void merge_quads(vector<vec4i>& quads, vector<vec3f>& positions,
      vector<vec3f>& normals, vector<vec2f>& texcoords,
      const vector<vec4i>& merge_quads, const vector<vec3f>& merge_positions,
      const vector<vec3f>& merge_normals,
      const vector<vec2f>& merge_texturecoords) {
    auto merge_verts = (int)positions.size();
    for (auto& q : merge_quads)
      quads.push_back({q.x + merge_verts, q.y + merge_verts, q.z + merge_verts,
          q.w + merge_verts});
    positions.insert(
        positions.end(), merge_positions.begin(), merge_positions.end());
    normals.insert(normals.end(), merge_normals.begin(), merge_normals.end());
    texcoords.insert(texcoords.end(), merge_texturecoords.begin(),
        merge_texturecoords.end());
  }

}  // namespace yocto

// -----------------------------------------------------------------------------
// IMPLEMENTATION OF SHAPE SAMPLING
// -----------------------------------------------------------------------------
namespace yocto {

  // Pick a point in a point set uniformly.
  int sample_points(int npoints, float re) {
    return sample_uniform(npoints, re);
  }
  int sample_points(const vector<float>& cdf, float re) {
    return sample_discrete(cdf, re);
  }
  vector<float> sample_points_cdf(int npoints) {
    auto cdf = vector<float>(npoints);
    for (auto i = 0; i < cdf.size(); i++)
      cdf[i] = 1 + (i != 0 ? cdf[i - 1] : 0);
    return cdf;
  }
  void sample_points_cdf(vector<float>& cdf, int npoints) {
    for (auto i = 0; i < cdf.size(); i++)
      cdf[i] = 1 + (i != 0 ? cdf[i - 1] : 0);
  }

  // Pick a point on lines uniformly.
  pair<int, float> sample_lines(const vector<float>& cdf, float re, float ru) {
    return {sample_discrete(cdf, re), ru};
  }
  vector<float> sample_lines_cdf(
      const vector<vec2i>& lines, const vector<vec3f>& positions) {
    auto cdf = vector<float>(lines.size());
    for (auto i = 0; i < cdf.size(); i++) {
      auto& l = lines[i];
      auto  w = line_length(positions[l.x], positions[l.y]);
      cdf[i]  = w + (i != 0 ? cdf[i - 1] : 0);
    }
    return cdf;
  }
  void sample_lines_cdf(vector<float>& cdf, const vector<vec2i>& lines,
      const vector<vec3f>& positions) {
    for (auto i = 0; i < cdf.size(); i++) {
      auto& l = lines[i];
      auto  w = line_length(positions[l.x], positions[l.y]);
      cdf[i]  = w + (i != 0 ? cdf[i - 1] : 0);
    }
  }

  // Pick a point on a triangle mesh uniformly.
  pair<int, vec2f> sample_triangles(
      const vector<float>& cdf, float re, const vec2f& ruv) {
    return {sample_discrete(cdf, re), sample_triangle(ruv)};
  }
  vector<float> sample_triangles_cdf(
      const vector<vec3i>& triangles, const vector<vec3f>& positions) {
    auto cdf = vector<float>(triangles.size());
    for (auto i = 0; i < cdf.size(); i++) {
      auto& t = triangles[i];
      auto  w = triangle_area(positions[t.x], positions[t.y], positions[t.z]);
      cdf[i]  = w + (i != 0 ? cdf[i - 1] : 0);
    }
    return cdf;
  }
  void sample_triangles_cdf(vector<float>& cdf, const vector<vec3i>& triangles,
      const vector<vec3f>& positions) {
    for (auto i = 0; i < cdf.size(); i++) {
      auto& t = triangles[i];
      auto  w = triangle_area(positions[t.x], positions[t.y], positions[t.z]);
      cdf[i]  = w + (i != 0 ? cdf[i - 1] : 0);
    }
  }

  // Pick a point on a quad mesh uniformly.
  pair<int, vec2f> sample_quads(
      const vector<float>& cdf, float re, const vec2f& ruv) {
    return {sample_discrete(cdf, re), ruv};
  }
  pair<int, vec2f> sample_quads(const vector<vec4i>& quads,
      const vector<float>& cdf, float re, const vec2f& ruv) {
    auto element = sample_discrete(cdf, re);
    if (quads[element].z == quads[element].w) {
      return {element, sample_triangle(ruv)};
    } else {
      return {element, ruv};
    }
  }
  vector<float> sample_quads_cdf(
      const vector<vec4i>& quads, const vector<vec3f>& positions) {
    auto cdf = vector<float>(quads.size());
    for (auto i = 0; i < cdf.size(); i++) {
      auto& q = quads[i];
      auto  w = quad_area(
           positions[q.x], positions[q.y], positions[q.z], positions[q.w]);
      cdf[i] = w + (i ? cdf[i - 1] : 0);
    }
    return cdf;
  }
  void sample_quads_cdf(vector<float>& cdf, const vector<vec4i>& quads,
      const vector<vec3f>& positions) {
    for (auto i = 0; i < cdf.size(); i++) {
      auto& q = quads[i];
      auto  w = quad_area(
           positions[q.x], positions[q.y], positions[q.z], positions[q.w]);
      cdf[i] = w + (i ? cdf[i - 1] : 0);
    }
  }

  // Samples a set of points over a triangle mesh uniformly. The rng function
  // takes the point index and returns vec3f numbers uniform directibuted in
  // [0,1]^3. unorm and texcoord are optional.
  void sample_triangles(vector<vec3f>& sampled_positions,
      vector<vec3f>& sampled_normals, vector<vec2f>& sampled_texcoords,
      const vector<vec3i>& triangles, const vector<vec3f>& positions,
      const vector<vec3f>& normals, const vector<vec2f>& texcoords, int npoints,
      int seed) {
    sampled_positions.resize(npoints);
    sampled_normals.resize(npoints);
    sampled_texcoords.resize(npoints);
    auto cdf = sample_triangles_cdf(triangles, positions);
    auto rng = make_rng(seed);
    for (auto i = 0; i < npoints; i++) {
      auto  sample         = sample_triangles(cdf, rand1f(rng), rand2f(rng));
      auto& t              = triangles[sample.first];
      auto  uv             = sample.second;
      sampled_positions[i] = interpolate_triangle(
          positions[t.x], positions[t.y], positions[t.z], uv);
      if (!sampled_normals.empty()) {
        sampled_normals[i] = normalize(
            interpolate_triangle(normals[t.x], normals[t.y], normals[t.z], uv));
      } else {
        sampled_normals[i] = triangle_normal(
            positions[t.x], positions[t.y], positions[t.z]);
      }
      if (!sampled_texcoords.empty()) {
        sampled_texcoords[i] = interpolate_triangle(
            texcoords[t.x], texcoords[t.y], texcoords[t.z], uv);
      } else {
        sampled_texcoords[i] = zero2f;
      }
    }
  }

  // Samples a set of points over a triangle mesh uniformly. The rng function
  // takes the point index and returns vec3f numbers uniform directibuted in
  // [0,1]^3. unorm and texcoord are optional.
  void sample_quads(vector<vec3f>& sampled_positions,
      vector<vec3f>& sampled_normals, vector<vec2f>& sampled_texcoords,
      const vector<vec4i>& quads, const vector<vec3f>& positions,
      const vector<vec3f>& normals, const vector<vec2f>& texcoords, int npoints,
      int seed) {
    sampled_positions.resize(npoints);
    sampled_normals.resize(npoints);
    sampled_texcoords.resize(npoints);
    auto cdf = sample_quads_cdf(quads, positions);
    auto rng = make_rng(seed);
    for (auto i = 0; i < npoints; i++) {
      auto  sample         = sample_quads(cdf, rand1f(rng), rand2f(rng));
      auto& q              = quads[sample.first];
      auto  uv             = sample.second;
      sampled_positions[i] = interpolate_quad(
          positions[q.x], positions[q.y], positions[q.z], positions[q.w], uv);
      if (!sampled_normals.empty()) {
        sampled_normals[i] = normalize(interpolate_quad(
            normals[q.x], normals[q.y], normals[q.z], normals[q.w], uv));
      } else {
        sampled_normals[i] = quad_normal(
            positions[q.x], positions[q.y], positions[q.z], positions[q.w]);
      }
      if (!sampled_texcoords.empty()) {
        sampled_texcoords[i] = interpolate_quad(
            texcoords[q.x], texcoords[q.y], texcoords[q.z], texcoords[q.w], uv);
      } else {
        sampled_texcoords[i] = zero2f;
      }
    }
  }

}  // namespace yocto
