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

#ifndef _YOCTO_DGRAM_BVH_H_
#define _YOCTO_DGRAM_BVH_H_

// -----------------------------------------------------------------------------
// INCLUDES
// -----------------------------------------------------------------------------

#include "yocto_dgram.h"
#include "yocto_dgram_shape.h"

// -----------------------------------------------------------------------------
// USING DIRECTIVES
// -----------------------------------------------------------------------------
namespace yocto {

  // using directives

}  // namespace yocto

// -----------------------------------------------------------------------------
// BVH BUILD
// -----------------------------------------------------------------------------
namespace yocto {

  struct dgram_bvh_node {
    bbox3f  bbox     = invalidb3f;
    int32_t start    = 0;
    int16_t num      = 0;
    int8_t  axis     = 0;
    bool    internal = false;
  };

  struct dgram_shape_bvh {
    vector<dgram_bvh_node> nodes      = {};
    vector<int>            primitives = {};
  };

  struct dgram_scene_bvh {
    vector<dgram_bvh_node>  nodes      = {};
    vector<int>             primitives = {};
    vector<dgram_shape_bvh> shapes     = {};
  };

  dgram_scene_bvh make_bvh(const trace_shapes& shapes, bool highquality = false,
      bool noparallel = false);

}  // namespace yocto

// -----------------------------------------------------------------------------
// BVH INTERSECTION
// -----------------------------------------------------------------------------
namespace yocto {

  struct bvh_intersection {
    int           shape     = -1;
    shape_element element   = {};
    vec2f         uv        = {0, 0};
    float         distance  = 0;
    vec3f         position  = {0, 0, 0};
    vec3f         normal    = {0, 0, 0};
    bool          hit_arrow = false;

    bool operator<(const bvh_intersection& x) const {
      if (shape != x.shape) return shape < x.shape;
      return element < x.element;
    }
  };

  struct bvh_intersections {
    vector<bvh_intersection> intersections = {};
  };

  bvh_intersections intersect_bvh(const dgram_scene_bvh& bvh,
      const trace_shapes& shapes, const ray3f& ray_);
}  // namespace yocto

#endif