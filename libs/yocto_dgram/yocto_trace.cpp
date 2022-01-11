//
// Implementation for Yocto/Trace.
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

#include "yocto_trace.h"

#include <algorithm>
#include <cstring>
#include <future>
#include <memory>
#include <stdexcept>
#include <utility>

#include "yocto_color.h"
#include "yocto_geometry.h"
#include "yocto_sampling.h"
#include "yocto_shading.h"

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
// IMPLEMENTATION OF RAY-SCENE INTERSECTION
// -----------------------------------------------------------------------------
namespace yocto {

  // Build the bvh acceleration structure.
  scene_bvh make_bvh(const scene_data& scene, const trace_params& params) {
    return make_bvh(scene, params.highqualitybvh, params.noparallel);
  }

}  // namespace yocto

// -----------------------------------------------------------------------------
// IMPLEMENTATION FOR PATH TRACING
// -----------------------------------------------------------------------------
namespace yocto {

  // Convenience functions
  static material_point eval_material(
      const scene_data& scene, const scene_intersection& intersection) {
    return eval_material(scene, scene.instances[intersection.instance],
        intersection.element, intersection.uv);
  }
  static vec3f eval_position(
      const scene_data& scene, const scene_intersection& intersection) {
    auto instance = scene.instances[intersection.instance];
    return transform_point(instance.frame, intersection.position);
  }
  static vec3f eval_normal(
      const scene_data& scene, const scene_intersection& intersection) {
    auto instance = scene.instances[intersection.instance];
    return transform_normal(instance.frame, intersection.normal);
  }

  // Sample camera
  static ray3f sample_camera(const camera_data& camera, const vec2i& ij,
      const vec2i& image_size, const vec2f& puv, const vec2f& luv, bool tent) {
    if (!tent) {
      auto uv = vec2f{
          (ij.x + puv.x) / image_size.x, (ij.y + puv.y) / image_size.y};
      return eval_camera(camera, uv, sample_disk(luv));
    } else {
      const auto width  = 2.0f;
      const auto offset = 0.5f;
      auto       fuv =
          width *
              vec2f{
                  puv.x < 0.5f ? sqrt(2 * puv.x) - 1 : 1 - sqrt(2 - 2 * puv.x),
                  puv.y < 0.5f ? sqrt(2 * puv.y) - 1 : 1 - sqrt(2 - 2 * puv.y),
              } +
          offset;
      auto uv = vec2f{
          (ij.x + fuv.x) / image_size.x, (ij.y + fuv.y) / image_size.y};
      return eval_camera(camera, uv, sample_disk(luv));
    }
  }

  static vec4f trace_color(const scene_data& scene, const scene_bvh& bvh,
      const ray3f& ray, rng_state& rng, const trace_params& params) {
    auto intersection = intersect_scene(bvh, scene, ray, false);
    if (!intersection.hit)
      return params.transparent_background ? vec4f{0, 0, 0, 0}
                                           : scene.background_color;

    if (intersection.border) {
      auto back_isec     = intersect_scene(bvh, scene, ray, true);
      auto back_isec_old = back_isec;
      if (back_isec.instance == intersection.instance) {
        auto uv  = vec2f{-1e-2f, back_isec.uv.y};
        auto min = back_isec.uv.x;
        if ((1 - back_isec.uv.x) < min) {
          min = 1 - back_isec.uv.x;
          uv  = vec2f{1 + 1e-2f, back_isec.uv.y};
        }
        if (back_isec.uv.y < min) {
          min = back_isec.uv.y;
          uv  = vec2f{back_isec.uv.x, -1e-2f};
        }
        if ((1 - back_isec.uv.y) < min) {
          min = 1 - back_isec.uv.y;
          uv  = vec2f{back_isec.uv.x, 1 + 1e-2f};
        }
        auto  instance = scene.instances[intersection.instance];
        auto  element  = intersection.element;
        auto& shape    = scene.shapes[instance.shape];

        auto p = vec3f{0, 0, 0};

        if (!shape.triangles.empty()) {
          auto t = shape.triangles[element];
          p      = transform_point(instance.frame,
                   interpolate_triangle(shape.positions[t.x], shape.positions[t.y],
                       shape.positions[t.z], uv));
        } else if (!shape.quads.empty()) {
          auto q = shape.quads[element];
          p      = transform_point(instance.frame,
                   interpolate_quad(shape.positions[q.x], shape.positions[q.y],
                       shape.positions[q.z], shape.positions[q.w], uv));
        }
        auto dir  = normalize(p - ray.o);
        back_isec = intersect_scene(bvh, scene, {p - dir * 1e-2f, dir}, true);
      }
      if (back_isec.instance == intersection.instance)
        intersection = back_isec_old;
    }

    auto position = eval_position(scene, intersection);
    auto material = eval_material(scene, intersection);
    auto color    = intersection.border ? material.stroke : material.fill;

    if (color.w < 1) {
      auto back_color = trace_color(scene, bvh, {position, ray.d}, rng, params);
      return composite(color, back_color);
    }

    return color;
  }

  static vec4f trace_color_wireframe(const scene_data& scene,
      const scene_bvh& bvh, const ray3f& ray, rng_state& rng,
      const trace_params& params) {
    auto intersection = intersect_scene(bvh, scene, ray, false);
    if (!intersection.hit)
      return params.transparent_background ? vec4f{0, 0, 0, 0}
                                           : scene.background_color;

    auto position = eval_position(scene, intersection);
    auto material = eval_material(scene, intersection);
    auto color    = intersection.border ? material.stroke : material.fill;

    if (color.w < 1) {
      auto back_color = trace_color_wireframe(
          scene, bvh, {position, ray.d}, rng, params);
      return composite(color, back_color);
    }

    return color;
  }

