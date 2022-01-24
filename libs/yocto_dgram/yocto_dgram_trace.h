//
// # Yocto/Dgram trace: Path tracing
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

#ifndef _YOCTO_DGRAM_TRACE_H_
#define _YOCTO_DGRAM_TRACE_H_

// -----------------------------------------------------------------------------
// INCLUDES
// -----------------------------------------------------------------------------

#include <yocto/yocto_sampling.h>

#include "yocto_dgram.h"
#include "yocto_dgram_bvh.h"
#include "yocto_dgram_shape.h"
#include "yocto_dgram_text.h"

// -----------------------------------------------------------------------------
// USING DIRECTIVES
// -----------------------------------------------------------------------------
namespace yocto {

  // using directives

}  // namespace yocto

// -----------------------------------------------------------------------------
// RENDERING API
// -----------------------------------------------------------------------------
namespace yocto {

  // Type of tracing algorithm
  enum struct dgram_sampler_type { color, normal, uv, eyelight };

  // Type of antialiasing
  enum struct antialiasing_type { random_sampling, super_sampling };

  const auto dgram_default_seed = 961748941ull;

  struct dgram_trace_params {
    int                camera       = 0;
    float              scale        = 0.0f;
    vec2f              size         = {0, 0};
    int                width        = 0;
    int                height       = 0;
    int                samples      = 0;
    uint64_t           seed         = dgram_default_seed;
    dgram_sampler_type sampler      = dgram_sampler_type::color;
    antialiasing_type  antialiasing = antialiasing_type::super_sampling;
    bool               noparallel   = false;
  };

}  // namespace yocto

// -----------------------------------------------------------------------------
// LOWER-LEVEL RENDERING API
// -----------------------------------------------------------------------------
namespace yocto {

  struct dgram_trace_state {
    int               width   = 0;
    int               height  = 0;
    int               samples = 0;
    vector<vec4f>     image   = {};
    vector<rng_state> rngs    = {};
  };

  dgram_trace_state make_state(const dgram_trace_params& params);

  void trace_samples(dgram_trace_state& state, const dgram_scene& scene,
      const trace_shapes& shapes, const trace_texts& texts,
      const dgram_scene_bvh& bvh, const dgram_trace_params& params);
  void trace_sample(dgram_trace_state& state, const dgram_scene& scene,
      const trace_shapes& shapes, const trace_texts& texts,
      const dgram_scene_bvh& bvh, int i, int j,
      const dgram_trace_params& params);

  image_data get_render(const dgram_trace_state& state);
  void       get_render(image_data& render, const dgram_trace_state& state);

}  // namespace yocto

// -----------------------------------------------------------------------------
// ENUM LABELS
// -----------------------------------------------------------------------------
namespace yocto {

  // trace sampler names
  inline const auto dgram_sampler_names = vector<string>{
      "color", "normal", "uv", "eyelight"};

  // trace sampler labels
  inline const auto dgram_sampler_labels =
      vector<pair<dgram_sampler_type, string>>{
          {dgram_sampler_type::color, "color"},
          {dgram_sampler_type::normal, "normal"},
          {dgram_sampler_type::uv, "uv"},
          {dgram_sampler_type::eyelight, "eyelight"}};

  // antialiasing names
  inline const auto antialiasing_names = vector<string>{
      "random_sampling", "super_sampling"};

  // antialiasing labels
  inline const auto antialiasing_labels =
      vector<pair<antialiasing_type, string>>{
          {antialiasing_type::random_sampling, "random_sampling"},
          {antialiasing_type::super_sampling, "super_sampling"}};

}  // namespace yocto
#endif