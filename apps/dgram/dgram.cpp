//
// LICENSE:
//
// Copyright (c) 2021 -- 2022 Simone Bartolini
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//

#include <yocto/yocto_cli.h>
#include <yocto/yocto_image.h>
#include <yocto/yocto_math.h>
#include <yocto_dgram/yocto_dgram.h>
#include <yocto_dgram/yocto_dgram_bvh.h>
#include <yocto_dgram/yocto_dgram_gui.h>
#include <yocto_dgram/yocto_dgram_shape.h>
#include <yocto_dgram/yocto_dgram_text.h>
#include <yocto_dgram/yocto_dgram_trace.h>
#include <yocto_dgram/yocto_dgramio.h>

using namespace yocto;

#include <filesystem>
namespace fs = std::filesystem;

// render params
struct render_params {
  string             scene                  = "scene.json";
  string             output                 = "out.png";
  int                resolution             = 0;
  bool               transparent_background = false;
  int                samples                = 9;
  bool               highqualitybvh         = false;
  bool               noparallel             = false;
  dgram_sampler_type sampler                = dgram_sampler_type::color;
  antialiasing_type  antialiasing           = antialiasing_type::super_sampling;
};

// Cli
void add_options(cli_command& cli, render_params& params) {
  add_option(cli, "scene", params.scene, "scene filename");
  add_option(cli, "output", params.output, "output filename");
  add_option(cli, "resolution", params.resolution, "image resolution");
  add_option(cli, "transparent_background", params.transparent_background,
      "hide background");
  add_option(cli, "samples", params.samples, "number of samples");
  add_option(cli, "highqualitybvh", params.highqualitybvh, "high quality bvh");
  add_option(cli, "noparallel", params.noparallel, "disable threading");
  add_option(cli, "antialiasing", params.antialiasing, "antialiasing type",
      antialiasing_labels);
  add_option(
      cli, "sampler", params.sampler, "sampler type", dgram_sampler_labels);
}

// render diagram
void run_render(const render_params& params_) {
  print_info("rendering {}", params_.scene);
  auto timer = simple_timer{};

  // copy params
  auto params = params_;

  // scene loading
  timer      = simple_timer{};
  auto dgram = load_dgram(params.scene);
  print_info("load diagram: {}", elapsed_formatted(timer));

  if (params.resolution == 0) params.resolution = 2 * (int)round(dgram.size.x);

  auto aspect = dgram.size.x / dgram.size.y;
  auto width  = params.resolution;
  auto height = (int)round(params.resolution / aspect);

  auto image = make_image(width, height, true);

  if (!params.transparent_background)
    image.pixels = vector<vec4f>(width * height, vec4f{1, 1, 1, 1});

  for (auto idx = 0; idx < dgram.scenes.size(); idx++) {
    auto& scene = dgram.scenes[idx];
    timer       = simple_timer{};

    auto params_         = dgram_trace_params{};
    params_.width        = width;
    params_.height       = height;
    params_.samples      = params.samples;
    params_.noparallel   = params.noparallel;
    params_.scale        = dgram.scale;
    params_.size         = dgram.size;
    params_.sampler      = params.sampler;
    params_.antialiasing = params.antialiasing;

    // build bvh
    auto shapes = make_shapes(
        scene, params_.camera, params_.size, params_.scale, params_.noparallel);
    auto bvh = make_bvh(shapes, params.highqualitybvh, params_.noparallel);

    // make texts
    auto texts = make_texts(scene, params_.camera, params_.size, params_.scale,
        params_.width, params_.height, params_.noparallel);

    // make state
    auto state = make_state(params_);

    // render
    timer = simple_timer{};
    for (auto sample = 0; sample < params.samples; sample++) {
      auto sample_timer = simple_timer{};
      trace_samples(state, scene, shapes, texts, bvh, params_);
      print_info("render sample {}/{}: {}", sample + 1, params.samples,
          elapsed_formatted(sample_timer));
    }
    print_info("render scene: {}/{}: {}", idx + 1, dgram.scenes.size(),
        elapsed_formatted(timer));

    image = composite_image(get_render(state), image);
  }

  // save image
  timer = simple_timer{};
  if (!is_hdr_filename(params.output)) image = tonemap_image(image, 0);
  save_image(params.output, image);
  print_info("save image: {}", elapsed_formatted(timer));
}

