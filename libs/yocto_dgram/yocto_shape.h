//
// # Yocto/Shape: Shape utilities
//
// Yocto/Shape is a collection of utilities for manipulating shapes in 3D
// graphics, with a focus on triangle and quad meshes.
// Yocto/Shape is implemented in `yocto_shape.h` and `yocto_shape.cpp`.
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
//

#ifndef _YOCTO_SHAPE_H_
#define _YOCTO_SHAPE_H_

// -----------------------------------------------------------------------------
// INCLUDES
// -----------------------------------------------------------------------------

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "yocto_geometry.h"
#include "yocto_math.h"

// -----------------------------------------------------------------------------
// USING DIRECTIVES
// -----------------------------------------------------------------------------
namespace yocto {

  // using directives
  using std::array;
  using std::pair;
  using std::string;
  using std::unordered_map;
  using std::vector;

}  // namespace yocto

// -----------------------------------------------------------------------------
// SHAPE DATA AND UTILITIES
// -----------------------------------------------------------------------------
namespace yocto {

  // Shape data represented as indexed meshes of elements.
  // May contain either points, lines, triangles and quads.
  struct shape_data {
    // element data
    vector<int>   points    = {};
    vector<vec2i> lines     = {};
    vector<vec3i> triangles = {};
    vector<vec4i> quads     = {};

    vector<vec2i> borders = {};

    // vertex data
    vector<vec3f>    positions = {};
    vector<vec3f>    normals   = {};
    vector<vec2f>    texcoords = {};
    vector<vec4f>    colors    = {};
    vector<float>    radius    = {};
    vector<vec4f>    tangents  = {};
    vector<line_end> ends      = {};

    bool cull     = false;
    bool boundary = false;

    vector<vec3f> cclips = {};
  };

  // Interpolate vertex data
  vec3f eval_position(const shape_data& shape, int element, const vec2f& uv);
  vec3f eval_normal(const shape_data& shape, int element, const vec2f& uv);
  vec3f eval_tangent(const shape_data& shape, int element, const vec2f& uv);
  vec2f eval_texcoord(const shape_data& shape, int element, const vec2f& uv);
  vec4f eval_color(const shape_data& shape, int element, const vec2f& uv);
  float eval_radius(const shape_data& shape, int element, const vec2f& uv);

  // Evaluate element normals
  vec3f eval_element_normal(const shape_data& shape, int element);

  // Compute per-vertex normals/tangents for lines/triangles/quads.
  vector<vec3f> compute_normals(const shape_data& shape);
  void compute_normals(vector<vec3f>& normals, const shape_data& shape);

  // An unevaluated location on a shape
  struct shape_point {
    int   element = 0;
    vec2f uv      = {0, 0};
  };

  // Shape sampling
  vector<float> sample_shape_cdf(const shape_data& shape);
  void          sample_shape_cdf(vector<float>& cdf, const shape_data& shape);
  shape_point   sample_shape(const shape_data& shape, const vector<float>& cdf,
        float rn, const vec2f& ruv);
  vector<shape_point> sample_shape(
      const shape_data& shape, int num_samples, uint64_t seed = 98729387);

  // Conversions
  shape_data quads_to_triangles(const shape_data& shape);
  void       quads_to_triangles_inplace(shape_data& shape);

  // Shape statistics
  vector<string> shape_stats(const shape_data& shape, bool verbose = false);

}  // namespace yocto

// -----------------------------------------------------------------------------
// COMPUTATION OF PER_VERTEX PROPERTIES
// -----------------------------------------------------------------------------
namespace yocto {

  // Compute per-vertex normals/tangents for lines/triangles/quads.
  vector<vec3f> lines_tangents(
      const vector<vec2i>& lines, const vector<vec3f>& positions);
  vector<vec3f> triangles_normals(
      const vector<vec3i>& triangles, const vector<vec3f>& positions);
  vector<vec3f> quads_normals(
      const vector<vec4i>& quads, const vector<vec3f>& positions);
  // Update normals and tangents
  void lines_tangents(vector<vec3f>& tangents, const vector<vec2i>& lines,
      const vector<vec3f>& positions);
  void triangles_normals(vector<vec3f>& normals, const vector<vec3i>& triangles,
      const vector<vec3f>& positions);
  void quads_normals(vector<vec3f>& normals, const vector<vec4i>& quads,
      const vector<vec3f>& positions);

