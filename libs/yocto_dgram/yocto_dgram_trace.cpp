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

// -----------------------------------------------------------------------------
// INCLUDES
// -----------------------------------------------------------------------------

#include "yocto_dgram_trace.h"

#include <future>

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
// IMPLEMENTATION FOR PATH TRACING
// -----------------------------------------------------------------------------
namespace yocto {

  static vec4f eval_material(const dgram_scene& scene,
      const trace_shapes& shapes, const bvh_intersection& intersection) {
    auto& shape    = shapes.shapes[intersection.shape];
    auto& material = scene.materials[shape.material];
    return eval_material(
        shape, material, intersection.element, intersection.uv);
  }

  static bool eval_dashes(const dgram_scene& scene, const trace_shapes& shapes,
      const bvh_intersection& intersection, const dgram_trace_params& params,
      const bool first) {
    auto& shape    = shapes.shapes[intersection.shape];
    auto& camera   = scene.cameras[params.camera];
    auto& material = scene.materials[shape.material];

    if (!intersection.hit_arrow &&
        (material.dashed == dashed_line::always ||
            (material.dashed == dashed_line::transparency && !first)) &&
        (intersection.element.primitive == primitive_type::line ||
            intersection.element.primitive == primitive_type::border)) {
      return eval_dashes(intersection.position, shape, material,
          intersection.element, camera, params.size, params.scale);
    }

    return true;
  }

  static ray3f sample_camera(const dgram_camera& camera, const vec2i& ij,
      const vec2i& image_size, const vec2f& puv,
      const dgram_trace_params& params) {
    auto uv = vec2f{
        (ij.x + puv.x) / image_size.x, (ij.y + puv.y) / image_size.y};
    return eval_camera(camera, uv, params.size, params.scale);
  }

  dgram_trace_state make_state(const dgram_trace_params& params) {
    auto state   = dgram_trace_state{};
    state.width  = params.width;
    state.height = params.height;
    state.image.assign(state.width * state.height, {0, 0, 0, 0});
    state.rngs.assign(state.width * state.height, {});
    auto rng_ = make_rng(1301081);
    for (auto& rng : state.rngs) {
      rng = make_rng(params.seed, rand1i(rng_, 1 << 31) / 2 + 1);
    }

    return state;
  }

  static vec4f trace_text(const trace_texts& texts, const ray3f& ray,
      rng_state& rng, const dgram_trace_params& params) {
    auto text_color = vec4f{0, 0, 0, 0};
    for (auto& text : texts.texts) {
      auto uv = zero2f;
      if (intersect_text(text, ray, uv))
        text_color = composite(eval_text(text, uv), text_color);
    }
    return text_color;
  }

  static vec4f trace_color(const dgram_scene& scene, const trace_shapes& shapes,
      const dgram_scene_bvh& bvh, const ray3f& ray, rng_state& rng,
      const dgram_trace_params& params, const bool first) {
    auto radiance = vec4f{0, 0, 0, 0};

    auto intersections = intersect_bvh(bvh, shapes, ray);

    auto hit = false;

    for (auto& intersection : intersections.intersections) {
      hit        = true;
      auto color = eval_material(scene, shapes, intersection);
      color.w *= eval_dashes(scene, shapes, intersection, params, first);

      radiance = composite(color, radiance);
    }

    if (hit && radiance.w < 1) {
      auto back_color = trace_color(scene, shapes, bvh,
          {intersections.intersections[0].position, ray.d}, rng, params, false);
      return composite(radiance, back_color);
    }

    return radiance;
  }

  static vec4f trace_normal(const dgram_scene& scene,
      const trace_shapes& shapes, const dgram_scene_bvh& bvh, const ray3f& ray,
      rng_state& rng, const dgram_trace_params& params, const bool first) {
    auto intersections = intersect_bvh(bvh, shapes, ray);

    if (!intersections.intersections.empty())
      return rgb_to_rgba(intersections.intersections[0].normal);

    return vec4f{0, 0, 0, 0};
  }

  static vec4f trace_uv(const dgram_scene& scene, const trace_shapes& shapes,
      const dgram_scene_bvh& bvh, const ray3f& ray, rng_state& rng,
      const dgram_trace_params& params, const bool first) {
    auto intersections = intersect_bvh(bvh, shapes, ray);

    if (!intersections.intersections.empty()) {
      auto uv = intersections.intersections[0].uv;
      return {uv.x, uv.y, 0, 1};
    }

    return vec4f{0, 0, 0, 0};
  }