// view params
struct view_params {
  string             scene                  = "scene.json";
  int                resolution             = 0;
  bool               transparent_background = false;
  int                samples                = 9;
  bool               highqualitybvh         = false;
  bool               noparallel             = false;
  dgram_sampler_type sampler                = dgram_sampler_type::color;
  antialiasing_type  antialiasing           = antialiasing_type::super_sampling;
};

// Cli
void add_options(cli_command& cli, view_params& params) {
  add_option(cli, "scene", params.scene, "scene filename");
  add_option(cli, "resolution", params.resolution, "image resolution");
  add_option(cli, "transparent_background", params.transparent_background,
      "hide background");
  add_option(cli, "samples", params.samples, "number of samples");
  add_option(cli, "highqualitybvh", params.highqualitybvh, "high quality bvh");
  add_option(cli, "noparallel", params.noparallel, "disable threading");
  add_option(cli, "antialiasing", params.antialiasing, "antialiasing type",
      antialiasing_labels);
  add_option(
      cli, "sampler", params.sampler, "sampler type", dgram_sampler_labels);
}

void run_view(const view_params& params_) {
  print_info("rendering {}", params_.scene);
  auto timer = simple_timer{};

  // scene loading
  timer      = simple_timer{};
  auto dgram = load_dgram(params_.scene);
  print_info("load diagram: {}", elapsed_formatted(timer));

  auto params         = dgram_trace_params{};
  params.samples      = params_.samples;
  params.noparallel   = params_.noparallel;
  params.scale        = dgram.scale;
  params.size         = dgram.size;
  params.sampler      = params_.sampler;
  params.antialiasing = params_.antialiasing;

  auto res = params_.resolution;
  if (res == 0) res = 2 * (int)round(dgram.size.x);

  auto aspect = dgram.size.x / dgram.size.y;
  auto width  = res;
  auto height = (int)round(res / aspect);

  params.width  = width;
  params.height = height;

  show_dgram_gui(dgram, params, params_.transparent_background);
}

// text params
struct text_params {
  string scene      = "scene.json";
  int    resolution = 0;
  bool   noparallel = false;
};

// Cli
void add_options(cli_command& cli, text_params& params) {
  add_option(cli, "scene", params.scene, "scene filename");
  add_option(cli, "resolution", params.resolution, "image resolution");
  add_option(cli, "noparallel", params.noparallel, "disable threading");
}

void run_text(const text_params& params_) {
  print_info("rendering {}", params_.scene);
  auto timer = simple_timer{};

  // scene loading
  timer      = simple_timer{};
  auto dgram = load_dgram(params_.scene);
  print_info("load diagram: {}", elapsed_formatted(timer));

  auto resolution = params_.resolution;
  if (resolution == 0) resolution = 2 * (int)round(dgram.size.x);

  save_texts(params_.scene, dgram, resolution);
}

struct app_params {
  string        command = "render";
  render_params render  = {};
  view_params   view    = {};
  text_params   text    = {};
};

// Run
int main(int argc, const char* argv[]) {
  try {
    // command line parameters
    auto params = app_params{};
    auto cli    = make_cli("dscene", "render and view diagrams");
    add_command_var(cli, params.command);
    add_command(cli, "render", params.render, "render diagrams");
    add_command(cli, "view", params.view, "view diagrams");
    add_command(cli, "render_text", params.text, "render text for diagrams");
    parse_cli(cli, argc, argv);

    // dispatch commands
    if (params.command == "render") {
      run_render(params.render);
    } else if (params.command == "view") {
      run_view(params.view);
    } else if (params.command == "render_text") {
      run_text(params.text);
    } else {
      throw io_error{"unknown command"};
    }
  } catch (const std::exception& error) {
    print_error(error.what());
    return 1;
  }

  // done
  return 0;
}
