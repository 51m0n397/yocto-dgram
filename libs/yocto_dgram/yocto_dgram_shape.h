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

#ifndef _YOCTO_DGRAM_SHAPE_H_
#define _YOCTO_DGRAM_SHAPE_H_

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
// SHAPE BUILD
// -----------------------------------------------------------------------------
namespace yocto {

  enum class primitive_type { point, line, triangle, quad, border };

  struct trace_shape {
    vector<vec3f> positions = {};

    vector<int>   points    = {};
    vector<vec2i> lines     = {};
    vector<vec3i> triangles = {};
    vector<vec4i> quads     = {};
    vector<vec2i> borders   = {};

    vector<vec4f>     fills         = {};
    vector<line_ends> ends          = {};
    vector<float>     radii         = {};
    vector<vec3f>     arrow_dirs    = {};
    vector<float>     arrow_rads0   = {};
    vector<float>     arrow_rads1   = {};
    vector<vec3f>     arrow_points0 = {};
    vector<vec3f>     arrow_points1 = {};

    vector<vec3f> cclip_positions = {};
    vector<vec3i> cclip_indices   = {};

    int material = -1;
  };

  struct trace_shapes {
    vector<trace_shape> shapes = {};
  };

  struct shape_element {
    primitive_type primitive = primitive_type::point;
    int            index     = -1;

    bool operator<(const shape_element& x) const {
      if (primitive != x.primitive) return primitive < x.primitive;
      return index < x.index;
    }
  };

  trace_shapes make_shapes(const dgram_scene& scene, const int& cam,
      const vec2f& size, const float& scale, const bool noparallel = false);

}  // namespace yocto

// -----------------------------------------------------------------------------
// SHAPE PROPERTY EVALUATION
// -----------------------------------------------------------------------------
namespace yocto {

  vec4f eval_material(const trace_shape& shape, const dgram_material& material,
      const shape_element& element, const vec2f& uv);

}  // namespace yocto

#endif