//
// # Yocto/Dgram: Diagram representation
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

#ifndef _YOCTO_DGRAM_H_
#define _YOCTO_DGRAM_H_

// -----------------------------------------------------------------------------
// INCLUDES
// -----------------------------------------------------------------------------

#include <yocto/yocto_geometry.h>
#include <yocto/yocto_image.h>
#include <yocto/yocto_math.h>

// -----------------------------------------------------------------------------
// USING DIRECTIVES
// -----------------------------------------------------------------------------
namespace yocto {

  // using directives

}  // namespace yocto

// -----------------------------------------------------------------------------
// DGRAM SCENES
// -----------------------------------------------------------------------------
namespace yocto {

  enum class line_end { cap, stealth_arrow, triangle_arrow };

  enum class dashed_line { always, never, transparency };
  inline const auto dashed_line_names = vector<string>{
      "always", "never", "transparency"};

  enum class dash_cap_type { round, square };
  inline const auto dash_cap_type_names = vector<string>{"round", "square"};

  struct line_ends {
    line_end a = line_end::cap;
    line_end b = line_end::cap;
  };

  struct dgram_camera {
    bool  orthographic = true;
    vec2f center       = {0, 0};
    vec3f from         = {0, 0, 1};
    vec3f to           = {0, 0, 0};
    float lens         = 0.036f;
    float film         = 0.036f;
  };

  struct dgram_object {
    frame3f frame    = identity3x4f;
    int     shape    = -1;
    int     material = -1;
    int     labels   = -1;
  };

  struct dgram_material {
    vec4f fill   = {0, 0, 0, 1};
    vec4f stroke = {0, 0, 0, 1};

    float thickness = 2;

    float         dash_period = 20.0f;
    float         dash_phase  = 5.0f;
    float         dash_on     = 12.0f;
    dash_cap_type dash_cap    = dash_cap_type::square;

    dashed_line dashed = dashed_line::transparency;
  };

  struct dgram_shape {
    vector<vec3f> positions = {};

    vector<int>   points    = {};
    vector<vec2i> lines     = {};
    vector<vec3i> triangles = {};
    vector<vec4i> quads     = {};

    // flll colors for quads
    vector<vec4f> fills = {};

    // end types for lines
    vector<line_ends> ends = {};

    bool cull     = false;
    bool boundary = false;

    vector<vec3f> cclips = {};
  };

  struct dgram_label {
    vector<string> names      = {};
    vector<vec3f>  positions  = {};
    vector<string> texts      = {};
    vector<vec2f>  offsets    = {};
    vector<vec2f>  alignments = {};

    vector<image_data> images = {};
  };

  struct dgram_scene {
    vec2f                  offset    = {0, 0};
    vector<dgram_camera>   cameras   = {};
    vector<dgram_object>   objects   = {};
    vector<dgram_material> materials = {};
    vector<dgram_shape>    shapes    = {};
    vector<dgram_label>    labels    = {};
  };

  struct dgram_scenes {
    vec2f               size   = {720, 480};
    float               scale  = 80;
    vector<dgram_scene> scenes = {};
  };

  ray3f eval_camera(const dgram_camera& camera, const vec2f& image_uv,
      const vec2f& size, const float& scale);

}  // namespace yocto

#endif