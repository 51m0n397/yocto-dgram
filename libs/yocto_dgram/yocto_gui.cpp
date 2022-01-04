//
// Simpler image viewer.
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

#include "yocto_gui.h"

#ifdef YOCTO_OPENGL

#include <glad/glad.h>

#include <cassert>
#include <cstdlib>
#include <future>
#include <stdexcept>

#include "yocto_geometry.h"

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#endif
#include <GLFW/glfw3.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_opengl3.h>
#include <imgui/imgui.h>
#include <imgui_internal.h>

#ifdef _WIN32
#undef near
#undef far
#endif

// -----------------------------------------------------------------------------
// PARALLEL HELPERS
// -----------------------------------------------------------------------------
namespace yocto {

  // Simple parallel for used since our target platforms do not yet support
  // parallel algorithms. `Func` takes the two integer indices.
  template <typename T, typename Func>
  inline void parallel_for(T num1, T num2, Func&& func) {
    auto              futures  = vector<std::future<void>>{};
    auto              nthreads = std::thread::hardware_concurrency();
    std::atomic<T>    next_idx(0);
    std::atomic<bool> has_error(false);
    for (auto thread_id = 0; thread_id < (int)nthreads; thread_id++) {
      futures.emplace_back(std::async(
          std::launch::async, [&func, &next_idx, &has_error, num1, num2]() {
            try {
              while (true) {
                auto j = next_idx.fetch_add(1);
                if (j >= num2) break;
                if (has_error) break;
                for (auto i = (T)0; i < num1; i++) func(i, j);
              }
            } catch (...) {
              has_error = true;
              throw;
            }
          }));
    }
    for (auto& f : futures) f.get();
  }

}  // namespace yocto

// -----------------------------------------------------------------------------
// IMAGE DRAWING
// -----------------------------------------------------------------------------
namespace yocto {

  // OpenGL image data
  struct glimage_state {
    // image properties
    int width  = 0;
    int height = 0;

    // Opengl state
    uint texture     = 0;  // texture
    uint program     = 0;  // program
    uint vertex      = 0;
    uint fragment    = 0;
    uint vertexarray = 0;  // vertex
    uint positions   = 0;
    uint triangles   = 0;  // elements
  };

  // create image drawing program
  static bool init_image(glimage_state& glimage);

  // clear image
  static void clear_image(glimage_state& glimage);

  // update image data
  static void set_image(glimage_state& glimage, const image_data& image);

  // OpenGL image drawing params
  struct glimage_params {
    vec2i window      = {512, 512};
    vec4i framebuffer = {0, 0, 512, 512};
    vec2f center      = {0, 0};
    float scale       = 1;
    bool  fit         = true;
    bool  checker     = true;
    float border_size = 2;
    vec4f background  = {0.15f, 0.15f, 0.15f, 1.0f};
  };

  // draw image
  static void draw_image(glimage_state& image, const glimage_params& params);

}  // namespace yocto

// -----------------------------------------------------------------------------
// VIEW HELPERS
// -----------------------------------------------------------------------------
namespace yocto {

  static void update_image_params(const gui_input& input,
      const image_data& image, glimage_params& glparams) {
    glparams.window                           = input.window;
    glparams.framebuffer                      = input.framebuffer;
    std::tie(glparams.center, glparams.scale) = camera_imview(glparams.center,
        glparams.scale, {image.width, image.height}, glparams.window,
        glparams.fit);
  }

  static bool uiupdate_camera_params(
      const gui_input& input, camera_data& camera) {
    if (input.mouse.x && input.modifiers.x && !input.onwidgets) {
      auto dolly  = 0.0f;
      auto pan    = zero2f;
      auto rotate = zero2f;
      if (input.modifiers.y) {
        pan   = (input.cursor - input.last) * camera.focus / 200.0f;
        pan.x = -pan.x;
      } else if (input.modifiers.z) {
        dolly = (input.cursor.y - input.last.y) / 100.0f;
      } else {
        rotate = (input.cursor - input.last) / 100.0f;
      }
      auto [frame, focus] = camera_turntable(
          camera.frame, camera.focus, rotate, dolly, pan);
      if (camera.frame != frame || camera.focus != focus) {
        camera.frame = frame;
        camera.focus = focus;
        return true;
      }
    }
    return false;
  }

  static bool draw_image_inspector(const gui_input& input,
      const image_data& image, const image_data& display,
      glimage_params& glparams) {
    if (draw_gui_header("inspect")) {
      draw_gui_slider("zoom", glparams.scale, 0.1f, 10);
      draw_gui_checkbox("fit", glparams.fit);
      draw_gui_coloredit("background", glparams.background);
      auto [i, j] = image_coords(input.cursor, glparams.center, glparams.scale,
          {image.width, image.height});
      auto ij     = vec2i{i, j};
      draw_gui_dragger("mouse", ij);
      auto image_pixel   = zero4f;
      auto display_pixel = zero4f;
      if (i >= 0 && i < image.width && j >= 0 && j < image.height) {
        image_pixel   = image.pixels[j * image.width + i];
        display_pixel = image.pixels[j * image.width + i];
      }
      draw_gui_coloredit("image", image_pixel);
      draw_gui_coloredit("display", display_pixel);
      end_gui_header();
    }
    return false;
  }

  struct scene_selection {
    int camera   = 0;
    int instance = 0;
    int shape    = 0;
    int texture  = 0;
    int material = 0;
  };

  static bool draw_scene_editor(scene_data& scene, scene_selection& selection,
      const function<void()>& before_edit) {
    auto edited = 0;
    if (draw_gui_header("cameras")) {
      draw_gui_combobox("camera", selection.camera, scene.camera_names);
      auto camera = scene.cameras.at(selection.camera);
      edited += draw_gui_checkbox("ortho", camera.orthographic);
      edited += draw_gui_slider("lens", camera.lens, 0.001f, 1);
      edited += draw_gui_slider("aspect", camera.aspect, 0.1f, 5);
      edited += draw_gui_slider("film", camera.film, 0.1f, 0.5f);
      edited += draw_gui_slider("focus", camera.focus, 0.001f, 100);
      edited += draw_gui_slider("aperture", camera.aperture, 0, 1);
      //   frame3f frame        = identity3x4f;
      if (edited) {
        if (before_edit) before_edit();
        scene.cameras.at(selection.camera) = camera;
      }
      end_gui_header();
    }
    if (draw_gui_header("instances")) {
      draw_gui_combobox("instance", selection.instance, scene.instance_names);
      auto instance = scene.instances.at(selection.instance);
      edited += draw_gui_combobox("shape", instance.shape, scene.shape_names);
      edited += draw_gui_combobox(
          "material", instance.material, scene.material_names);
      //   frame3f frame        = identity3x4f;
      if (edited) {
        if (before_edit) before_edit();
        scene.instances.at(selection.instance) = instance;
      }
      end_gui_header();
    }
    if (draw_gui_header("materials")) {
      draw_gui_combobox("material", selection.material, scene.material_names);
      auto material = scene.materials.at(selection.material);
      edited += draw_gui_coloredithdr("fill", material.fill);
      edited += draw_gui_combobox(
          "fill_tex", material.fill_tex, scene.texture_names, true);
      edited += draw_gui_coloredithdr("stroke", material.stroke);
      edited += draw_gui_combobox(
          "stroke_tex", material.stroke_tex, scene.texture_names, true);
      if (edited) {
        if (before_edit) before_edit();
        scene.materials.at(selection.material) = material;
      }
      end_gui_header();
    }
    if (draw_gui_header("shapes")) {
      draw_gui_combobox("shape", selection.shape, scene.shape_names);
      auto& shape = scene.shapes.at(selection.shape);
      draw_gui_label("points", (int)shape.points.size());
      draw_gui_label("lines", (int)shape.lines.size());
      draw_gui_label("triangles", (int)shape.triangles.size());
      draw_gui_label("quads", (int)shape.quads.size());
      draw_gui_label("positions", (int)shape.positions.size());
      draw_gui_label("normals", (int)shape.normals.size());
      draw_gui_label("texcoords", (int)shape.texcoords.size());
      draw_gui_label("colors", (int)shape.colors.size());
      draw_gui_label("radius", (int)shape.radius.size());
      draw_gui_label("tangents", (int)shape.tangents.size());
      end_gui_header();
    }
    if (draw_gui_header("textures")) {
      draw_gui_combobox("texture", selection.texture, scene.texture_names);
      auto& texture = scene.textures.at(selection.texture);
      draw_gui_label("width", texture.width);
      draw_gui_label("height", texture.height);
      draw_gui_label("linear", texture.linear);
      draw_gui_label("byte", !texture.pixelsb.empty());
      end_gui_header();
    }
    return (bool)edited;
  }

}  // namespace yocto