  // Compute per-vertex tangent space for triangle meshes.
  // Tangent space is defined by a four component vector.
  // The first three components are the tangent with respect to the u texcoord.
  // The fourth component is the sign of the tangent wrt the v texcoord.
  // Tangent frame is useful in normal mapping.
  vector<vec4f> triangle_tangent_spaces(const vector<vec3i>& triangles,
      const vector<vec3f>& positions, const vector<vec3f>& normals,
      const vector<vec2f>& texcoords);

  // Apply skinning to vertex position and normals.
  pair<vector<vec3f>, vector<vec3f>> skin_vertices(
      const vector<vec3f>& positions, const vector<vec3f>& normals,
      const vector<vec4f>& weights, const vector<vec4i>& joints,
      const vector<frame3f>& xforms);
  // Apply skinning as specified in Khronos glTF.
  pair<vector<vec3f>, vector<vec3f>> skin_matrices(
      const vector<vec3f>& positions, const vector<vec3f>& normals,
      const vector<vec4f>& weights, const vector<vec4i>& joints,
      const vector<mat4f>& xforms);
  // Update skinning
  void skin_vertices(vector<vec3f>& skinned_positions,
      vector<vec3f>& skinned_normals, const vector<vec3f>& positions,
      const vector<vec3f>& normals, const vector<vec4f>& weights,
      const vector<vec4i>& joints, const vector<frame3f>& xforms);
  void skin_matrices(vector<vec3f>& skinned_positions,
      vector<vec3f>& skinned_normals, const vector<vec3f>& positions,
      const vector<vec3f>& normals, const vector<vec4f>& weights,
      const vector<vec4i>& joints, const vector<mat4f>& xforms);

}  // namespace yocto

// -----------------------------------------------------------------------------
// COMPUTATION OF VERTEX PROPERTIES
// -----------------------------------------------------------------------------
namespace yocto {

  // Flip vertex normals
  vector<vec3f> flip_normals(const vector<vec3f>& normals);
  // Flip face orientation
  vector<vec3i> flip_triangles(const vector<vec3i>& triangles);
  vector<vec4i> flip_quads(const vector<vec4i>& quads);
  // Align vertex positions. Alignment is 0: none, 1: min, 2: max, 3: center.
  vector<vec3f> align_vertices(
      const vector<vec3f>& positions, const vec3i& alignment);

}  // namespace yocto

// -----------------------------------------------------------------------------
// VECTOR HASHING
// -----------------------------------------------------------------------------
namespace std {

