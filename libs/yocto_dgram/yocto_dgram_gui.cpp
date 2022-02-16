//
// # Yocto/Dgram GUI: GUI viewer
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

// -----------------------------------------------------------------------------
// INCLUDES
// -----------------------------------------------------------------------------

#include "yocto_dgram_gui.h"

#include <yocto/yocto_gui.h>

#ifdef YOCTO_OPENGL

#include <glad/glad.h>

#include <cassert>
#include <cstdlib>
#include <future>
#include <stdexcept>

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
// USING DIRECTIVES
// -----------------------------------------------------------------------------
namespace yocto {

  // using directives

}  // namespace yocto

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
// VIEW
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

  static bool uiupdate_camera_params(
      const gui_input& input, dgram_camera& camera) {
    if (input.mouse.x && input.modifiers.x && !input.onwidgets) {
      auto dolly  = 0.0f;
      auto pan    = zero2f;
      auto rotate = zero2f;
      if (input.modifiers.y) {
        pan = (input.cursor - input.last) * distance(camera.from, camera.to) /
              200.0f;
      } else if (input.modifiers.z) {
        dolly = (input.cursor.y - input.last.y) / 100.0f;
      } else {
        rotate = (input.cursor - input.last) / 100.0f;
      }
      auto [from, to] = camera_turntable(
          camera.from, camera.to, vec3f{0, 1, 0}, rotate, dolly, pan);
      if (camera.from != from || camera.to != to) {
        camera.from = from;
        camera.to   = to;
        return true;
      }
    }
    return false;
  }

  struct scene_selection {
    int scene    = 0;
    int camera   = 0;
    int object   = 0;
    int shape    = 0;
    int labels   = 0;
    int label    = -1;
    int material = 0;
  };

  void show_dgram_gui(dgram_scenes& dgram, dgram_trace_params& params,
      bool transparent_background) {
    auto shapes_v = vector<trace_shapes>(dgram.scenes.size());
    auto texts_v  = vector<trace_texts>(dgram.scenes.size());
    auto bvh_v    = vector<dgram_scene_bvh>(dgram.scenes.size());
    auto state_v  = vector<dgram_trace_state>(dgram.scenes.size());

    auto needs_rendering = vector<bool>(dgram.scenes.size(), true);
    auto text_edited     = true;

    auto renders = vector<image_data>(
        dgram.scenes.size(), make_image(params.width, params.height, true));
    auto image   = make_image(params.width, params.height, true);
    auto display = make_image(params.width, params.height, false);

    // opengl image
    auto glimage  = glimage_state{};
    auto glparams = glimage_params{};

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

      for (auto idx = 0; idx < dgram.scenes.size(); idx++) {
        if (needs_rendering[idx]) {
          needs_rendering[idx] = false;

          auto& scene  = dgram.scenes[idx];
          auto& render = renders[idx];
          auto& shapes = shapes_v[idx];
          auto& bvh    = bvh_v[idx];
          auto& texts  = texts_v[idx];
          auto& state  = state_v[idx];

          shapes = make_shapes(scene, params.camera, params.size, params.scale,
               params.noparallel);
          bvh    = make_bvh(shapes, true, params.noparallel);
          texts  = trace_texts{};
          state  = make_state(params);

          render = make_image(params.width, params.height, true);

          render_worker = {};
          render_stop   = false;

          // preview
          auto pparams = params;
          auto pratio  = 8;
          pparams.width /= pratio;
          pparams.height /= pratio;
          pparams.samples = 1;
          auto pstate     = make_state(pparams);
          trace_samples(pstate, scene, shapes, texts, bvh, pparams);
          auto preview = get_render(pstate);
          for (auto idx = 0; idx < state.width * state.height; idx++) {
            auto i = idx % render.width, j = idx / render.width;
            auto pi            = clamp(i / pratio, 0, preview.width - 1),
                 pj            = clamp(j / pratio, 0, preview.height - 1);
            render.pixels[idx] = preview.pixels[pj * preview.width + pi];
          }
          {
            auto lock      = std::lock_guard{render_mutex};
            render_current = 0;
            render_update  = true;
          }

          // start renderer
          render_worker = std::async(std::launch::async, [&]() {
            // make texts
            texts = make_texts(scene, params.camera, params.size, params.scale,
                 params.width, params.height, params.noparallel, text_edited);

            for (auto sample = 0; sample < params.samples; sample++) {
              if (render_stop) return;
              parallel_for(state.width, state.height, [&](int i, int j) {
                if (render_stop) return;
                trace_sample(state, scene, shapes, texts, bvh, i, j, params);
               });
              state.samples++;
              if (!render_stop) {
                auto lock      = std::lock_guard{render_mutex};
                render_current = state.samples;
                get_render(render, state);
                render_update = true;
              }
            }
          });
        }
      }
    };

    // stop render
    auto stop_render = [&]() {
      render_stop = true;
      if (render_worker.valid()) render_worker.get();
    };

    // start rendering
    reset_display();

    auto tparams   = params;
    auto selection = scene_selection{};

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

        image   = make_image(params.width, params.height, true);
        display = make_image(params.width, params.height, false);

        if (!transparent_background)
          image.pixels = vector<vec4f>(
              params.width * params.height, vec4f{1, 1, 1, 1});
        for (auto& render : renders) {
          auto scaled = make_image(params.width, params.height, true);
          auto ratio  = params.width / render.width;
          for (auto idx = 0; idx < params.width * params.height; idx++) {
            auto i = idx % scaled.width, j = idx / scaled.width;
            auto pi            = clamp(i / ratio, 0, render.width - 1),
                 pj            = clamp(j / ratio, 0, render.height - 1);
            scaled.pixels[idx] = render.pixels[pj * render.width + pi];
          }
          image = composite_image(scaled, image);
        }
        tonemap_image_mt(display, image, 0);

        set_image(glimage, display);
        render_update = false;
      }
      update_image_params(input, image, glparams);
      draw_image(glimage, glparams);
    };
    callbacks.widgets = [&](const gui_input& input) {
      auto one_edited = 0;
      auto all_edited = 0;
      text_edited     = false;

      auto current = (int)render_current;
      draw_gui_progressbar("sample", current, params.samples);

      if (draw_gui_header("render")) {
        /*one_edited += draw_gui_combobox("camera", tparams.camera, "camera",
            (int)dgram.scenes[selection.scene].cameras.size());*/

        if (draw_gui_slider("resolution", tparams.width, 180, 3840)) {
          tparams.height = (int)round(
              (float)tparams.width * params.size.y / params.size.x);
        }
        if (ImGui::IsItemDeactivated()) {
          text_edited = true;
          all_edited++;
        }

        draw_gui_slider("samples", tparams.samples, 1, 100);
        all_edited += ImGui::IsItemDeactivated();

        all_edited += draw_gui_combobox(
            "antialiasing", (int&)tparams.antialiasing, antialiasing_names);

        all_edited += draw_gui_combobox(
            "sampler", (int&)tparams.sampler, dgram_sampler_names);

        all_edited += draw_gui_checkbox(
            "transparent background", transparent_background);

        end_gui_header();
      }

      if (draw_gui_header("dgram")) {
        if (draw_gui_dragger("size", dgram.size)) {
          tparams.size   = dgram.size;
          tparams.height = (int)round(
              (float)tparams.width * dgram.size.y / dgram.size.x);
        }
        if (ImGui::IsItemDeactivated()) {
          text_edited = true;
          all_edited++;
        }
        if (draw_gui_slider("scale", dgram.scale, 0.1f, 1000.0f))
          tparams.scale = dgram.scale;
        if (ImGui::IsItemDeactivated()) {
          text_edited = true;
          all_edited++;
        }

        end_gui_header();
      }

      if (draw_gui_header("scenes")) {
        auto selected_scene = selection.scene;
        if (draw_gui_combobox(
                "scene", selected_scene, "scene", (int)dgram.scenes.size())) {
          selection       = scene_selection{};
          selection.scene = selected_scene;
        }

        draw_gui_dragger("offset", dgram.scenes[selection.scene].offset, 0.01f);
        one_edited += ImGui::IsItemDeactivated();

        end_gui_header();
      }

      if (draw_gui_header("cameras")) {
        draw_gui_combobox("camera", selection.camera, "camera",
            (int)dgram.scenes[selection.scene].cameras.size());
        auto& camera = dgram.scenes[selection.scene].cameras.at(
            selection.camera);

        one_edited += draw_gui_checkbox("ortho", camera.orthographic);

        draw_gui_dragger("center", camera.center, 0.01f);
        one_edited += ImGui::IsItemDeactivated();

        draw_gui_dragger("from", camera.from, 0.05f);
        one_edited += ImGui::IsItemDeactivated();

        draw_gui_dragger("to", camera.to, 0.05f);
        one_edited += ImGui::IsItemDeactivated();

        draw_gui_slider("lens", camera.lens, 0.001f, 1);
        one_edited += ImGui::IsItemDeactivated();

        draw_gui_slider("film", camera.film, 0.001f, 0.5f);
        one_edited += ImGui::IsItemDeactivated();

        end_gui_header();
      }

      if (draw_gui_header("objects")) {
        draw_gui_combobox("object", selection.object, "object",
            (int)dgram.scenes[selection.scene].objects.size());
        auto& object = dgram.scenes[selection.scene].objects.at(
            selection.object);

        one_edited += draw_gui_combobox("shape", object.shape, "shape",
            (int)dgram.scenes[selection.scene].shapes.size());

        one_edited += draw_gui_combobox("material", object.material, "material",
            (int)dgram.scenes[selection.scene].materials.size());

        one_edited += draw_gui_combobox("labels", object.labels, "labels",
            (int)dgram.scenes[selection.scene].labels.size());

        end_gui_header();
      }

      if (draw_gui_header("materials")) {
        draw_gui_combobox("material", selection.material, "material",
            (int)dgram.scenes[selection.scene].materials.size());
        auto& material = dgram.scenes[selection.scene].materials.at(
            selection.material);

        draw_gui_coloredit("fill", material.fill);
        one_edited += ImGui::IsItemDeactivated();

        draw_gui_coloredit("stroke", material.stroke);
        one_edited += ImGui::IsItemDeactivated();

        draw_gui_slider("thickness", material.thickness, 0.0f, 100.0f);
        one_edited += ImGui::IsItemDeactivated();

        draw_gui_slider("dash_period", material.dash_period, 0.0f, 100.0f);
        one_edited += ImGui::IsItemDeactivated();

        draw_gui_slider("dash_phase", material.dash_phase, 0.0f, 100.0f);
        one_edited += ImGui::IsItemDeactivated();

        draw_gui_slider("dash_on", material.dash_on, 0.0f, 100.0f);
        one_edited += ImGui::IsItemDeactivated();

        one_edited += draw_gui_combobox(
            "dash_cap", (int&)material.dash_cap, dash_cap_type_names);

        one_edited += draw_gui_combobox(
            "dashed", (int&)material.dashed, dashed_line_names);

        end_gui_header();
      }

      if (draw_gui_header("shapes")) {
        draw_gui_combobox("shape", selection.shape, "shape",
            (int)dgram.scenes[selection.scene].shapes.size());
        auto& shape = dgram.scenes[selection.scene].shapes.at(selection.shape);

        draw_gui_label("positions", (int)shape.positions.size());
        draw_gui_label("points", (int)shape.points.size());
        draw_gui_label("lines", (int)shape.lines.size());
        draw_gui_label("triangles", (int)shape.triangles.size());
        draw_gui_label("quads", (int)shape.quads.size());
        draw_gui_label("fills", (int)shape.fills.size());
        draw_gui_label("line ends", (int)shape.ends.size());

        one_edited += draw_gui_checkbox("cull", shape.cull);
        one_edited += draw_gui_checkbox("boundary", shape.boundary);

        end_gui_header();
      }

      if (draw_gui_header("labels")) {
        if (draw_gui_combobox("labels", selection.labels, "labels",
                (int)dgram.scenes[selection.scene].labels.size()))
          selection.label = -1;
        auto& labels = dgram.scenes[selection.scene].labels.at(
            selection.labels);

        draw_gui_combobox("label", selection.label, "label",
            (int)labels.positions.size(), true);

        if (selection.label != -1) {
          draw_gui_dragger(
              "position", labels.positions[selection.label], 0.01f);
          one_edited += ImGui::IsItemDeactivated();

          draw_gui_dragger("offset", labels.offsets[selection.label], 1.0f);
          one_edited += ImGui::IsItemDeactivated();

          draw_gui_textinput("text", labels.texts[selection.label]);
          if (ImGui::IsItemDeactivated()) {
            text_edited = true;
            one_edited++;
          }

          auto alignments = vector<string>{"left", "center", "right"};
          auto idx        = 1;
          if (labels.alignments[selection.label].x > 0)
            idx = 0;
          else if (labels.alignments[selection.label].x < 0)
            idx = 2;

          if (draw_gui_combobox("alignment", idx, alignments)) {
            text_edited = true;
            one_edited++;
          }

          if (idx == 0)
            labels.alignments[selection.label].x = 1.0f;
          else if (idx == 1)
            labels.alignments[selection.label].x = 0.0f;
          else
            labels.alignments[selection.label].x = -1.0f;
        }

        end_gui_header();
      }

      if (one_edited) {
        stop_render();
        params                           = tparams;
        needs_rendering[selection.scene] = true;
        reset_display();
      }
      if (all_edited) {
        stop_render();
        params          = tparams;
        needs_rendering = vector<bool>(dgram.scenes.size(), true);
        reset_display();
      }
      draw_image_inspector(input, image, display, glparams);
    };
    callbacks.uiupdate = [&](const gui_input& input) {
      auto camera = dgram.scenes[selection.scene].cameras[params.camera];
      if (uiupdate_camera_params(input, camera)) {
        stop_render();
        dgram.scenes[selection.scene].cameras[params.camera] = camera;
        needs_rendering[selection.scene]                     = true;
        reset_display();
      }
    };

    show_gui_window({1280 + 320, 720}, "dgram", callbacks);
  }

}  // namespace yocto

#endif