// -----------------------------------------------------------------------------
// IMAGE AND TRACE VIEW
// -----------------------------------------------------------------------------
namespace yocto {

  // Open a window and show an scene via path tracing
  void show_trace_gui(const string& title, const string& name,
      scene_data& scene, const trace_params& params_, bool print, bool edit) {
    // copy params and camera
    auto params = params_;

    // build bvh
    auto bvh = make_bvh(scene, params);

    // init state
    auto state   = make_state(scene, params);
    auto image   = make_image(state.width, state.height, true);
    auto display = make_image(state.width, state.height, false);
    auto render  = make_image(state.width, state.height, true);

    // opengl image
    auto glimage  = glimage_state{};
    auto glparams = glimage_params{};

    // top level combo
    auto names    = vector<string>{name};
    auto selected = 0;

    // camera names
    auto camera_names = scene.camera_names;
    if (camera_names.empty()) {
      for (auto idx = 0; idx < (int)scene.cameras.size(); idx++) {
        camera_names.push_back("camera" + std::to_string(idx + 1));
      }
    }

    // renderer update
    auto render_update  = std::atomic<bool>{};
    auto render_current = std::atomic<int>{};
    auto render_mutex   = std::mutex{};
    auto render_worker  = std::future<void>{};
    auto render_stop    = std::atomic<bool>{};
    auto reset_display  = [&]() {
      // stop render
      render_stop = true;
      if (render_worker.valid()) render_worker.get();

      state   = make_state(scene, params);
      image   = make_image(state.width, state.height, true);
      display = make_image(state.width, state.height, false);
      render  = make_image(state.width, state.height, true);

      render_worker = {};
      render_stop   = false;

      // preview
      auto pparams = params;
      pparams.resolution /= params.pratio;
      pparams.samples = 1;
      auto pstate     = make_state(scene, pparams);
      trace_samples(pstate, scene, bvh, pparams);
      auto preview = get_render(pstate);
      for (auto idx = 0; idx < state.width * state.height; idx++) {
        auto i = idx % render.width, j = idx / render.width;
        auto pi            = clamp(i / params.pratio, 0, preview.width - 1),
             pj            = clamp(j / params.pratio, 0, preview.height - 1);
        render.pixels[idx] = preview.pixels[pj * preview.width + pi];
      }
      // if (current > 0) return;
      {
        auto lock      = std::lock_guard{render_mutex};
        render_current = 0;
        image          = render;
        tonemap_image_mt(display, image, params.exposure);
        render_update = true;
      }

      // start renderer
      render_worker = std::async(std::launch::async, [&]() {
        for (auto sample = 0; sample < params.samples; sample += params.batch) {
          if (render_stop) return;
          parallel_for(state.width, state.height, [&](int i, int j) {
            for (auto s = 0; s < params.batch; s++) {
              if (render_stop) return;
              trace_sample(state, scene, bvh, i, j, params);
            }
           });
          state.samples += params.batch;
          if (!render_stop) {
            auto lock      = std::lock_guard{render_mutex};
            render_current = state.samples;
            get_render(render, state);
            image = render;
            tonemap_image_mt(display, image, params.exposure);
            render_update = true;
          }
        }
       });
    };

    // stop render
    auto stop_render = [&]() {
      render_stop = true;
      if (render_worker.valid()) render_worker.get();
    };

    // start rendering
    reset_display();

    // prepare selection
    auto selection = scene_selection{};

    // callbacks
    auto callbacks = gui_callbacks{};
    callbacks.init = [&](const gui_input& input) {
      auto lock = std::lock_guard{render_mutex};
      init_image(glimage);
      set_image(glimage, display);
    };
    callbacks.clear = [&](const gui_input& input) { clear_image(glimage); };
    callbacks.draw  = [&](const gui_input& input) {
      // update image
      if (render_update) {
        auto lock = std::lock_guard{render_mutex};
        set_image(glimage, display);
        render_update = false;
      }
      update_image_params(input, image, glparams);
      draw_image(glimage, glparams);
    };
    callbacks.widgets = [&](const gui_input& input) {
      auto edited = 0;
      draw_gui_combobox("name", selected, names);
      auto current = (int)render_current;
      draw_gui_progressbar("sample", current, params.samples);
      if (draw_gui_header("render")) {
        auto edited  = 0;
        auto tparams = params;
        edited += draw_gui_combobox("camera", tparams.camera, camera_names);
        edited += draw_gui_slider("resolution", tparams.resolution, 180, 4096);
        edited += draw_gui_slider("samples", tparams.samples, 1, 4096);
        edited += draw_gui_combobox(
            "tracer", (int&)tparams.sampler, trace_sampler_names);
        edited += draw_gui_slider("bounces", tparams.bounces, 1, 128);
        edited += draw_gui_slider("batch", tparams.batch, 1, 16);
        edited += draw_gui_slider("clamp", tparams.clamp, 10, 1000);
        edited += draw_gui_checkbox(
            "transparent background", tparams.transparent_background);
        continue_gui_line();
        edited += draw_gui_checkbox("filter", tparams.tentfilter);
        edited += draw_gui_slider("pratio", tparams.pratio, 1, 64);
        end_gui_header();
        if (edited) {
          stop_render();
          params = tparams;
          reset_display();
        }
      }
      if (draw_gui_header("tonemap")) {
        edited += draw_gui_slider("exposure", params.exposure, -5, 5);
        end_gui_header();
        if (edited) {
          tonemap_image_mt(display, image, params.exposure);
          set_image(glimage, display);
        }
      }
      draw_image_inspector(input, image, display, glparams);
      if (edit) {
        if (draw_scene_editor(scene, selection, [&]() { stop_render(); })) {
          reset_display();
        }
      }
    };
    callbacks.uiupdate = [&](const gui_input& input) {
      auto camera = scene.cameras[params.camera];
      if (uiupdate_camera_params(input, camera)) {
        stop_render();
        scene.cameras[params.camera] = camera;
        reset_display();
      }
    };

    // run ui
    show_gui_window({1280 + 320, 720}, title, callbacks);

    // done
    stop_render();
  }

}  // namespace yocto

// -----------------------------------------------------------------------------
// OPENGL HELPERS
// -----------------------------------------------------------------------------
namespace yocto {

  // assert on error
  [[maybe_unused]] static GLenum _assert_ogl_error() {
    auto error_code = glGetError();
    if (error_code != GL_NO_ERROR) {
      auto error = string{};
      switch (error_code) {
        case GL_INVALID_ENUM: error = "INVALID_ENUM"; break;
        case GL_INVALID_VALUE: error = "INVALID_VALUE"; break;
        case GL_INVALID_OPERATION: error = "INVALID_OPERATION"; break;
        // case GL_STACK_OVERFLOW: error = "STACK_OVERFLOW"; break;
        // case GL_STACK_UNDERFLOW: error = "STACK_UNDERFLOW"; break;
        case GL_OUT_OF_MEMORY: error = "OUT_OF_MEMORY"; break;
        case GL_INVALID_FRAMEBUFFER_OPERATION:
          error = "INVALID_FRAMEBUFFER_OPERATION";
          break;
      }
      printf("\n    OPENGL ERROR: %s\n\n", error.c_str());
    }
    return error_code;
  }
  static void assert_glerror() { assert(_assert_ogl_error() == GL_NO_ERROR); }

