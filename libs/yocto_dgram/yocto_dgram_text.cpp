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
#include "yocto_dgram_geometry.h"

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

  static string placeholder =
      "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";

  string escape_string(const string& value) {
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

  static image_data base64_to_image(const string& base64) {
    auto image = image_data{};

    auto buffer  = base64_decode(base64);
    auto ncomp   = 0;
    auto pixels  = stbi_load_from_memory(buffer.data(), (int)buffer.size(),
         &image.width, &image.height, &ncomp, 4);
    image.linear = false;
    image.pixels = vector<vec4f>((size_t)image.width * (size_t)image.height);
    for (size_t i = 0; i < image.pixels.size(); i++) {
      image.pixels[i] = byte_to_float(((vec4b*)pixels)[i]);
    }
    free(pixels);

    return image;
  }

  static image_data make_placeholder(
      const float alignment, const int width, const int height) {
    auto image             = make_image(width * 2, height * 2, false);
    auto placeholder_image = base64_to_image(placeholder);
    placeholder_image = resize_image(placeholder_image, width * 130 / 720, 0);

    auto i = (image.width - placeholder_image.width) / 2;
    if (alignment < 0)
      i = 0;
    else if (alignment > 0)
      i = image.width - placeholder_image.width;
    auto j = image.height - placeholder_image.height;
    set_region(image, placeholder_image, i, j);

    return image;
  }

  static image_data make_text_image(const string& text, const float alignment,
      const vec4f& color, const int width, const int height, const float zoom) {
    http::Request request{"localhost:5500/rasterize"};
    auto body = "text=" + escape_string(text) + "&width=" + to_string(width) +
                "&height=" + to_string(height) + "&zoom=" + to_string(zoom) +
                "&align_x=" + to_string(alignment) +
                "&r=" + to_string((int)round(color.x * 255)) +
                "&g=" + to_string((int)round(color.y * 255)) +
                "&b=" + to_string((int)round(color.z * 255)) +
                "&a=" + to_string((int)round(color.w));
    auto response = request.send(
        "POST", body, {"Content-Type: application/x-www-form-urlencoded"});
    auto string64 = string{response.body.begin(), response.body.end()};

    return base64_to_image(string64);
  }

  static trace_text make_text(const int i, const int j, dgram_scene& scene,
      const int width, const int height, const vec2f& size, const float scale,
      const bool orthographic, const frame3f& camera_frame,
      const float camera_distance, const vec2f& film, const float lens,
      const bool rerender) {
    auto text = trace_text{};

    auto& object   = scene.objects[i];
    auto& label    = scene.labels[object.labels];
    auto& material = scene.materials[object.material];
    auto  color    = rgb_to_srgb(material.stroke);

    if (rerender) {
      text.image = make_text_image(label.texts[j], label.alignments[j].x, color,
          width, height, width / size.x);
      label.images[j] = text.image;
    } else {
      if (!label.images[j].pixels.empty() && label.images[j].width == width * 2)
        text.image = label.images[j];
      else
        text.image = make_placeholder(label.alignments[j].x, width, height);
    }

    // Computing text positions
    auto p        = transform_point(object.frame, label.positions[j]);
    auto camera_p = transform_point(inverse(camera_frame), p);

    auto offset   = label.offsets[j];
    auto baseline = 7.0f;

    auto plane_distance = -lens * scale / size.x;

    // fix for when point is behind the camera
    if (camera_p.z >= 0) camera_p.z = -ray_eps;

    auto im_w = film.x;
    auto im_h = film.y;
    if (orthographic) {
      im_w *= size.x * camera_distance / (scale * lens);
      im_h *= size.x * camera_distance / (scale * lens);
    }

    auto screen_off =
        orthographic
            ? vec3f{offset.x * film.x * camera_distance / (scale * lens),
                  (-baseline - offset.y) * film.x * camera_distance /
                      (scale * lens),
                  0}
            : vec3f{offset.x * film.x / size.x,
                  (-baseline - offset.y) * film.x / size.x, 0};

    float align_x0, align_x1, align_x2, align_x3;

    if (label.alignments[j].x > 0) {
      align_x0 = im_w;
      align_x1 = 0.0f;
      align_x2 = 0.0f;
      align_x3 = im_w;
    } else if (label.alignments[j].x < 0) {
      align_x0 = 0.0f;
      align_x1 = -im_w;
      align_x2 = -im_w;
      align_x3 = 0.0f;
    } else {
      align_x0 = im_w / 2;
      align_x1 = -im_w / 2;
      align_x2 = -im_w / 2;
      align_x3 = im_w / 2;
    }

    auto align_y0 = -im_h;
    auto align_y1 = -im_h;
    auto align_y2 = 0.0f;
    auto align_y3 = 0.0f;

    vec3f p0, p1, p2, p3;

    auto screen_camera_p = orthographic
                               ? vec3f{camera_p.x, camera_p.y, 0}
                               : screen_space_point(camera_p, plane_distance);
    screen_camera_p += screen_off;

    auto screen_camera_p0 = screen_camera_p - vec3f{align_x0, align_y0, 0};
    auto screen_camera_p1 = screen_camera_p - vec3f{align_x1, align_y1, 0};
    auto screen_camera_p2 = screen_camera_p - vec3f{align_x2, align_y2, 0};
    auto screen_camera_p3 = screen_camera_p - vec3f{align_x3, align_y3, 0};

    if (orthographic) {
      p0 = transform_point(camera_frame,
          vec3f{screen_camera_p0.x, screen_camera_p0.y, camera_p.z});
      p1 = transform_point(camera_frame,
          vec3f{screen_camera_p1.x, screen_camera_p1.y, camera_p.z});
      p2 = transform_point(camera_frame,
          vec3f{screen_camera_p2.x, screen_camera_p2.y, camera_p.z});
      p3 = transform_point(camera_frame,
          vec3f{screen_camera_p3.x, screen_camera_p3.y, camera_p.z});
    } else {
      p0 = transform_point(
          camera_frame, world_space_point(screen_camera_p0, camera_p.z));
      p1 = transform_point(
          camera_frame, world_space_point(screen_camera_p1, camera_p.z));
      p2 = transform_point(
          camera_frame, world_space_point(screen_camera_p2, camera_p.z));
      p3 = transform_point(
          camera_frame, world_space_point(screen_camera_p3, camera_p.z));
    }

    text.positions.push_back(p0);
    text.positions.push_back(p1);
    text.positions.push_back(p2);
    text.positions.push_back(p3);

    text.name = label.names[j];

    return text;
  }

  text_images make_text_images(const dgram_scene& scene, const vec2f& size,
      const float& scale, const int width, const int height) {
    auto images = text_images{};

    for (auto i = 0; i < scene.objects.size(); i++) {
      auto& object = scene.objects[i];
      if (object.labels != -1) {
        auto& label    = scene.labels[object.labels];
        auto& material = scene.materials[object.material];
        auto  color    = rgb_to_srgb(material.stroke);
        for (auto j = 0; j < label.texts.size(); j++) {
          auto& text_image = images.images.emplace_back();
          text_image.image = make_text_image(label.texts[j],
              label.alignments[j].x, color, width, height, width / size.x);
          text_image.name  = label.names[j];
        }
      }
    }

    return images;
  }

  trace_texts make_texts(dgram_scene& scene, const int& cam, const vec2f& size,
      const float& scale, const int width, const int height,
      const bool noparallel, const bool rerender) {
    auto& camera          = scene.cameras[cam];
    auto  camera_frame    = lookat_frame(camera.from, camera.to, {0, 1, 0});
    auto  camera_distance = length(camera.from - camera.to);
    auto  aspect          = size.x / size.y;
    auto  film = aspect >= 1 ? vec2f{camera.film, camera.film / aspect}
                             : vec2f{camera.film * aspect, camera.film};

    auto texts = trace_texts{};

    if (noparallel) {
      for (auto i = 0; i < scene.objects.size(); i++) {
        auto& object = scene.objects[i];
        if (object.labels != -1) {
          auto& label = scene.labels[object.labels];
          for (auto j = 0; j < label.texts.size(); j++) {
            auto text = make_text(i, j, scene, width, height, size, scale,
                camera.orthographic, camera_frame, camera_distance, film,
                camera.lens, rerender);
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
        auto i           = idxs[idx].first;
        auto j           = idxs[idx].second;
        auto text        = make_text(i, j, scene, width, height, size, scale,
                   camera.orthographic, camera_frame, camera_distance, film,
                   camera.lens, rerender);
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
    return eval_image(text.image, uv, true);
  }

}  // namespace yocto