  static vec4f trace_eyelight(const dgram_scene& scene,
      const trace_shapes& shapes, const dgram_scene_bvh& bvh, const ray3f& ray,
      rng_state& rng, const dgram_trace_params& params, const bool first) {
    auto radiance = vec4f{0, 0, 0, 0};

    auto intersections = intersect_bvh(bvh, shapes, ray);

    auto hit = false;

    for (auto& intersection : intersections.intersections) {
      hit            = true;
      auto color     = eval_material(scene, shapes, intersection);
      auto rgb_color = rgba_to_rgb(color) * dot(intersection.normal, -ray.d);
      color.x        = rgb_color.x;
      color.y        = rgb_color.y;
      color.z        = rgb_color.z;
      color.w *= eval_dashes(scene, shapes, intersection, params, first);

      radiance = composite(color, radiance);
    }

    if (hit && radiance.w < 1) {
      auto back_color = trace_eyelight(scene, shapes, bvh,
          {intersections.intersections[0].position, ray.d}, rng, params, false);
      return composite(radiance, back_color);
    }

    return radiance;
  }

  using sampler_func = vec4f (*)(const dgram_scene& scene,
      const trace_shapes& shapes, const dgram_scene_bvh& bvh, const ray3f& ray,
      rng_state& rng, const dgram_trace_params& params, const bool first);
  static sampler_func get_trace_sampler_func(const dgram_trace_params& params) {
    switch (params.sampler) {
      case dgram_sampler_type::color: return trace_color;
      case dgram_sampler_type::normal: return trace_normal;
      case dgram_sampler_type::uv: return trace_uv;
      case dgram_sampler_type::eyelight: return trace_eyelight;
      default: {
        throw std::runtime_error("sampler unknown");
        return nullptr;
      }
    }
  }

  void trace_sample(dgram_trace_state& state, const dgram_scene& scene,
      const trace_shapes& shapes, const trace_texts& texts,
      const dgram_scene_bvh& bvh, int i, int j,
      const dgram_trace_params& params) {
    auto& camera  = scene.cameras[params.camera];
    auto  sampler = get_trace_sampler_func(params);
    auto  idx     = state.width * j + i;

    auto puv = rand2f(state.rngs[idx]);

    if (params.antialiasing == antialiasing_type::super_sampling) {
      auto ns = ceil(sqrt((float)params.samples));
      auto si = floor(state.samples / ns);
      auto sj = state.samples - floor(state.samples / ns) * ns;
      puv     = (vec2f{si, sj} + 0.5f) / ns;
    }

    auto offset = scene.offset * params.scale * params.width * 2 /
                  params.size.x;
    auto ii = i - (int)offset.x;
    auto ij = j - (int)offset.y;

    auto ray = sample_camera(
        camera, {ii, ij}, {state.width, state.height}, puv, params);
    auto radiance = sampler(
        scene, shapes, bvh, ray, state.rngs[idx], params, true);
    auto text = trace_text(texts, ray, state.rngs[idx], params);
    radiance  = composite(text, radiance);
    if (!isfinite(radiance)) radiance = {0, 0, 0};
    state.image[idx] += radiance;
  }

  void trace_samples(dgram_trace_state& state, const dgram_scene& scene,
      const trace_shapes& shapes, const trace_texts& texts,
      const dgram_scene_bvh& bvh, const dgram_trace_params& params) {
    if (state.samples >= params.samples) return;
    if (params.noparallel) {
      for (auto j = 0; j < state.height; j++) {
        for (auto i = 0; i < state.width; i++) {
          trace_sample(state, scene, shapes, texts, bvh, i, j, params);
        }
      }
    } else {
      parallel_for(state.width, state.height, [&](int i, int j) {
        trace_sample(state, scene, shapes, texts, bvh, i, j, params);
      });
    }
    state.samples += 1;
  }

  static void check_image(
      const image_data& image, int width, int height, bool linear) {
    if (image.width != width || image.height != height)
      throw std::invalid_argument{"image should have the same size"};
    if (image.linear != linear)
      throw std::invalid_argument{
          linear ? "expected linear image" : "expected srgb image"};
  }

  image_data get_render(const dgram_trace_state& state) {
    auto image = make_image(state.width, state.height, true);
    get_render(image, state);
    return image;
  }
  void get_render(image_data& image, const dgram_trace_state& state) {
    check_image(image, state.width, state.height, true);
    auto scale = 1.0f / (float)state.samples;
    for (auto idx = 0; idx < state.width * state.height; idx++) {
      image.pixels[idx] = state.image[idx] * scale;
    }
  }

}  // namespace yocto