  // initialize program
  static void set_program(uint& program_id, uint& vertex_id, uint& fragment_id,
      const string& vertex, const string& fragment) {
    // error
    auto program_error = [&](const char* message, const char* log) {
      if (program_id) glDeleteProgram(program_id);
      if (vertex_id) glDeleteShader(program_id);
      if (fragment_id) glDeleteShader(program_id);
      program_id  = 0;
      vertex_id   = 0;
      fragment_id = 0;
      printf("%s\n", message);
      printf("%s\n", log);
    };

    const char* ccvertex   = vertex.data();
    const char* ccfragment = fragment.data();
    auto        errflags   = 0;
    auto        errbuf     = array<char, 10000>{};

    assert_glerror();

    // create vertex
    vertex_id = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_id, 1, &ccvertex, NULL);
    glCompileShader(vertex_id);
    glGetShaderiv(vertex_id, GL_COMPILE_STATUS, &errflags);
    if (errflags == 0) {
      glGetShaderInfoLog(vertex_id, 10000, 0, errbuf.data());
      return program_error("vertex shader not compiled", errbuf.data());
    }
    assert_glerror();

    // create fragment
    fragment_id = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_id, 1, &ccfragment, NULL);
    glCompileShader(fragment_id);
    glGetShaderiv(fragment_id, GL_COMPILE_STATUS, &errflags);
    if (errflags == 0) {
      glGetShaderInfoLog(fragment_id, 10000, 0, errbuf.data());
      return program_error("fragment shader not compiled", errbuf.data());
    }
    assert_glerror();

    // create program
    program_id = glCreateProgram();
    glAttachShader(program_id, vertex_id);
    glAttachShader(program_id, fragment_id);
    glLinkProgram(program_id);
    glGetProgramiv(program_id, GL_LINK_STATUS, &errflags);
    if (errflags == 0) {
      glGetProgramInfoLog(program_id, 10000, 0, errbuf.data());
      return program_error("program not linked", errbuf.data());
    }
// TODO(fabio): Apparently validation must be done just before drawing.
//    https://community.khronos.org/t/samplers-of-different-types-use-the-same-textur/66329
// If done here, validation fails when using cubemaps and textures in the
// same shader. We should create a function validate_program() anc call it
// separately.
#if 0
  glValidateProgram(program_id);
  glGetProgramiv(program_id, GL_VALIDATE_STATUS, &errflags);
  if (!errflags) {
    glGetProgramInfoLog(program_id, 10000, 0, errbuf.data());
    return program_error("program not validated", errbuf.data());
  }
  assert_glerror();
#endif
  }

}  // namespace yocto

// -----------------------------------------------------------------------------
// HIGH-LEVEL OPENGL IMAGE DRAWING
// -----------------------------------------------------------------------------
namespace yocto {

  static auto glimage_vertex =
      R"(
#version 330
in vec2 positions;
out vec2 frag_texcoord;
uniform vec2 window, image_size;
uniform vec2 image_center;
uniform float image_scale;
void main() {
    vec2 pos = (positions * 0.5) * image_size * image_scale + image_center;
    gl_Position = vec4(2 * pos.x / window.x - 1, 1 - 2 * pos.y / window.y, 0, 1);
    frag_texcoord = positions * 0.5 + 0.5;
}
)";
#if 0
static auto glimage_vertex = R"(
#version 330
in vec2 positions;
out vec2 frag_texcoord;
uniform vec2 window, image_size, border_size;
uniform vec2 image_center;
uniform float image_scale;
void main() {
    vec2 pos = (positions * 0.5) * (image_size + border_size*2) * image_scale + image_center;
    gl_Position = vec4(2 * pos.x / window.x - 1, 1 - 2 * pos.y / window.y, 0.1, 1);
    frag_texcoord = positions * 0.5 + 0.5;
}
)";
#endif
  static auto glimage_fragment =
      R"(
#version 330
in vec2 frag_texcoord;
out vec4 frag_color;
uniform sampler2D txt;
uniform vec4 background;
void main() {
  frag_color = texture(txt, frag_texcoord);
}
)";
#if 0
static auto glimage_fragment = R"(
#version 330
in vec2 frag_texcoord;
out vec4 frag_color;
uniform vec2 image_size, border_size;
uniform float image_scale;
void main() {
    ivec2 imcoord = ivec2(frag_texcoord * (image_size + border_size*2) - border_size);
    ivec2 tilecoord = ivec2(frag_texcoord * (image_size + border_size*2) * image_scale - border_size);
    ivec2 tile = tilecoord / 16;
    if(imcoord.x <= 0 || imcoord.y <= 0 || 
        imcoord.x >= image_size.x || imcoord.y >= image_size.y) frag_color = vec4(0,0,0,1);
    else if((tile.x + tile.y) % 2 == 0) frag_color = vec4(0.1,0.1,0.1,1);
    else frag_color = vec4(0.3,0.3,0.3,1);
}
)";
#endif

  // init image program
  static bool init_image(glimage_state& glimage) {
    // program
    set_program(glimage.program, glimage.vertex, glimage.fragment,
        glimage_vertex, glimage_fragment);

    // vertex arrays
    glGenVertexArrays(1, &glimage.vertexarray);
    glBindVertexArray(glimage.vertexarray);

    // buffers
    auto positions = vector<vec3f>{
        {-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}};
    glGenBuffers(1, &glimage.positions);
    glBindBuffer(GL_ARRAY_BUFFER, glimage.positions);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vec3f) * positions.size(),
        positions.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, false, 0, nullptr);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    auto triangles = vector<vec3i>{{0, 1, 3}, {3, 2, 1}};
    glGenBuffers(1, &glimage.triangles);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glimage.triangles);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(vec3i) * triangles.size(),
        triangles.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    // done
    // glBindVertexArray(0);
    return true;
  }

  // clear an opengl image
  static void clear_image(glimage_state& glimage) {
    if (glimage.texture) glDeleteTextures(1, &glimage.texture);
    if (glimage.program) glDeleteProgram(glimage.program);
    if (glimage.vertex) glDeleteProgram(glimage.vertex);
    if (glimage.fragment) glDeleteProgram(glimage.fragment);
    if (glimage.vertexarray) glDeleteVertexArrays(1, &glimage.vertexarray);
    if (glimage.positions) glDeleteBuffers(1, &glimage.positions);
    if (glimage.triangles) glDeleteBuffers(1, &glimage.triangles);
    glimage = {};
  }

  static void set_image(glimage_state& glimage, const image_data& image) {
    if (!glimage.texture || glimage.width != image.width ||
        glimage.height != image.height) {
      if (!glimage.texture) glGenTextures(1, &glimage.texture);
      glBindTexture(GL_TEXTURE_2D, glimage.texture);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0,
          GL_RGBA, GL_FLOAT, image.pixels.data());
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    } else {
      glBindTexture(GL_TEXTURE_2D, glimage.texture);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, image.width, image.height,
          GL_RGBA, GL_FLOAT, image.pixels.data());
    }
    glimage.width  = image.width;
    glimage.height = image.height;
  }

  // draw image
  static void draw_image(glimage_state& glimage, const glimage_params& params) {
    // check errors
    assert_glerror();

    // viewport and framebuffer
    glViewport(params.framebuffer.x, params.framebuffer.y, params.framebuffer.z,
        params.framebuffer.w);
    glClearColor(params.background.x, params.background.y, params.background.z,
        params.background.w);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    // blend
    glEnable(GL_BLEND);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);

    // bind program and params
    glUseProgram(glimage.program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, glimage.texture);
    glUniform1i(glGetUniformLocation(glimage.program, "txt"), 0);
    glUniform2f(glGetUniformLocation(glimage.program, "window"),
        (float)params.window.x, (float)params.window.y);
    glUniform2f(glGetUniformLocation(glimage.program, "image_size"),
        (float)glimage.width, (float)glimage.height);
    glUniform2f(glGetUniformLocation(glimage.program, "image_center"),
        params.center.x, params.center.y);
    glUniform1f(
        glGetUniformLocation(glimage.program, "image_scale"), params.scale);
    glUniform4f(glGetUniformLocation(glimage.program, "background"),
        params.background.x, params.background.y, params.background.z,
        params.background.w);
    assert_glerror();

    // draw
    glBindVertexArray(glimage.vertexarray);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glimage.triangles);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    assert_glerror();

    // unbind program
    glUseProgram(0);
    assert_glerror();

    // blend
    glDisable(GL_BLEND);
  }

}  // namespace yocto

