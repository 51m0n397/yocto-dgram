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
#include <yocto_dgram/yocto_math.h>
#include <yocto_dgram/yocto_scene.h>
#include <yocto_dgram/yocto_sceneio.h>
#include <yocto_dgram/yocto_shape.h>
#include <yocto_dgram/yocto_trace.h>

using namespace yocto;

#include <filesystem>
namespace fs = std::filesystem;

// convert params
struct convert_params {
  string scene     = "scene.ply";
  string output    = "out.ply";
  bool   info      = false;
  bool   validate  = false;
  string copyright = "";
};

// Cli
void add_options(cli_command& cli, convert_params& params) {
  add_option(cli, "scene", params.scene, "input scene");
  add_option(cli, "output", params.output, "output scene");
  add_option(cli, "info", params.info, "print info");
  add_option(cli, "validate", params.validate, "validate scene");
  add_option(cli, "copyright", params.copyright, "set scene copyright");
}

// convert images
void run_convert(const convert_params& params) {
  print_info("converting {}", params.scene);
  auto timer = simple_timer{};

  // load scene
  timer      = simple_timer{};
  auto scene = load_scene(params.scene);
  print_info("load scene: {}", elapsed_formatted(timer));

  // copyright
  if (params.copyright != "") {
    scene.copyright = params.copyright;
  }

  // validate scene
  if (params.validate) {
    auto errors = scene_validation(scene);
    for (auto& error : errors) print_error(error);
    if (!errors.empty()) throw io_error{"invalid scene"};
  }

  // print info
  if (params.info) {
    print_info("scene stats ------------");
    for (auto stat : scene_stats(scene)) print_info(stat);
  }

  // save scene
  timer = simple_timer{};
  make_scene_directories(params.output, scene);
  save_scene(params.output, scene);
  print_info("save scene: {}", elapsed_formatted(timer));
}

// info params
struct info_params {
  string scene    = "scene.ply";
  bool   validate = false;
};

// Cli
void add_options(cli_command& cli, info_params& params) {
  add_option(cli, "scene", params.scene, "input scene");
  add_option(cli, "validate", params.validate, "validate scene");
}

// print info for scenes
void run_info(const info_params& params) {
  print_info("info for {}", params.scene);
  auto timer = simple_timer{};

  // load scene
  timer      = simple_timer{};
  auto scene = load_scene(params.scene);
  print_info("load scene: {}" + elapsed_formatted(timer));

  // validate scene
  if (params.validate) {
    for (auto& error : scene_validation(scene)) print_error(error);
  }

  // print info
  print_info("scene stats ------------");
  for (auto stat : scene_stats(scene)) print_info(stat);
}

// render params
struct render_params : trace_params {
  string scene     = "scene.json";
  string output    = "out.png";
  string camname   = "";
  bool   savebatch = false;
};

// Cli
void add_options(cli_command& cli, render_params& params) {
  add_option(cli, "scene", params.scene, "scene filename");
  add_option(cli, "output", params.output, "output filename");
  add_option(cli, "camera", params.camname, "camera name");
  add_option(cli, "savebatch", params.savebatch, "save batch");
  add_option(cli, "resolution", params.resolution, "image resolution");
  add_option(
      cli, "sampler", params.sampler, "sampler type", trace_sampler_labels);
  add_option(cli, "samples", params.samples, "number of samples");
  add_option(cli, "bounces", params.bounces, "number of bounces");
  add_option(cli, "batch", params.batch, "sample batch");
  add_option(cli, "clamp", params.clamp, "clamp params");
  add_option(cli, "transparent_background", params.transparent_background,
      "hide environment");
  add_option(cli, "tentfilter", params.tentfilter, "filter image");
  add_option(cli, "highqualitybvh", params.highqualitybvh, "high quality bvh");
  add_option(cli, "exposure", params.exposure, "exposure value");
  add_option(cli, "noparallel", params.noparallel, "disable threading");
}

// convert images
void run_render(const render_params& params_) {
  print_info("rendering {}", params_.scene);
  auto timer = simple_timer{};

  // copy params
  auto params = params_;

  // scene loading
  timer      = simple_timer{};
  auto scene = load_scene(params.scene);
  print_info("load scene: {}", elapsed_formatted(timer));

  // camera
  params.camera = find_camera(scene, params.camname);

  // build bvh
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
    if (params.savebatch && state.samples % params.batch == 0) {
      auto image       = get_render(state);
      auto outfilename = fs::path(params.output)
                             .replace_extension(
                                 "-s" + std::to_string(sample) +
                                 fs::path(params.output).extension().string())
                             .string();
      if (!is_hdr_filename(params.output))
        image = tonemap_image(image, params.exposure);
      save_image(outfilename, image);
    }
  }
  print_info("render image: {}", elapsed_formatted(timer));

  // save image
  timer      = simple_timer{};
  auto image = get_render(state);
  if (!is_hdr_filename(params.output))
    image = tonemap_image(image, params.exposure);
  save_image(params.output, image);
  print_info("save image: {}", elapsed_formatted(timer));
}

// convert params
struct view_params : trace_params {
  string scene   = "scene.json";
  string output  = "out.png";
  string camname = "";
};

// Cli
void add_options(cli_command& cli, view_params& params) {
  add_option(cli, "scene", params.scene, "scene filename");
  add_option(cli, "output", params.output, "output filename");
  add_option(cli, "camera", params.camname, "camera name");
  add_option(cli, "resolution", params.resolution, "image resolution");
  add_option(
      cli, "sampler", params.sampler, "sampler type", trace_sampler_labels);
  add_option(cli, "samples", params.samples, "number of samples");
  add_option(cli, "bounces", params.bounces, "number of bounces");
  add_option(cli, "batch", params.batch, "sample batch");
  add_option(cli, "clamp", params.clamp, "clamp params");
  add_option(cli, "transparent_background", params.transparent_background,
      "hide environment");
  add_option(cli, "tentfilter", params.tentfilter, "filter image");
  add_option(
      cli, "--highqualitybvh", params.highqualitybvh, "use high quality BVH");
  add_option(cli, "exposure", params.exposure, "exposure value");
  add_option(cli, "noparallel", params.noparallel, "disable threading");
}

// view scene
void run_view(const view_params& params_) {
  print_info("viewing {}", params_.scene);
  auto timer = simple_timer{};

  // copy params
  auto params = params_;

  // load scene
  timer      = simple_timer{};
  auto scene = load_scene(params.scene);
  print_info("load scene: {}", elapsed_formatted(timer));

  // find camera
  params.camera = find_camera(scene, params.camname);

  // run view
  show_trace_gui("dscene", params.scene, scene, params);
}

struct app_params {
  string         command = "convert";
  convert_params convert = {};
  info_params    info    = {};
  render_params  render  = {};
  view_params    view    = {};
};

// Run
int main(int argc, const char* argv[]) {
  try {
    // command line parameters
    auto params = app_params{};
    auto cli    = make_cli("dscene", "process and view scenes");
    add_command_var(cli, params.command);
    add_command(cli, "convert", params.convert, "convert scenes");
    add_command(cli, "info", params.info, "print scenes info");
    add_command(cli, "render", params.render, "render scenes");
    add_command(cli, "view", params.view, "view scenes");
    parse_cli(cli, argc, argv);

    // dispatch commands
    if (params.command == "convert") {
      run_convert(params.convert);
    } else if (params.command == "info") {
      run_info(params.info);
    } else if (params.command == "render") {
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
