//
// LICENSE:
//
// Copyright (c) 2016 -- 2021 Fabio Pellacini
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

#include <yocto_dgram/yocto_cli.h>
#include <yocto_dgram/yocto_gui.h>
#include <yocto_dgram/yocto_image.h>
#include <yocto_dgram/yocto_math.h>
#include <yocto_dgram/yocto_scene.h>
#include <yocto_dgram/yocto_sceneio.h>
#include <yocto_dgram/yocto_shape.h>
#include <yocto_dgram/yocto_trace.h>

using namespace yocto;

#include <filesystem>
namespace fs = std::filesystem;

// render params
struct render_params : trace_params {
  string scene     = "scene.json";
  string output    = "out.png";
  bool   savebatch = false;
};

// Cli
void add_options(cli_command& cli, render_params& params) {
  add_option(cli, "scene", params.scene, "scene filename");
  add_option(cli, "output", params.output, "output filename");
  add_option(cli, "camera", params.camera, "camera id");
  add_option(cli, "savebatch", params.savebatch, "save batch");
  add_option(cli, "resolution", params.resolution, "image resolution");
  add_option(cli, "antialiasing", params.antialiasing, "antialiasing type",
      antialiasing_labels);
  add_option(
      cli, "sampler", params.sampler, "sampler type", trace_sampler_labels);
  add_option(cli, "samples", params.samples, "number of samples");
  add_option(cli, "batch", params.batch, "sample batch");
  add_option(cli, "transparent_background", params.transparent_background,
      "hide environment");
  add_option(cli, "highqualitybvh", params.highqualitybvh, "high quality bvh");
  add_option(cli, "noparallel", params.noparallel, "disable threading");
}

// render diagram
void run_render(const render_params& params_) {
  print_info("rendering {}", params_.scene);
  auto timer = simple_timer{};

  // copy params
  auto params = params_;

  // scene loading
  timer      = simple_timer{};
  auto dgram = load_scene(params.scene);
  print_info("load scenes: {}", elapsed_formatted(timer));

  auto aspect = dgram.size.x / dgram.size.y;
  auto width  = params.resolution;
  auto height = (int)round(params.resolution / aspect);

  if (aspect < 1) swap(width, height);

  auto image = make_image(width, height, true);

  if (!params.transparent_background)
    image.pixels = vector<vec4f>(width * height, vec4f{1, 1, 1, 1});
  
  for (auto idx = 0; idx < dgram.scenes.size(); idx++) {
    auto& scene = dgram.scenes[idx];
    // build bvh
    cull_shapes(scene, params.camera);
    compute_radius(scene, params.camera, dgram.size, dgram.resolution);
    compute_text(
        scene, params.camera, params.resolution, dgram.size, dgram.resolution);
    compute_borders(scene);
    auto bvh = make_bvh(scene, params);

    // state
    auto state = make_state(scene, params);

    // render
    timer = simple_timer{};
    for (auto sample = 0; sample < params.samples; sample++) {
      auto sample_timer = simple_timer{};
      trace_samples(state, scene, bvh, params);
      print_info("render sample {}/{}: {}", sample, params.samples,
          elapsed_formatted(sample_timer));
    }
    print_info("render image: {}", elapsed_formatted(timer));

    image = composite_image(get_render(state), image);
    /*timer            = simple_timer{};
    auto image       = get_render(state);
    auto outfilename = fs::path(params.output);
    outfilename.replace_filename(outfilename.stem().string() +
                                 std::to_string(idx) +
                                 outfilename.extension().string());
    if (!is_hdr_filename(params.output)) image = tonemap_image(image, 0);
    save_image(outfilename.string(), image);
    print_info("save image: {}", elapsed_formatted(timer));*/
  }

  // save image
  timer = simple_timer{};
  if (!is_hdr_filename(params.output)) image = tonemap_image(image, 0);
  save_image(params.output, image);
  print_info("save image: {}", elapsed_formatted(timer));
}

// view params
struct view_params : trace_params {
  string scene  = "scene.json";
  string output = "out.png";
  int    camid  = 0;
};

// Cli
void add_options(cli_command& cli, view_params& params) {
  add_option(cli, "scene", params.scene, "scene filename");
  add_option(cli, "output", params.output, "output filename");
  add_option(cli, "camera", params.camid, "camera id");
  add_option(cli, "resolution", params.resolution, "image resolution");
  add_option(cli, "antialiasing", params.antialiasing, "antialiasing type",
      antialiasing_labels);
  add_option(
      cli, "sampler", params.sampler, "sampler type", trace_sampler_labels);
  add_option(cli, "samples", params.samples, "number of samples");
  add_option(cli, "batch", params.batch, "sample batch");
  add_option(cli, "transparent_background", params.transparent_background,
      "hide environment");
  add_option(
      cli, "--highqualitybvh", params.highqualitybvh, "use high quality BVH");
  add_option(cli, "noparallel", params.noparallel, "disable threading");
}

// view diagram
void run_view(const view_params& params_) {
  /*print_info("viewing {}", params_.scene);
  auto timer = simple_timer{};

  // copy params
  auto params = params_;

  // load scene
  timer      = simple_timer{};
  auto scene = load_scene(params.scene);
  print_info("load scene: {}", elapsed_formatted(timer));

  // find camera
  params.camera = find_camera(scene, params.camid);

  // run view
  show_trace_gui("dscene", params.scene, scene, params, true, true);*/
}

struct app_params {
  string        command = "view";
  render_params render  = {};
  view_params   view    = {};
};

// Run
int main(int argc, const char* argv[]) {
  try {
    // command line parameters
    auto params = app_params{};
    auto cli    = make_cli("dscene", "render and view scenes");
    add_command_var(cli, params.command);
    add_command(cli, "render", params.render, "render scenes");
    add_command(cli, "view", params.view, "view scenes");
    parse_cli(cli, argc, argv);

    // dispatch commands
    if (params.command == "render") {
      run_render(params.render);
    } else if (params.command == "view") {
      run_view(params.view);
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