// -----------------------------------------------------------------------------
// WINDOW
// -----------------------------------------------------------------------------
namespace yocto {

  // OpenGL window wrapper
  struct glwindow_state {
    string       title         = "";
    gui_callback init          = {};
    gui_callback clear         = {};
    gui_callback draw          = {};
    gui_callback widgets       = {};
    gui_callback update        = {};
    gui_callback uiupdate      = {};
    int          widgets_width = 0;
    bool         widgets_left  = true;
    gui_input    input         = {};
    vec2i        window        = {0, 0};
    vec4f        background    = {0.15f, 0.15f, 0.15f, 1.0f};
  };

  static void draw_window(glwindow_state& state) {
    glClearColor(state.background.x, state.background.y, state.background.z,
        state.background.w);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (state.draw) state.draw(state.input);
    if (state.widgets) {
      ImGui_ImplOpenGL3_NewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();
      auto window = state.window;
      if (state.widgets_left) {
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({(float)state.widgets_width, (float)window.y});
      } else {
        ImGui::SetNextWindowPos({(float)(window.x - state.widgets_width), 0});
        ImGui::SetNextWindowSize({(float)state.widgets_width, (float)window.y});
      }
      ImGui::SetNextWindowCollapsed(false);
      ImGui::SetNextWindowBgAlpha(1);
      if (ImGui::Begin(state.title.c_str(), nullptr,
              ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                  ImGuiWindowFlags_NoSavedSettings)) {
        state.widgets(state.input);
      }
      ImGui::End();
      ImGui::Render();
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
  }

  // run the user interface with the give callbacks
  void show_gui_window(const vec2i& size, const string& title,
      const gui_callbacks& callbacks, int widgets_width, bool widgets_left) {
    // init glfw
    if (!glfwInit())
      throw std::runtime_error("cannot initialize windowing system");
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    // create state
    auto state     = glwindow_state{};
    state.title    = title;
    state.init     = callbacks.init;
    state.clear    = callbacks.clear;
    state.draw     = callbacks.draw;
    state.widgets  = callbacks.widgets;
    state.update   = callbacks.update;
    state.uiupdate = callbacks.uiupdate;

    // create window
    auto window = glfwCreateWindow(
        size.x, size.y, title.c_str(), nullptr, nullptr);
    if (window == nullptr)
      throw std::runtime_error{"cannot initialize windowing system"};
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);  // Enable vsync

    // set user data
    glfwSetWindowUserPointer(window, &state);

    // set callbacks
    glfwSetWindowRefreshCallback(window, [](GLFWwindow* window) {
      auto& state = *(glwindow_state*)glfwGetWindowUserPointer(window);
      glfwGetWindowSize(window, &state.window.x, &state.window.y);
      draw_window(state);
      glfwSwapBuffers(window);
    });
    glfwSetWindowSizeCallback(window, [](GLFWwindow* window, int width,
                                          int height) {
      auto& state = *(glwindow_state*)glfwGetWindowUserPointer(window);
      glfwGetWindowSize(window, &state.input.window.x, &state.input.window.y);
      if (state.widgets_width) state.input.window.x -= state.widgets_width;
      glfwGetFramebufferSize(
          window, &state.input.framebuffer.z, &state.input.framebuffer.w);
      state.input.framebuffer.x = 0;
      state.input.framebuffer.y = 0;
      if (state.widgets_width) {
        auto win_size = zero2i;
        glfwGetWindowSize(window, &win_size.x, &win_size.y);
        auto offset = (int)(state.widgets_width *
                            (float)state.input.framebuffer.z / win_size.x);
        state.input.framebuffer.z -= offset;
        if (state.widgets_left) state.input.framebuffer.x += offset;
      }
    });

    // init gl extensions
    if (!gladLoadGL())
      throw std::runtime_error{"cannot initialize OpenGL extensions"};

    // widgets
    if (callbacks.widgets) {
      ImGui::CreateContext();
      ImGui::GetIO().IniFilename       = nullptr;
      ImGui::GetStyle().WindowRounding = 0;
      ImGui_ImplGlfw_InitForOpenGL(window, true);
#ifndef __APPLE__
      ImGui_ImplOpenGL3_Init();
#else
      ImGui_ImplOpenGL3_Init("#version 330");
#endif
      ImGui::StyleColorsDark();
      state.widgets_width = widgets_width;
      state.widgets_left  = widgets_left;
    }

    // init
    if (state.init) state.init(state.input);

    // run ui
    while (!glfwWindowShouldClose(window)) {
      // update input
      state.input.last = state.input.cursor;
      auto mouse_posx = 0.0, mouse_posy = 0.0;
      glfwGetCursorPos(window, &mouse_posx, &mouse_posy);
      state.input.cursor = vec2f{(float)mouse_posx, (float)mouse_posy};
      if (state.widgets_width && state.widgets_left)
        state.input.cursor.x -= state.widgets_width;
      state.input.mouse = {
          glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS ? 1
                                                                           : 0,
          glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS ? 1
                                                                            : 0,
          glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS
              ? 1
              : 0,
      };
      state.input.modifiers = {
          (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
              glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS)
              ? 1
              : 0,
          (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
              glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)
              ? 1
              : 0,
          (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
              glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS)
              ? 1
              : 0};
      glfwGetWindowSize(window, &state.input.window.x, &state.input.window.y);
      if (state.widgets_width) state.input.window.x -= state.widgets_width;
      glfwGetFramebufferSize(
          window, &state.input.framebuffer.z, &state.input.framebuffer.w);
      state.input.framebuffer.x = 0;
      state.input.framebuffer.y = 0;
      if (state.widgets_width) {
        auto win_size = zero2i;
        glfwGetWindowSize(window, &win_size.x, &win_size.y);
        auto offset = (int)(state.widgets_width *
                            (float)state.input.framebuffer.z / win_size.x);
        state.input.framebuffer.z -= offset;
        if (state.widgets_left) state.input.framebuffer.x += offset;
      }
      if (state.widgets_width) {
        auto io               = &ImGui::GetIO();
        state.input.onwidgets = io->WantTextInput || io->WantCaptureMouse ||
                                io->WantCaptureKeyboard;
      }

      // update ui
      if (state.uiupdate && !state.input.onwidgets) state.uiupdate(state.input);

      // update
      if (state.update) state.update(state.input);

      // draw
      glfwGetWindowSize(window, &state.window.x, &state.window.y);
      draw_window(state);
      glfwSwapBuffers(window);

      // event hadling
      glfwPollEvents();
    }

    // clear
    if (state.clear) state.clear(state.input);

    // clear
    glfwDestroyWindow(window);
    glfwTerminate();
  }  // namespace yocto

}  // namespace yocto

// -----------------------------------------------------------------------------
// OPENGL WIDGETS
// -----------------------------------------------------------------------------
namespace yocto {

  bool draw_gui_header(const char* lbl) {
    if (!ImGui::CollapsingHeader(lbl)) return false;
    ImGui::PushID(lbl);
    return true;
  }
  void end_gui_header() { ImGui::PopID(); }

  bool draw_gui_button(const char* lbl, bool enabled) {
    if (enabled) {
      return ImGui::Button(lbl);
    } else {
      ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
      ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
      auto ok = ImGui::Button(lbl);
      ImGui::PopItemFlag();
      ImGui::PopStyleVar();
      return ok;
    }
  }

  void draw_gui_label(const char* lbl, const string& label) {
    ImGui::LabelText(lbl, "%s", label.c_str());
  }
  void draw_gui_label(const char* lbl, int value) {
    ImGui::LabelText(lbl, "%s", std::to_string(value).c_str());
  }
  void draw_gui_label(const char* lbl, bool value) {
    ImGui::LabelText(lbl, "%s", value ? "true" : "false");
  }

