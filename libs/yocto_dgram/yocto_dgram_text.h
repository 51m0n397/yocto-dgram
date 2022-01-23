//
// # Yocto/Dgram text: Dgram text utilities
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

#ifndef _YOCTO_DGRAM_TEXT_H_
#define _YOCTO_DGRAM_TEXT_H_

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
// BVH
// -----------------------------------------------------------------------------
namespace yocto {

  struct trace_text {
    vector<vec3f> positions = {};
    image_data    image     = {};
  };

  struct trace_texts {
    vector<trace_text> texts = {};
  };

  trace_texts make_texts(const dgram_scene& scene, const int& cam,
      const vec2f& size, const float& scale, const int width, const int height,
      const bool noparallel = false);

  bool intersect_text(const trace_text& text, const ray3f& ray, vec2f& uv);

}  // namespace yocto

// -----------------------------------------------------------------------------
// TEXT PROPERTIES EVALUATION
// -----------------------------------------------------------------------------
namespace yocto {

  vec4f eval_text(const trace_text& text, const vec2f& uv);

}  // namespace yocto

#endif