//
// # Yocto/Trace: Path tracing
//
// Yocto/Trace is a simple path tracer written on the Yocto/Scene model.
// Yocto/Trace is implemented in `yocto_trace.h` and `yocto_trace.cpp`.
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

#ifndef _YOCTO_TRACE_H_
#define _YOCTO_TRACE_H_

// -----------------------------------------------------------------------------
// INCLUDES
// -----------------------------------------------------------------------------

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "yocto_bvh.h"
#include "yocto_image.h"
#include "yocto_math.h"
#include "yocto_sampling.h"
#include "yocto_scene.h"

// -----------------------------------------------------------------------------
// USING DIRECTIVES
// -----------------------------------------------------------------------------
namespace yocto {

  // using directives
  using std::pair;
  using std::string;
  using std::vector;

}  // namespace yocto

// -----------------------------------------------------------------------------
// RENDERING API
// -----------------------------------------------------------------------------
namespace yocto {

  // Type of tracing algorithm
  enum struct trace_sampler_type { color, normal, eyelight };

  // Type of antialiasing
  enum struct antialiasing_type { random_sampling, super_sampling };

  // Default trace seed
  const auto trace_default_seed = 961748941ull;

  // Options for trace functions
  struct trace_params {
    int                camera       = 0;
    int                resolution   = 1280;
    trace_sampler_type sampler      = trace_sampler_type::color;
    antialiasing_type  antialiasing = antialiasing_type::super_sampling;
    int                samples      = 9;
    bool               transparent_background = false;
    uint64_t           seed                   = trace_default_seed;
    bool               highqualitybvh         = false;
    bool               noparallel             = false;
    int                pratio                 = 8;
    int                batch                  = 1;
  };

}  // namespace yocto

// -----------------------------------------------------------------------------
// LOWER-LEVEL RENDERING API
// -----------------------------------------------------------------------------
namespace yocto {

  // Trace state
  struct trace_state {
    int               width   = 0;
    int               height  = 0;
    int               samples = 0;
    vector<vec4f>     image   = {};
    vector<int>       hits    = {};
    vector<rng_state> rngs    = {};
  };

  // Initialize state.
  trace_state make_state(const scene_data& scene, const trace_params& params);

  // Build the bvh acceleration structure.
  scene_bvh make_bvh(const scene_data& scene, const trace_params& params);

  // Progressively computes an image.
  void trace_samples(trace_state& state, const scene_data& scene,
      const scene_bvh& bvh, const trace_params& params);
  void trace_sample(trace_state& state, const scene_data& scene,
      const scene_bvh& bvh, int i, int j, const trace_params& params);

  // Get resulting render
  image_data get_render(const trace_state& state);
  void       get_render(image_data& render, const trace_state& state);

}  // namespace yocto

// -----------------------------------------------------------------------------
// ENUM LABELS
// -----------------------------------------------------------------------------
namespace yocto {

  // trace sampler names
  inline const auto trace_sampler_names = vector<string>{
      "color", "normal", "eyelight"};

  // trace sampler labels
  inline const auto trace_sampler_labels =
      vector<pair<trace_sampler_type, string>>{
          {trace_sampler_type::color, "color"},
          {trace_sampler_type::normal, "normal"},
          {trace_sampler_type::eyelight, "eyelight"}};

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