  void draw_gui_separator() { ImGui::Separator(); }

  void continue_gui_line() { ImGui::SameLine(); }

  bool draw_gui_textinput(const char* lbl, string& value) {
    auto buffer = array<char, 4096>{};
    auto num    = 0;
    for (auto c : value) buffer[num++] = c;
    buffer[num] = 0;
    auto edited = ImGui::InputText(lbl, buffer.data(), buffer.size());
    if (edited) value = buffer.data();
    return edited;
  }

  bool draw_gui_slider(const char* lbl, float& value, float min, float max) {
    return ImGui::SliderFloat(lbl, &value, min, max);
  }
  bool draw_gui_slider(const char* lbl, vec2f& value, float min, float max) {
    return ImGui::SliderFloat2(lbl, &value.x, min, max);
  }
  bool draw_gui_slider(const char* lbl, vec3f& value, float min, float max) {
    return ImGui::SliderFloat3(lbl, &value.x, min, max);
  }
  bool draw_gui_slider(const char* lbl, vec4f& value, float min, float max) {
    return ImGui::SliderFloat4(lbl, &value.x, min, max);
  }
  bool draw_gui_slider(const char* lbl, int& value, int min, int max) {
    return ImGui::SliderInt(lbl, &value, min, max);
  }
  bool draw_gui_slider(const char* lbl, vec2i& value, int min, int max) {
    return ImGui::SliderInt2(lbl, &value.x, min, max);
  }
  bool draw_gui_slider(const char* lbl, vec3i& value, int min, int max) {
    return ImGui::SliderInt3(lbl, &value.x, min, max);
  }
  bool draw_gui_slider(const char* lbl, vec4i& value, int min, int max) {
    return ImGui::SliderInt4(lbl, &value.x, min, max);
  }

  bool draw_gui_dragger(
      const char* lbl, float& value, float speed, float min, float max) {
    return ImGui::DragFloat(lbl, &value, speed, min, max);
  }
  bool draw_gui_dragger(
      const char* lbl, vec2f& value, float speed, float min, float max) {
    return ImGui::DragFloat2(lbl, &value.x, speed, min, max);
  }
  bool draw_gui_dragger(
      const char* lbl, vec3f& value, float speed, float min, float max) {
    return ImGui::DragFloat3(lbl, &value.x, speed, min, max);
  }
  bool draw_gui_dragger(
      const char* lbl, vec4f& value, float speed, float min, float max) {
    return ImGui::DragFloat4(lbl, &value.x, speed, min, max);
  }
  bool draw_gui_dragger(
      const char* lbl, int& value, float speed, int min, int max) {
    return ImGui::DragInt(lbl, &value, speed, min, max);
  }
  bool draw_gui_dragger(
      const char* lbl, vec2i& value, float speed, int min, int max) {
    return ImGui::DragInt2(lbl, &value.x, speed, min, max);
  }
  bool draw_gui_dragger(
      const char* lbl, vec3i& value, float speed, int min, int max) {
    return ImGui::DragInt3(lbl, &value.x, speed, min, max);
  }
  bool draw_gui_dragger(
      const char* lbl, vec4i& value, float speed, int min, int max) {
    return ImGui::DragInt4(lbl, &value.x, speed, min, max);
  }

  bool draw_gui_dragger(const char* lbl, array<float, 2>& value, float speed,
      float min, float max) {
    return ImGui::DragFloat2(lbl, value.data(), speed, min, max);
  }
  bool draw_gui_dragger(const char* lbl, array<float, 3>& value, float speed,
      float min, float max) {
    return ImGui::DragFloat3(lbl, value.data(), speed, min, max);
  }
  bool draw_gui_dragger(const char* lbl, array<float, 4>& value, float speed,
      float min, float max) {
    return ImGui::DragFloat4(lbl, value.data(), speed, min, max);
  }

  bool draw_gui_dragger(
      const char* lbl, array<int, 2>& value, float speed, int min, int max) {
    return ImGui::DragInt2(lbl, value.data(), speed, min, max);
  }
  bool draw_gui_dragger(
      const char* lbl, array<int, 3>& value, float speed, int min, int max) {
    return ImGui::DragInt3(lbl, value.data(), speed, min, max);
  }
  bool draw_gui_dragger(
      const char* lbl, array<int, 4>& value, float speed, int min, int max) {
    return ImGui::DragInt4(lbl, value.data(), speed, min, max);
  }

  bool draw_gui_checkbox(const char* lbl, bool& value) {
    return ImGui::Checkbox(lbl, &value);
  }
  bool draw_gui_checkbox(const char* lbl, bool& value, bool invert) {
    if (!invert) {
      return draw_gui_checkbox(lbl, value);
    } else {
      auto inverted = !value;
      auto edited   = ImGui::Checkbox(lbl, &inverted);
      if (edited) value = !inverted;
      return edited;
    }
  }

  bool draw_gui_coloredit(const char* lbl, vec3f& value) {
    auto flags = ImGuiColorEditFlags_Float;
    return ImGui::ColorEdit3(lbl, &value.x, flags);
  }
  bool draw_gui_coloredit(const char* lbl, vec4f& value) {
    auto flags = ImGuiColorEditFlags_Float;
    return ImGui::ColorEdit4(lbl, &value.x, flags);
  }

  bool draw_gui_coloredithdr(const char* lbl, vec3f& value) {
    auto color    = value;
    auto exposure = 0.0f;
    auto scale    = max(color);
    if (scale > 1) {
      color /= scale;
      exposure = log2(scale);
    }
    auto edit_exposure = draw_gui_slider(
        (string{lbl} + " [exp]").c_str(), exposure, 0, 10);
    auto edit_color = draw_gui_coloredit(
        (string{lbl} + " [col]").c_str(), color);
    if (edit_exposure || edit_color) {
      value = color * exp2(exposure);
      return true;
    } else {
      return false;
    }
  }
  bool draw_gui_coloredithdr(const char* lbl, vec4f& value) {
    auto color    = value;
    auto exposure = 0.0f;
    auto scale    = max(xyz(color));
    if (scale > 1) {
      color.x /= scale;
      color.y /= scale;
      color.z /= scale;
      exposure = log2(scale);
    }
    auto edit_exposure = draw_gui_slider(
        (string{lbl} + " [exp]").c_str(), exposure, 0, 10);
    auto edit_color = draw_gui_coloredit(
        (string{lbl} + " [col]").c_str(), color);
    if (edit_exposure || edit_color) {
      value.x = color.x * exp2(exposure);
      value.y = color.y * exp2(exposure);
      value.z = color.z * exp2(exposure);
      value.w = color.w;
      return true;
    } else {
      return false;
    }
  }

  bool draw_gui_coloredit(const char* lbl, vec4b& value) {
    auto valuef = byte_to_float(value);
    if (ImGui::ColorEdit4(lbl, &valuef.x)) {
      value = float_to_byte(valuef);
      return true;
    } else {
      return false;
    }
  }

  bool draw_gui_combobox(const char* lbl, int& value,
      const vector<string>& labels, bool include_null) {
    if (!ImGui::BeginCombo(
            lbl, value >= 0 ? labels.at(value).c_str() : "<none>"))
      return false;
    auto old_val = value;
    if (include_null) {
      ImGui::PushID(100000);
      if (ImGui::Selectable("<none>", value < 0)) value = -1;
      if (value < 0) ImGui::SetItemDefaultFocus();
      ImGui::PopID();
    }
    for (auto i = 0; i < labels.size(); i++) {
      ImGui::PushID(i);
      if (ImGui::Selectable(labels[i].c_str(), value == i)) value = i;
      if (value == i) ImGui::SetItemDefaultFocus();
      ImGui::PopID();
    }
    ImGui::EndCombo();
    return value != old_val;
  }