  static vec4f trace_normal(const scene_data& scene, const scene_bvh& bvh,
      const ray3f& ray, rng_state& rng, const trace_params& params) {
    auto intersection = intersect_scene(bvh, scene, ray, false);
    if (!intersection.hit)
      return params.transparent_background ? vec4f{0, 0, 0, 0}
                                           : vec4f{1, 1, 1, 1};

    return rgb_to_rgba(eval_normal(scene, intersection));
  }

  // Eyelight for quick previewing.
  static vec4f trace_eyelight(const scene_data& scene, const scene_bvh& bvh,
      const ray3f& ray, rng_state& rng, const trace_params& params) {
    auto intersection = intersect_scene(bvh, scene, ray, false);
    if (!intersection.hit)
      return params.transparent_background ? vec4f{0, 0, 0, 0}
                                           : scene.background_color;

    auto normal   = eval_normal(scene, intersection);
    auto position = eval_position(scene, intersection);
    auto material = eval_material(scene, intersection);
    auto color    = intersection.border ? material.stroke : material.fill;

    auto rgb_color = rgba_to_rgb(color) * dot(normal, -ray.d);
    color.x        = rgb_color.x;
    color.y        = rgb_color.y;
    color.z        = rgb_color.z;

    if (color.w < 1) {
      auto back_color = trace_eyelight(
          scene, bvh, {position, ray.d}, rng, params);
      return composite(color, back_color);
    }

    return color;
  }

  // Trace a single ray from the camera using the given algorithm.
  using sampler_func = vec4f (*)(const scene_data& scene, const scene_bvh& bvh,
      const ray3f& ray, rng_state& rng, const trace_params& params);
  static sampler_func get_trace_sampler_func(const trace_params& params) {
    switch (params.sampler) {
      case trace_sampler_type::color: return trace_color;

      case trace_sampler_type::color_wireframe: return trace_color_wireframe;
      case trace_sampler_type::normal: return trace_normal;
      case trace_sampler_type::eyelight: return trace_eyelight;
      default: {
        throw std::runtime_error("sampler unknown");
        return nullptr;
      }
    }
  }

  // Trace a block of samples
  void trace_sample(trace_state& state, const scene_data& scene,
      const scene_bvh& bvh, int i, int j, const trace_params& params) {
    auto& camera   = scene.cameras[params.camera];
    auto  sampler  = get_trace_sampler_func(params);
    auto  idx      = state.width * j + i;
    auto  ray      = sample_camera(camera, {i, j}, {state.width, state.height},
              rand2f(state.rngs[idx]), rand2f(state.rngs[idx]), params.tentfilter);
    auto  radiance = sampler(scene, bvh, ray, state.rngs[idx], params);
    if (!isfinite(radiance)) radiance = {0, 0, 0};
    if (max(radiance) > params.clamp)
      radiance = radiance * (params.clamp / max(radiance));
    state.image[idx] += radiance;
    state.hits[idx] += 1;
  }

  // Init a sequence of random number generators.
  trace_state make_state(const scene_data& scene, const trace_params& params) {
    auto& camera = scene.cameras[params.camera];
    auto  state  = trace_state{};
    if (camera.aspect >= 1) {
      state.width  = params.resolution;
      state.height = (int)round(params.resolution / camera.aspect);
    } else {
      state.height = params.resolution;
      state.width  = (int)round(params.resolution * camera.aspect);
    }
    state.samples = 0;
    state.image.assign(state.width * state.height, {0, 0, 0, 0});
    state.hits.assign(state.width * state.height, 0);
    state.rngs.assign(state.width * state.height, {});
    auto rng_ = make_rng(1301081);
    for (auto& rng : state.rngs) {
      rng = make_rng(params.seed, rand1i(rng_, 1 << 31) / 2 + 1);
    }
    return state;
  }

  // Progressively compute an image by calling trace_samples multiple times.
  void trace_samples(trace_state& state, const scene_data& scene,
      const scene_bvh& bvh, const trace_params& params) {
    if (state.samples >= params.samples) return;
    if (params.noparallel) {
      for (auto j = 0; j < state.height; j++) {
        for (auto i = 0; i < state.width; i++) {
          trace_sample(state, scene, bvh, i, j, params);
        }
      }
    } else {
      parallel_for(state.width, state.height,
          [&](int i, int j) { trace_sample(state, scene, bvh, i, j, params); });
    }
    state.samples += 1;
  }

  // Check image type
  static void check_image(
      const image_data& image, int width, int height, bool linear) {
    if (image.width != width || image.height != height)
      throw std::invalid_argument{"image should have the same size"};
    if (image.linear != linear)
      throw std::invalid_argument{
          linear ? "expected linear image" : "expected srgb image"};
  }

  // Get resulting render
  image_data get_render(const trace_state& state) {
    auto image = make_image(state.width, state.height, true);
    get_render(image, state);
    return image;
  }
  void get_render(image_data& image, const trace_state& state) {
    check_image(image, state.width, state.height, true);
    auto scale = 1.0f / (float)state.samples;
    for (auto idx = 0; idx < state.width * state.height; idx++) {
      image.pixels[idx] = state.image[idx] * scale;
    }
  }

}  // namespace yocto
