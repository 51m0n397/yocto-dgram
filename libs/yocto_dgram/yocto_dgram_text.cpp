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

// -----------------------------------------------------------------------------
// INCLUDES
// -----------------------------------------------------------------------------

#include "yocto_dgram_text.h"

#include <yocto/ext/stb_image.h>
#include <yocto/yocto_geometry.h>

#include <future>
#include <iomanip>
#include <sstream>

#include "ext/HTTPRequest.hpp"
#include "ext/base64.h"

// -----------------------------------------------------------------------------
// USING DIRECTIVES
// -----------------------------------------------------------------------------
namespace yocto {

  // using directives
  using std::make_pair;
  using std::pair;
  using std::to_string;
}  // namespace yocto

// -----------------------------------------------------------------------------
// PARALLEL HELPERS
// -----------------------------------------------------------------------------
namespace yocto {

  // Simple parallel for used since our target platforms do not yet support
  // parallel algorithms. `Func` takes the integer index.
  template <typename T, typename Func>
  inline void parallel_for(T num, Func&& func) {
    auto              futures  = vector<std::future<void>>{};
    auto              nthreads = std::thread::hardware_concurrency();
    std::atomic<T>    next_idx(0);
    std::atomic<bool> has_error(false);
    for (auto thread_id = 0; thread_id < (int)nthreads; thread_id++) {
      futures.emplace_back(
          std::async(std::launch::async, [&func, &next_idx, &has_error, num]() {
            try {
              while (true) {
                auto idx = next_idx.fetch_add(1);
                if (idx >= num) break;
                if (has_error) break;
                func(idx);
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
// TEXT BUILD
// -----------------------------------------------------------------------------
namespace yocto {

  static vec3f intersect_plane(
      const ray3f& ray, const vec3f& p, const vec3f& n) {
    auto o = ray.o - p;

    auto den = dot(ray.d, n);
    if (den == 0) return vec3f{0, 0, 0};

    auto t = -dot(n, o) / den;

    auto q = ray.o + ray.d * t;

    return q;
  }

  static string escape_string(const string& value) {
    // https://stackoverflow.com/questions/154536/encode-decode-urls-in-c
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (string::const_iterator i = value.begin(), n = value.end(); i != n;
         ++i) {
      string::value_type c = (*i);

      // Keep alphanumeric and other accepted characters intact
      if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
        escaped << c;
        continue;
      }

      // Any other characters are percent-encoded
      escaped << std::uppercase;
      escaped << '%' << std::setw(2) << int((unsigned char)c);
      escaped << std::nouppercase;
    }

    return escaped.str();
  }

  static trace_text make_text(const int i, const int j,
      const dgram_scene& scene, const int width, const int height,
      const float zoom, const vec2f& size, const float scale,
      const bool orthographic, const frame3f& camera_frame,
      const vec3f& camera_origin, const vec3f& plane_point,
      const vec3f& plane_dir, const vec2f& film) {
    auto text = trace_text{};

    auto& object   = scene.objects[i];
    auto& label    = scene.labels[object.labels];
    auto& material = scene.materials[object.material];
    auto  color    = rgb_to_srgb(material.stroke);

    // Requesting text image from server
    http::Request request{"localhost:5500/rasterize"};
    auto          body = "text=" + escape_string(label.texts[j]) +
                "&width=" + to_string(width) + "&height=" + to_string(height) +
                "&zoom=" + to_string(zoom * width / size.x) +
                "&align_x=" + to_string(label.alignments[j].x) +
                "&r=" + to_string((int)round(color.x * 255)) +
                "&g=" + to_string((int)round(color.y * 255)) +
                "&b=" + to_string((int)round(color.z * 255)) +
                "&a=" + to_string((int)round(color.w * 255));
    auto response = request.send(
        "POST", body, {"Content-Type: application/x-www-form-urlencoded"});
    auto string64     = string{response.body.begin(), response.body.end()};
    auto buffer       = base64_decode(string64);
    auto ncomp        = 0;
    auto pixels       = stbi_load_from_memory(buffer.data(), (int)buffer.size(),
              &text.image.width, &text.image.height, &ncomp, 4);
    text.image.linear = false;
    text.image.pixels = vector<vec4f>(
        (size_t)text.image.width * (size_t)text.image.height);
    for (size_t i = 0; i < text.image.pixels.size(); i++) {
      text.image.pixels[i] = byte_to_float(((vec4b*)pixels)[i]);
    }
    free(pixels);

    // Computing text positions
    auto p = transform_point(object.frame, label.positions[j]);

    auto offset   = label.offsets[j] * zoom;
    auto baseline = 7.0f * zoom;

    auto align_x0 = 0.0f;
    auto align_x1 = 0.0f;
    auto align_x2 = 0.0f;
    auto align_x3 = 0.0f;

    if (label.alignments[j].x > 0) {
      align_x0 = 1.0f;
      align_x1 = 0.0f;
      align_x2 = 0.0f;
      align_x3 = 1.0f;
    } else if (label.alignments[j].x < 0) {
      align_x0 = 0.0f;
      align_x1 = -1.0f;
      align_x2 = -1.0f;
      align_x3 = 0.0f;
    } else {
      align_x0 = 0.5f;
      align_x1 = -0.5f;
      align_x2 = -0.5f;
      align_x3 = 0.5f;
    }

    auto align_y0 = -1.0f;
    auto align_y1 = -1.0f;
    auto align_y2 = 0.0f;
    auto align_y3 = 0.0f;

    vec3f p0, p1, p2, p3;

    if (orthographic) {
      auto poff = p +
                  vec3f{offset.x / scale, (-baseline - offset.y) / scale, 0};
      p0 = poff -
           vec3f{align_x0 * size.x / scale, align_y0 * size.y / scale, 0};
      p1 = poff -
           vec3f{align_x1 * size.x / scale, align_y1 * size.y / scale, 0};
      p2 = poff -
           vec3f{align_x2 * size.x / scale, align_y2 * size.y / scale, 0};
      p3 = poff -
           vec3f{align_x3 * size.x / scale, align_y3 * size.y / scale, 0};
    } else {
      auto po = normalize(p - camera_origin);

      auto p_on_plane = intersect_plane(
          ray3f{camera_origin, po}, plane_point, plane_dir);

      auto trans_p = transform_point(inverse(camera_frame), p_on_plane);
      trans_p += vec3f{-offset.x / size.x * film.x,
          (baseline + offset.y) / size.x * film.x, 0};

      auto p0_on_plane = transform_point(camera_frame,
          trans_p + vec3f{align_x0 * film.x, align_y0 * film.y, 0});
      auto p1_on_plane = transform_point(camera_frame,
          trans_p + vec3f{align_x1 * film.x, align_y1 * film.y, 0});
      auto p2_on_plane = transform_point(camera_frame,
          trans_p + vec3f{align_x2 * film.x, align_y2 * film.y, 0});
      auto p3_on_plane = transform_point(camera_frame,
          trans_p + vec3f{align_x3 * film.x, align_y3 * film.y, 0});

      p0 = intersect_plane(
          ray3f{camera_origin, p0_on_plane - camera_origin}, p, plane_dir);
      p1 = intersect_plane(
          ray3f{camera_origin, p1_on_plane - camera_origin}, p, plane_dir);
      p2 = intersect_plane(
          ray3f{camera_origin, p2_on_plane - camera_origin}, p, plane_dir);
      p3 = intersect_plane(
          ray3f{camera_origin, p3_on_plane - camera_origin}, p, plane_dir);
    }

    text.positions.push_back(p0);
    text.positions.push_back(p1);
    text.positions.push_back(p2);
    text.positions.push_back(p3);

    return text;
  }

  trace_texts make_texts(const dgram_scene& scene, const int& cam,
      const vec2f& size, const float& scale, const int width, const int height,
      const bool noparallel) {
    auto& camera        = scene.cameras[cam];
    auto  camera_frame  = lookat_frame(camera.from, camera.to, {0, 1, 0});
    auto  camera_origin = camera.from;
    auto  plane_point   = transform_point(
           camera_frame, {0, 0, camera.lens / size.x * scale});
    auto plane_dir = transform_normal(
        camera_frame, {0, 0, camera.lens / size.x * scale});
    auto aspect = size.x / size.y;
    auto film   = aspect >= 1 ? vec2f{camera.film, camera.film / aspect}
                              : vec2f{camera.film * aspect, camera.film};
    auto zoom   = camera.orthographic ? length(camera.from - camera.to) : 1.0f;

    auto texts = trace_texts{};

    if (noparallel) {
      for (auto i = 0; i < scene.objects.size(); i++) {
        auto& object = scene.objects[i];
        if (object.labels != -1) {
          auto& label = scene.labels[object.labels];
          for (auto j = 0; j < label.texts.size(); j++) {
            auto text = make_text(i, j, scene, width, height, zoom, size, scale,
                camera.orthographic, camera_frame, camera_origin, plane_point,
                plane_dir, film);
            texts.texts.push_back(text);
          }
        }
      }
    } else {
      auto idxs = vector<pair<int, int>>{};
      for (auto i = 0; i < scene.objects.size(); i++) {
        auto& object = scene.objects[i];
        if (object.labels != -1) {
          auto& label = scene.labels[object.labels];
          for (auto j = 0; j < label.texts.size(); j++) {
            idxs.push_back(make_pair(i, j));
          }
        }
      }

      texts.texts.resize(idxs.size());
      parallel_for(idxs.size(), [&](size_t idx) {
        auto i    = idxs[idx].first;
        auto j    = idxs[idx].second;
        auto text = make_text(i, j, scene, width, height, zoom, size, scale,
            camera.orthographic, camera_frame, camera_origin, plane_point,
            plane_dir, film);
        texts.texts[idx] = text;
      });
    }

    return texts;
  }

  bool intersect_text(const trace_text& text, const ray3f& ray, vec2f& uv) {
    auto dist = 0.0f;
    return intersect_quad(ray, text.positions[0], text.positions[1],
        text.positions[2], text.positions[3], uv, dist);
  }

}  // namespace yocto

// -----------------------------------------------------------------------------
// TEXT PROPERTIES EVALUATION
// -----------------------------------------------------------------------------
namespace yocto {

  vec4f eval_text(const trace_text& text, const vec2f& uv) {
    auto size = vec2i{text.image.width, text.image.height};
    auto s    = fmod(uv.x, 1.0f) * size.x;
    if (s < 0) s += size.x;
    auto t = fmod(uv.y, 1.0f) * size.y;
    if (t < 0) t += size.y;

    auto i = clamp((int)s, 0, size.x - 1);
    auto j = clamp((int)t, 0, size.y - 1);

    auto color = text.image.pixels[j * size.x + i];
    return srgb_to_rgb(color);
  }

}  // namespace yocto