  bool draw_gui_combobox(const char* lbl, string& value,
      const vector<string>& labels, bool include_null) {
    if (!ImGui::BeginCombo(lbl, value.c_str())) return false;
    auto old_val = value;
    if (include_null) {
      ImGui::PushID(100000);
      if (ImGui::Selectable("<none>", value.empty())) value = "";
      if (value.empty()) ImGui::SetItemDefaultFocus();
      ImGui::PopID();
    }
    for (auto i = 0; i < labels.size(); i++) {
      ImGui::PushID(i);
      if (ImGui::Selectable(labels[i].c_str(), value == labels[i]))
        value = labels[i];
      if (value == labels[i]) ImGui::SetItemDefaultFocus();
      ImGui::PopID();
    }
    ImGui::EndCombo();
    return value != old_val;
  }

  bool draw_gui_combobox(const char* lbl, int& idx, int num,
      const function<string(int)>& labels, bool include_null) {
    if (num <= 0) idx = -1;
    if (!ImGui::BeginCombo(lbl, idx >= 0 ? labels(idx).c_str() : "<none>"))
      return false;
    auto old_idx = idx;
    if (include_null) {
      ImGui::PushID(100000);
      if (ImGui::Selectable("<none>", idx < 0)) idx = -1;
      if (idx < 0) ImGui::SetItemDefaultFocus();
      ImGui::PopID();
    }
    for (auto i = 0; i < num; i++) {
      ImGui::PushID(i);
      if (ImGui::Selectable(labels(i).c_str(), idx == i)) idx = i;
      if (idx == i) ImGui::SetItemDefaultFocus();
      ImGui::PopID();
    }
    ImGui::EndCombo();
    return idx != old_idx;
  }

  void draw_gui_progressbar(const char* lbl, float fraction) {
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.5, 0.5, 1, 0.25));
    ImGui::ProgressBar(fraction, ImVec2(0.0f, 0.0f));
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::Text(lbl, ImVec2(0.0f, 0.0f));
    ImGui::PopStyleColor(1);
  }

  void draw_gui_progressbar(const char* lbl, int current, int total) {
    auto overlay = std::to_string(current) + "/" + std::to_string(total);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.5, 0.5, 1, 0.25));
    ImGui::ProgressBar(
        (float)current / (float)total, ImVec2(0.0f, 0.0f), overlay.c_str());
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::Text(lbl, ImVec2(0.0f, 0.0f));
    ImGui::PopStyleColor(1);
  }

  void draw_histogram(const char* lbl, const float* values, int count) {
    ImGui::PlotHistogram(lbl, values, count);
  }
  void draw_histogram(const char* lbl, const vector<float>& values) {
    ImGui::PlotHistogram(lbl, values.data(), (int)values.size(), 0, nullptr,
        flt_max, flt_max, {0, 0}, 4);
  }
  void draw_histogram(const char* lbl, const vector<vec2f>& values) {
    ImGui::PlotHistogram((string{lbl} + " x").c_str(),
        (const float*)values.data() + 0, (int)values.size(), 0, nullptr,
        flt_max, flt_max, {0, 0}, sizeof(vec2f));
    ImGui::PlotHistogram((string{lbl} + " y").c_str(),
        (const float*)values.data() + 1, (int)values.size(), 0, nullptr,
        flt_max, flt_max, {0, 0}, sizeof(vec2f));
  }
  void draw_histogram(const char* lbl, const vector<vec3f>& values) {
    ImGui::PlotHistogram((string{lbl} + " x").c_str(),
        (const float*)values.data() + 0, (int)values.size(), 0, nullptr,
        flt_max, flt_max, {0, 0}, sizeof(vec3f));
    ImGui::PlotHistogram((string{lbl} + " y").c_str(),
        (const float*)values.data() + 1, (int)values.size(), 0, nullptr,
        flt_max, flt_max, {0, 0}, sizeof(vec3f));
    ImGui::PlotHistogram((string{lbl} + " z").c_str(),
        (const float*)values.data() + 2, (int)values.size(), 0, nullptr,
        flt_max, flt_max, {0, 0}, sizeof(vec3f));
  }
  void draw_histogram(const char* lbl, const vector<vec4f>& values) {
    ImGui::PlotHistogram((string{lbl} + " x").c_str(),
        (const float*)values.data() + 0, (int)values.size(), 0, nullptr,
        flt_max, flt_max, {0, 0}, sizeof(vec4f));
    ImGui::PlotHistogram((string{lbl} + " y").c_str(),
        (const float*)values.data() + 1, (int)values.size(), 0, nullptr,
        flt_max, flt_max, {0, 0}, sizeof(vec4f));
    ImGui::PlotHistogram((string{lbl} + " z").c_str(),
        (const float*)values.data() + 2, (int)values.size(), 0, nullptr,
        flt_max, flt_max, {0, 0}, sizeof(vec4f));
    ImGui::PlotHistogram((string{lbl} + " w").c_str(),
        (const float*)values.data() + 3, (int)values.size(), 0, nullptr,
        flt_max, flt_max, {0, 0}, sizeof(vec4f));
  }

}  // namespace yocto

// -----------------------------------------------------------------------------
// OPENGL WIDGETS
// -----------------------------------------------------------------------------
namespace yocto {

  enum struct glwidgets_param_type {
    // clang-format off
  value1f, value2f, value3f, value4f, 
  value1i, value2i, value3i, value4i, 
  value1s, value1b
    // clang-format on
  };

  struct glwidgets_param {
    // constructors
    glwidgets_param()
        : type{glwidgets_param_type::value1f}
        , valuef{0, 0, 0, 0}
        , minmaxf{0, 0}
        , readonly{true} {}
    glwidgets_param(
        float value, const vec2f& minmax = {0, 0}, bool readonly = false)
        : type{glwidgets_param_type::value1f}
        , valuef{value, 0, 0, 0}
        , minmaxf{minmax}
        , readonly{readonly} {}
    glwidgets_param(
        vec2f value, const vec2f& minmax = {0, 0}, bool readonly = false)
        : type{glwidgets_param_type::value2f}
        , valuef{value.x, value.y, 0, 0}
        , minmaxf{minmax}
        , readonly{readonly} {}
    glwidgets_param(
        vec3f value, const vec2f& minmax = {0, 0}, bool readonly = false)
        : type{glwidgets_param_type::value3f}
        , valuef{value.x, value.y, value.z}
        , minmaxf{minmax}
        , readonly{readonly} {}
    glwidgets_param(
        vec4f value, const vec2f& minmax = {0, 0}, bool readonly = false)
        : type{glwidgets_param_type::value4f}
        , valuef{value.x, value.y, value.z, value.w}
        , minmaxf{minmax}
        , readonly{readonly} {}
    glwidgets_param(vec3f value, bool color, bool readonly = false)
        : type{glwidgets_param_type::value3f}
        , valuef{value.x, value.y, value.z, 1}
        , color{color}
        , readonly{readonly} {}
    glwidgets_param(vec4f value, bool color, bool readonly = false)
        : type{glwidgets_param_type::value4f}
        , valuef{value.x, value.y, value.z, value.w}
        , color{color}
        , readonly{readonly} {}
    glwidgets_param(
        int value, const vec2i& minmax = {0, 0}, bool readonly = false)
        : type{glwidgets_param_type::value1i}
        , valuei{value, 0, 0, 0}
        , minmaxi{minmax}
        , readonly{readonly} {}
    glwidgets_param(
        vec2i value, const vec2i& minmax = {0, 0}, bool readonly = false)
        : type{glwidgets_param_type::value2i}
        , valuei{value.x, value.y, 0, 0}
        , minmaxi{minmax}
        , readonly{readonly} {}
    glwidgets_param(
        vec3i value, const vec2i& minmax = {0, 0}, bool readonly = false)
        : type{glwidgets_param_type::value3i}
        , valuei{value.x, value.y, value.z, 0}
        , minmaxi{minmax}
        , readonly{readonly} {}
    glwidgets_param(
        vec4i value, const vec2i& minmax = {0, 0}, bool readonly = false)
        : type{glwidgets_param_type::value4i}
        , valuei{value.x, value.y, value.z, value.w}
        , minmaxi{minmax}
        , readonly{readonly} {}
    glwidgets_param(bool value, bool readonly = false)
        : type{glwidgets_param_type::value1b}
        , valueb{value}
        , readonly{readonly} {}
    glwidgets_param(const string& value, bool readonly = false)
        : type{glwidgets_param_type::value1s}
        , values{value}
        , readonly{readonly} {}
    glwidgets_param(const string& value, const vector<string>& labels,
        bool readonly = false)
        : type{glwidgets_param_type::value1s}
        , values{value}
        , labels{labels}
        , readonly{readonly} {}
    glwidgets_param(
        int value, const vector<string>& labels, bool readonly = false)
        : type{glwidgets_param_type::value1i}
        , valuei{value, 0, 0, 0}
        , labels{labels}
        , readonly{readonly} {}
    template <typename T, typename = std::enable_if_t<std::is_enum_v<T>>>
    glwidgets_param(
        T value, const vector<string>& labels, bool readonly = false)
        : type{glwidgets_param_type::value1i}
        , valuei{(int)value, 0, 0, 0}
        , labels{labels}
        , readonly{readonly} {}