  // Hash functor for vector for use with hash_map
  template <>
  struct hash<yocto::vec2i> {
    size_t operator()(const yocto::vec2i& v) const {
      static const auto hasher = std::hash<int>();
      auto              h      = (size_t)0;
      h ^= hasher(v.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
      h ^= hasher(v.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
      return h;
    }
  };
  template <>
  struct hash<yocto::vec3i> {
    size_t operator()(const yocto::vec3i& v) const {
      static const auto hasher = std::hash<int>();
      auto              h      = (size_t)0;
      h ^= hasher(v.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
      h ^= hasher(v.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
      h ^= hasher(v.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
      return h;
    }
  };
  template <>
  struct hash<yocto::vec4i> {
    size_t operator()(const yocto::vec4i& v) const {
      static const auto hasher = std::hash<int>();
      auto              h      = (size_t)0;
      h ^= hasher(v.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
      h ^= hasher(v.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
      h ^= hasher(v.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
      h ^= hasher(v.w) + 0x9e3779b9 + (h << 6) + (h >> 2);
      return h;
    }
  };

}  // namespace std

// -----------------------------------------------------------------------------
// EDGES AND ADJACENCIES
// -----------------------------------------------------------------------------
namespace yocto {

  // Dictionary to store edge information. `index` is the index to the edge
  // array, `edges` the array of edges and `nfaces` the number of adjacent
  // faces. We store only bidirectional edges to keep the dictionary small. Use
  // the functions below to access this data.
  struct edge_map {
    struct edge_data {
      int index;
      int nfaces;
    };
    unordered_map<vec2i, edge_data> edges = {};
  };

  // Initialize an edge map with elements.
  edge_map make_edge_map(const vector<vec3i>& triangles);
  edge_map make_edge_map(const vector<vec4i>& quads);
  void     insert_edges(edge_map& emap, const vector<vec3i>& triangles);
  void     insert_edges(edge_map& emap, const vector<vec4i>& quads);
  // Insert an edge and return its index
  int insert_edge(edge_map& emap, const vec2i& edge);
  // Get the edge index
  int edge_index(const edge_map& emap, const vec2i& edge);
  // Get edges and boundaries
  int           num_edges(const edge_map& emap);
  vector<vec2i> get_edges(const edge_map& emap);
  vector<vec2i> get_boundary(const edge_map& emap);
  vector<vec2i> get_edges(const vector<vec3i>& triangles);
  vector<vec2i> get_edges(const vector<vec4i>& quads);
  vector<vec2i> get_edges(
      const vector<vec3i>& triangles, const vector<vec4i>& quads);

  // Build adjacencies between faces (sorted counter-clockwise)
  vector<vec3i> face_adjacencies(const vector<vec3i>& triangles);

  // Build adjacencies between vertices (sorted counter-clockwise)
  vector<vector<int>> vertex_adjacencies(
      const vector<vec3i>& triangles, const vector<vec3i>& adjacencies);

  // Compute boundaries as a list of loops (sorted counter-clockwise)
  vector<vector<int>> ordered_boundaries(const vector<vec3i>& triangles,
      const vector<vec3i>& adjacency, int num_vertices);

  // Build adjacencies between each vertex and its adjacent faces.
  // Adjacencies are sorted counter-clockwise and have same starting points as
  // vertex_adjacencies()
  vector<vector<int>> vertex_to_faces_adjacencies(
      const vector<vec3i>& triangles, const vector<vec3i>& adjacencies);

}  // namespace yocto

// -----------------------------------------------------------------------------
// HASH GRID AND NEAREST NEIGHBORS
// -----------------------------------------------------------------------------
namespace yocto {

  // A sparse grid of cells, containing list of points. Cells are stored in
  // a dictionary to get sparsity. Helpful for nearest neighboor lookups.
  struct hash_grid {
    float                             cell_size     = 0;
    float                             cell_inv_size = 0;
    vector<vec3f>                     positions     = {};
    unordered_map<vec3i, vector<int>> cells         = {};
  };

  // Create a hash_grid
  hash_grid make_hash_grid(float cell_size);
  hash_grid make_hash_grid(const vector<vec3f>& positions, float cell_size);
  // Inserts a point into the grid
  int insert_vertex(hash_grid& grid, const vec3f& position);
  // Finds the nearest neighbors within a given radius
  void find_neighbors(const hash_grid& grid, vector<int>& neighbors,
      const vec3f& position, float max_radius);
  void find_neighbors(const hash_grid& grid, vector<int>& neighbors, int vertex,
      float max_radius);

}  // namespace yocto

// -----------------------------------------------------------------------------
// SHAPE ELEMENT CONVERSION AND GROUPING
// -----------------------------------------------------------------------------
namespace yocto {

  // Convert quads to triangles
  vector<vec3i> quads_to_triangles(const vector<vec4i>& quads);
  // Convert triangles to quads by creating degenerate quads
  vector<vec4i> triangles_to_quads(const vector<vec3i>& triangles);
  // Convert beziers to lines using 3 lines for each bezier.
  vector<vec4i> bezier_to_lines(vector<vec2i>& lines);

  // Weld vertices within a threshold.
  pair<vector<vec3f>, vector<int>> weld_vertices(
      const vector<vec3f>& positions, float threshold);
  pair<vector<vec3i>, vector<vec3f>> weld_triangles(
      const vector<vec3i>& triangles, const vector<vec3f>& positions,
      float threshold);
  pair<vector<vec4i>, vector<vec3f>> weld_quads(const vector<vec4i>& quads,
      const vector<vec3f>& positions, float threshold);

  // Merge shape elements
  void merge_lines(vector<vec2i>& lines, vector<vec3f>& positions,
      vector<vec3f>& tangents, vector<vec2f>& texcoords, vector<float>& radius,
      const vector<vec2i>& merge_lines, const vector<vec3f>& merge_positions,
      const vector<vec3f>& merge_tangents,
      const vector<vec2f>& merge_texturecoords,
      const vector<float>& merge_radius);
  void merge_triangles(vector<vec3i>& triangles, vector<vec3f>& positions,
      vector<vec3f>& normals, vector<vec2f>& texcoords,
      const vector<vec2i>& merge_triangles,
      const vector<vec3f>& merge_positions, const vector<vec3f>& merge_normals,
      const vector<vec2f>& merge_texturecoords);
  void merge_quads(vector<vec4i>& quads, vector<vec3f>& positions,
      vector<vec3f>& normals, vector<vec2f>& texcoords,
      const vector<vec4i>& merge_quads, const vector<vec3f>& merge_positions,
      const vector<vec3f>& merge_normals,
      const vector<vec2f>& merge_texturecoords);

}  // namespace yocto

// -----------------------------------------------------------------------------
// SHAPE SAMPLING
// -----------------------------------------------------------------------------
namespace yocto {

  // Pick a point in a point set uniformly.
  int           sample_points(int npoints, float re);
  int           sample_points(const vector<float>& cdf, float re);
  vector<float> sample_points_cdf(int npoints);
  void          sample_points_cdf(vector<float>& cdf, int npoints);

  // Pick a point on lines uniformly.
  pair<int, float> sample_lines(const vector<float>& cdf, float re, float ru);
  vector<float>    sample_lines_cdf(
         const vector<vec2i>& lines, const vector<vec3f>& positions);
  void sample_lines_cdf(vector<float>& cdf, const vector<vec2i>& lines,
      const vector<vec3f>& positions);

  // Pick a point on a triangle mesh uniformly.
  pair<int, vec2f> sample_triangles(
      const vector<float>& cdf, float re, const vec2f& ruv);
  vector<float> sample_triangles_cdf(
      const vector<vec3i>& triangles, const vector<vec3f>& positions);
  void sample_triangles_cdf(vector<float>& cdf, const vector<vec3i>& triangles,
      const vector<vec3f>& positions);

  // Pick a point on a quad mesh uniformly.
  pair<int, vec2f> sample_quads(
      const vector<float>& cdf, float re, const vec2f& ruv);
  vector<float> sample_quads_cdf(
      const vector<vec4i>& quads, const vector<vec3f>& positions);
  void sample_quads_cdf(vector<float>& cdf, const vector<vec4i>& quads,
      const vector<vec3f>& positions);

  // Samples a set of points over a triangle/quad mesh uniformly. Returns pos,
  // norm and texcoord of the sampled points.
  void sample_triangles(vector<vec3f>& sampled_positions,
      vector<vec3f>& sampled_normals, vector<vec2f>& sampled_texcoords,
      const vector<vec3i>& triangles, const vector<vec3f>& positions,
      const vector<vec3f>& normals, const vector<vec2f>& texcoords, int npoints,
      int seed = 7);
  void sample_quads(vector<vec3f>& sampled_positions,
      vector<vec3f>& sampled_normals, vector<vec2f>& sampled_texcoords,
      const vector<vec4i>& quads, const vector<vec3f>& positions,
      const vector<vec3f>& normals, const vector<vec2f>& texcoords, int npoints,
      int seed = 7);

}  // namespace yocto

#endif