    // conversions
    operator float() const {
      check_type(glwidgets_param_type::value1f);
      return valuef.x;
    }
    operator vec2f() const {
      check_type(glwidgets_param_type::value2f);
      return {valuef.x, valuef.y};
    }
    operator vec3f() const {
      check_type(glwidgets_param_type::value3f);
      return {valuef.x, valuef.y, valuef.z};
    }
    operator vec4f() const {
      check_type(glwidgets_param_type::value4f);
      return {valuef.x, valuef.y, valuef.z, valuef.w};
    }
    operator int() const {
      check_type(glwidgets_param_type::value1i);
      return valuei.x;
    }
    operator vec2i() const {
      check_type(glwidgets_param_type::value2i);
      return {valuei.x, valuei.y};
    }
    operator vec3i() const {
      check_type(glwidgets_param_type::value3i);
      return {valuei.x, valuei.y, valuei.z};
    }
    operator vec4i() const {
      check_type(glwidgets_param_type::value4i);
      return {valuei.x, valuei.y, valuei.z, valuei.w};
    }
    operator bool() const {
      check_type(glwidgets_param_type::value1b);
      return valueb;
    }
    operator string() const {
      check_type(glwidgets_param_type::value1s);
      return values;
    }
    template <typename T, typename = std::enable_if_t<std::is_enum_v<T>>>
    operator T() const {
      check_type(glwidgets_param_type::value1i);
      return (T)valuei.x;
    }

    // type checking
    void check_type(glwidgets_param_type type) const {
      if (type != this->type) throw std::invalid_argument{"bad gui type"};
    }

    // value
    glwidgets_param_type type   = glwidgets_param_type::value1f;
    vec4f                valuef = {0, 0, 0, 0};
    vec4i                valuei = {0, 0, 0, 0};
    bool                 valueb = false;
    string               values = "";

    // display properties
    vec2f          minmaxf  = {0, 0};
    vec2i          minmaxi  = {0, 0};
    bool           color    = false;
    vector<string> labels   = {};
    bool           readonly = false;
  };

  struct glwidgets_params {
    using container      = vector<pair<string, glwidgets_param>>;
    using iterator       = container::iterator;
    using const_iterator = container::const_iterator;

    glwidgets_params() {}

    bool   empty() const { return items.empty(); }
    size_t size() const { return items.size(); }

    glwidgets_param& operator[](const string& key) {
      auto item = find(key);
      if (item == end())
        return items.emplace_back(key, glwidgets_param{}).second;
      return item->second;
    }
    const glwidgets_param& operator[](const string& key) const {
      return at(key);
    }

    glwidgets_param& at(const string& key) {
      auto item = find(key);
      if (item == end()) throw std::out_of_range{"key not found " + key};
      return item->second;
    }
    const glwidgets_param& at(const string& key) const {
      auto item = find(key);
      if (item == end()) throw std::out_of_range{"key not found " + key};
      return item->second;
    }

    iterator find(const string& key) {
      for (auto iterator = items.begin(); iterator != items.end(); ++iterator) {
        if (iterator->first == key) return iterator;
      }
      return items.end();
    }
    const_iterator find(const string& key) const {
      for (auto iterator = items.begin(); iterator != items.end(); ++iterator) {
        if (iterator->first == key) return iterator;
      }
      return items.end();
    }

    iterator       begin() { return items.begin(); }
    iterator       end() { return items.end(); }
    const_iterator begin() const { return items.begin(); }
    const_iterator end() const { return items.end(); }

   private:
    vector<pair<string, glwidgets_param>> items;
  };

  // draw param
  bool draw_gui_param(const string& name, glwidgets_param& param) {
    auto copy = param;
    switch (param.type) {
      case glwidgets_param_type::value1f:
        if (param.minmaxf.x == param.minmaxf.y) {
          return draw_gui_dragger(name.c_str(), param.readonly
                                                    ? (float&)copy.valuef
                                                    : (float&)param.valuef) &&
                 !param.readonly;
        } else {
          return draw_gui_slider(name.c_str(),
                     param.readonly ? (float&)copy.valuef
                                    : (float&)param.valuef,
                     param.minmaxf.x, param.minmaxf.y) &&
                 !param.readonly;
        }
        break;
      case glwidgets_param_type::value2f:
        if (param.minmaxf.x == param.minmaxf.y) {
          return draw_gui_dragger(name.c_str(), param.readonly
                                                    ? (vec2f&)copy.valuef
                                                    : (vec2f&)param.valuef) &&
                 !param.readonly;
        } else {
          return draw_gui_slider(name.c_str(),
                     param.readonly ? (vec2f&)copy.valuef
                                    : (vec2f&)param.valuef,
                     param.minmaxf.x, param.minmaxf.y) &&
                 !param.readonly;
        }
        break;
      case glwidgets_param_type::value3f:
        if (param.color) {
          return draw_gui_coloredit(name.c_str(), param.readonly
                                                      ? (vec3f&)copy.valuef
                                                      : (vec3f&)param.valuef) &&
                 !param.readonly;
        } else if (param.minmaxf.x == param.minmaxf.y) {
          return draw_gui_dragger(name.c_str(), param.readonly
                                                    ? (vec3f&)copy.valuef
                                                    : (vec3f&)param.valuef) &&
                 !param.readonly;
        } else {
          return draw_gui_slider(name.c_str(),
                     param.readonly ? copy.valuef : param.valuef,
                     param.minmaxf.x, param.minmaxf.y) &&
                 !param.readonly;
        }
        break;
      case glwidgets_param_type::value4f:
        if (param.color) {
          return draw_gui_coloredit(name.c_str(), param.readonly
                                                      ? (vec4f&)copy.valuef
                                                      : (vec4f&)param.valuef) &&
                 !param.readonly;
        } else if (param.minmaxf.x == param.minmaxf.y) {
          return draw_gui_dragger(name.c_str(), param.readonly
                                                    ? (vec4f&)copy.valuef
                                                    : (vec4f&)param.valuef) &&
                 !param.readonly;
        } else {
          return draw_gui_slider(name.c_str(),
                     param.readonly ? (vec4f&)copy.valuef
                                    : (vec4f&)param.valuef,
                     param.minmaxf.x, param.minmaxf.y) &&
                 !param.readonly;
        }
        break;
      case glwidgets_param_type::value1i:
        if (!param.labels.empty()) {
          return draw_gui_combobox(name.c_str(),
                     param.readonly ? (int&)copy.valuei : (int&)param.valuei,
                     param.labels) &&
                 !param.readonly;
        } else if (param.minmaxi.x == param.minmaxi.y) {
          return draw_gui_dragger(name.c_str(),
                     param.readonly ? (int&)copy.valuei : (int&)param.valuei) &&
                 !param.readonly;
        } else {
          return draw_gui_slider(name.c_str(),
                     param.readonly ? (int&)copy.valuei : (int&)param.valuei,
                     param.minmaxi.x, param.minmaxi.y) &&
                 !param.readonly;
        }
        break;
      case glwidgets_param_type::value2i:
        if (param.minmaxi.x == param.minmaxi.y) {
          return draw_gui_dragger(name.c_str(), param.readonly
                                                    ? (vec2i&)copy.valuei
                                                    : (vec2i&)param.valuei) &&
                 !param.readonly;
        } else {
          return draw_gui_slider(name.c_str(),
                     param.readonly ? (vec2i&)copy.valuei
                                    : (vec2i&)param.valuei,
                     param.minmaxi.x, param.minmaxi.y) &&
                 !param.readonly;
        }
        break;
      case glwidgets_param_type::value3i:
        if (param.minmaxi.x == param.minmaxi.y) {
          return draw_gui_dragger(name.c_str(), param.readonly
                                                    ? (vec3i&)copy.valuei
                                                    : (vec3i&)param.valuei) &&
                 !param.readonly;
        } else {
          return draw_gui_slider(name.c_str(),
                     param.readonly ? (vec3i&)copy.valuei
                                    : (vec3i&)param.valuei,
                     param.minmaxi.x, param.minmaxi.y) &&
                 !param.readonly;
        }
        break;
      case glwidgets_param_type::value4i:
        if (param.minmaxi.x == param.minmaxi.y) {
          return draw_gui_dragger(name.c_str(), param.readonly
                                                    ? (vec4i&)copy.valuei
                                                    : (vec4i&)param.valuei) &&
                 !param.readonly;
        } else {
          return draw_gui_slider(name.c_str(),
                     param.readonly ? (vec4i&)copy.valuei
                                    : (vec4i&)param.valuei,
                     param.minmaxi.x, param.minmaxi.y) &&
                 !param.readonly;
        }
        break;
      case glwidgets_param_type::value1s:
        if (!param.labels.empty()) {
          return draw_gui_combobox(name.c_str(),
                     param.readonly ? copy.values : param.values,
                     param.labels) &&
                 !param.readonly;
        } else {
          return draw_gui_textinput(name.c_str(),
                     param.readonly ? copy.values : param.values) &&
                 !param.readonly;
        }
        break;
      case glwidgets_param_type::value1b:
        if (!param.labels.empty()) {
          // maybe we should implement something different here
          return draw_gui_checkbox(name.c_str(),
                     param.readonly ? copy.valueb : param.valueb) &&
                 !param.readonly;
        } else {
          return draw_gui_checkbox(name.c_str(),
                     param.readonly ? copy.valueb : param.valueb) &&
                 !param.readonly;
        }
        break;
      default: return false;
    }
  }

  // draw params
  bool draw_gui_params(const string& name, glwidgets_params& params) {
    auto edited = false;
    if (draw_gui_header(name.c_str())) {
      for (auto& [name, param] : params) {
        auto pedited = draw_gui_param(name, param);
        edited       = edited || pedited;
      }
      end_gui_header();
    }
    return edited;
  }

}  // namespace yocto

#else

// -----------------------------------------------------------------------------
// NO OPENGL
// -----------------------------------------------------------------------------
namespace yocto {

  static void exit_nogl() {
    printf("opngl not linked\n");
    exit(1);
  }

  // Open a window and show an scene via path tracing
  void show_trace_gui(const string& title, const string& name,
      scene_data& scene, const trace_params& params, bool print, bool edit) {
    exit_nogl();
  }

}  // namespace yocto

// -----------------------------------------------------------------------------
// WINDOW
// -----------------------------------------------------------------------------
namespace yocto {

  // run the user interface with the give callbacks
  void show_gui_window(const vec2i& size, const string& title,
      const gui_callbacks& callbaks, int widgets_width, bool widgets_left) {
    exit_nogl();
  }

}  // namespace yocto

// -----------------------------------------------------------------------------
// WIDGETS
// -----------------------------------------------------------------------------
namespace yocto {

  // Headers
  bool draw_gui_header(const char* title) {
    exit_nogl();
    return false;
  }
  void end_gui_header() { exit_nogl(); }

  // Labels
  void draw_gui_label(const char* lbl, const string& text) { exit_nogl(); }
  void draw_gui_label(const char* lbl, int value) { exit_nogl(); }
  void draw_gui_label(const char* lbl, bool value) { exit_nogl(); }

  // Lines
  void draw_gui_separator() { exit_nogl(); }
  void continue_gui_line() { exit_nogl(); }

  // Buttons
  bool draw_gui_button(const char* lbl, bool enabled) {
    exit_nogl();
    return false;
  }

  // Text
  bool draw_gui_textinput(const char* lbl, string& value) {
    exit_nogl();
    return false;
  }

  // Slider
  bool draw_gui_slider(const char* lbl, float& value, float min, float max) {
    exit_nogl();
    return false;
  }
  bool draw_gui_slider(const char* lbl, vec2f& value, float min, float max) {
    exit_nogl();
    return false;
  }
  bool draw_gui_slider(const char* lbl, vec3f& value, float min, float max) {
    exit_nogl();
    return false;
  }
  bool draw_gui_slider(const char* lbl, vec4f& value, float min, float max) {
    exit_nogl();
    return false;
  }
  bool draw_gui_slider(const char* lbl, int& value, int min, int max) {
    exit_nogl();
    return false;
  }
  bool draw_gui_slider(const char* lbl, vec2i& value, int min, int max) {
    exit_nogl();
    return false;
  }
  bool draw_gui_slider(const char* lbl, vec3i& value, int min, int max) {
    exit_nogl();
    return false;
  }
  bool draw_gui_slider(const char* lbl, vec4i& value, int min, int max) {
    exit_nogl();
    return false;
  }

  // Dragger
  bool draw_gui_dragger(
      const char* lbl, float& value, float speed, float min, float max) {
    exit_nogl();
    return false;
  }
  bool draw_gui_dragger(
      const char* lbl, vec2f& value, float speed, float min, float max) {
    exit_nogl();
    return false;
  }
  bool draw_gui_dragger(
      const char* lbl, vec3f& value, float speed, float min, float max) {
    exit_nogl();
    return false;
  }
  bool draw_gui_dragger(
      const char* lbl, vec4f& value, float speed, float min, float max) {
    exit_nogl();
    return false;
  }
  bool draw_gui_dragger(
      const char* lbl, int& value, float speed, int min, int max) {
    exit_nogl();
    return false;
  }
  bool draw_gui_dragger(
      const char* lbl, vec2i& value, float speed, int min, int max) {
    exit_nogl();
    return false;
  }
  bool draw_gui_dragger(
      const char* lbl, vec3i& value, float speed, int min, int max) {
    exit_nogl();
    return false;
  }
  bool draw_gui_dragger(
      const char* lbl, vec4i& value, float speed, int min, int max) {
    exit_nogl();
    return false;
  }

  // Checkbox
  bool draw_gui_checkbox(const char* lbl, bool& value) {
    exit_nogl();
    return false;
    return false;
  }
  bool draw_gui_checkbox(const char* lbl, bool& value, bool invert) {
    exit_nogl();
    return false;
  }

  // Color editor
  bool draw_gui_coloredit(const char* lbl, vec3f& value) {
    exit_nogl();
    return false;
  }
  bool draw_gui_coloredit(const char* lbl, vec4f& value) {
    exit_nogl();
    return false;
  }
  bool draw_gui_coloredit(const char* lbl, vec4b& value) {
    exit_nogl();
    return false;
  }
  bool draw_gui_coloredithdr(const char* lbl, vec3f& value) {
    exit_nogl();
    return false;
  }
  bool draw_gui_coloredithdr(const char* lbl, vec4f& value) {
    exit_nogl();
    return false;
  }

  // Combo box
  bool draw_gui_combobox(const char* lbl, int& idx,
      const vector<string>& labels, bool include_null) {
    exit_nogl();
    return false;
  }
  bool draw_gui_combobox(const char* lbl, string& value,
      const vector<string>& labels, bool include_null) {
    exit_nogl();
    return false;
  }

  // Progress bar
  void draw_gui_progressbar(const char* lbl, float fraction) { exit_nogl(); }
  void draw_gui_progressbar(const char* lbl, int current, int total) {
    exit_nogl();
  }

}  // namespace yocto

#endif