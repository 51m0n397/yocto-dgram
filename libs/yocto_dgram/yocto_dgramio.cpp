//
// # Yocto/Dgramio: Diagram loader
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

#include "yocto_dgramio.h"

#include <filesystem>
#include <yocto/ext/json.hpp>

// -----------------------------------------------------------------------------
// USING DIRECTIVES
// -----------------------------------------------------------------------------
namespace yocto {}  // namespace yocto

// -----------------------------------------------------------------------------
// PATH UTILITIES
// -----------------------------------------------------------------------------
namespace yocto {

  // Make a path from a utf8 string
  static std::filesystem::path make_path(const string& filename) {
    return std::filesystem::u8path(filename);
  }

  // Get extension (including .)
  static string path_extension(const string& filename) {
    return make_path(filename).extension().u8string();
  }

}  // namespace yocto

// -----------------------------------------------------------------------------
// JSON SUPPORT
// -----------------------------------------------------------------------------
namespace yocto {

  // Json values
  using json_value = nlohmann::ordered_json;

  // Load/save json
  static bool load_json(
      const string& filename, json_value& json, string& error) {
    auto text = string{};
    if (!load_text(filename, text, error)) return false;
    try {
      json = json_value::parse(text);
      return true;
    } catch (...) {
      error = "cannot parse " + filename;
      return false;
    }
  }
  static bool save_json(
      const string& filename, const json_value& json, string& error) {
    return save_text(filename, json.dump(2), error);
  }

  // Load/save json
  [[maybe_unused]] static json_value load_json(const string& filename) {
    auto error = string{};
    auto json  = json_value{};
    if (!load_json(filename, json, error)) throw io_error{error};
    return json;
  }
  [[maybe_unused]] static void load_json(
      const string& filename, json_value& json) {
    auto error = string{};
    if (!load_json(filename, json, error)) throw io_error{error};
  }
  [[maybe_unused]] static void save_json(
      const string& filename, const json_value& json) {
    auto error = string{};
    if (!save_json(filename, json, error)) throw io_error{error};
  }

  // Json conversions
  inline void to_json(json_value& json, const vec2f& value) {
    nlohmann::to_json(json, (const array<float, 2>&)value);
  }
  inline void to_json(json_value& json, const vec3f& value) {
    nlohmann::to_json(json, (const array<float, 3>&)value);
  }
  inline void to_json(json_value& json, const vec4f& value) {
    nlohmann::to_json(json, (const array<float, 4>&)value);
  }
  inline void to_json(json_value& json, const frame2f& value) {
    nlohmann::to_json(json, (const array<float, 6>&)value);
  }
  inline void to_json(json_value& json, const frame3f& value) {
    nlohmann::to_json(json, (const array<float, 12>&)value);
  }
  inline void to_json(json_value& json, const mat2f& value) {
    nlohmann::to_json(json, (const array<float, 4>&)value);
  }
  inline void to_json(json_value& json, const mat3f& value) {
    nlohmann::to_json(json, (const array<float, 9>&)value);
  }
  inline void to_json(json_value& json, const mat4f& value) {
    nlohmann::to_json(json, (const array<float, 16>&)value);
  }
  inline void from_json(const json_value& json, vec2f& value) {
    nlohmann::from_json(json, (array<float, 2>&)value);
  }
  inline void from_json(const json_value& json, vec3f& value) {
    nlohmann::from_json(json, (array<float, 3>&)value);
  }
  inline void from_json(const json_value& json, vec4f& value) {
    nlohmann::from_json(json, (array<float, 4>&)value);
  }
  inline void from_json(const json_value& json, vec2i& value) {
    nlohmann::from_json(json, (array<int, 2>&)value);
  }
  inline void from_json(const json_value& json, vec3i& value) {
    nlohmann::from_json(json, (array<int, 3>&)value);
  }
  inline void from_json(const json_value& json, vec4i& value) {
    nlohmann::from_json(json, (array<int, 4>&)value);
  }
  inline void from_json(const json_value& json, frame2f& value) {
    nlohmann::from_json(json, (array<float, 6>&)value);
  }
  inline void from_json(const json_value& json, frame3f& value) {
    nlohmann::from_json(json, (array<float, 12>&)value);
  }
  inline void from_json(const json_value& json, mat2f& value) {
    nlohmann::from_json(json, (array<float, 4>&)value);
  }
  inline void from_json(const json_value& json, mat3f& value) {
    nlohmann::from_json(json, (array<float, 9>&)value);
  }
  inline void from_json(const json_value& json, mat4f& value) {
    nlohmann::from_json(json, (array<float, 16>&)value);
  }

}  // namespace yocto

// -----------------------------------------------------------------------------
// DGRAM SCENES LOADER
// -----------------------------------------------------------------------------
namespace yocto {

  static bool load_json_dgram(
      const string& filename, dgram_scenes& dgram, string& error) {
    // open file
    auto json = json_value{};
    if (!load_json(filename, json, error)) return false;

    auto get_opt = [](const json_value& json, const string& key, auto& value) {
      value = json.value(key, value);
    };

    // parsing values
    try {
      get_opt(json, "size", dgram.size);
      get_opt(json, "resolution", dgram.scale);

      if (json.contains("scenes")) {
        auto& jscenes = json.at("scenes");
        dgram.scenes.reserve(jscenes.size());

        for (auto& jscene : jscenes) {
          auto& scene = dgram.scenes.emplace_back();

          get_opt(jscene, "offset", scene.offset);

          if (jscene.contains("cameras")) {
            auto& jcameras = jscene.at("cameras");
            scene.cameras.reserve(jcameras.size());

            for (auto& jcamera : jcameras) {
              auto& camera = scene.cameras.emplace_back();

              get_opt(jcamera, "orthographic", camera.orthographic);
              get_opt(jcamera, "center", camera.center);
              get_opt(jcamera, "from", camera.from);
              get_opt(jcamera, "to", camera.to);
              get_opt(jcamera, "lens", camera.lens);
            }
          }

          if (jscene.contains("objects")) {
            auto& jobjects = jscene.at("objects");
            scene.objects.reserve(jobjects.size());

            for (auto& jobject : jobjects) {
              auto& object = scene.objects.emplace_back();

              get_opt(jobject, "frame", object.frame);
              get_opt(jobject, "shape", object.shape);
              get_opt(jobject, "material", object.material);
              get_opt(jobject, "labels", object.labels);
            }
          }

          if (jscene.contains("materials")) {
            auto& jmaterials = jscene.at("materials");
            scene.materials.reserve(jmaterials.size());

            for (auto& jmaterial : jmaterials) {
              auto& material = scene.materials.emplace_back();

              get_opt(jmaterial, "fill", material.fill);
              material.fill = srgb_to_rgb(material.fill);
              get_opt(jmaterial, "stroke", material.stroke);
              material.stroke = srgb_to_rgb(material.stroke);
              get_opt(jmaterial, "thickness", material.thickness);
            }
          }

          if (jscene.contains("shapes")) {
            auto& jshapes = jscene.at("shapes");
            scene.shapes.reserve(jshapes.size());

            for (auto& jshape : jshapes) {
              auto& shape = scene.shapes.emplace_back();

              get_opt(jshape, "points", shape.points);
              get_opt(jshape, "triangles", shape.triangles);
              get_opt(jshape, "quads", shape.quads);

              get_opt(jshape, "positions", shape.positions);
              get_opt(jshape, "fills", shape.fills);
              for (auto& fill : shape.fills) fill = srgb_to_rgb(fill);

              get_opt(jshape, "cull", shape.cull);
              get_opt(jshape, "boundary", shape.boundary);

              auto lines = vector<vec2i>{};
              get_opt(jshape, "lines", lines);
              for (auto idx = 0; idx < lines.size(); idx++)
                shape.ends.push_back(line_ends{line_end::cap, line_end::cap});

              auto arrows = vector<vec2i>{};
              get_opt(jshape, "arrows", arrows);
              for (auto idx = 0; idx < arrows.size(); idx++)
                shape.ends.push_back(line_ends{line_end::cap, line_end::arrow});

              lines.insert(lines.end(), arrows.begin(), arrows.end());
              shape.lines = lines;

              get_opt(jshape, "cclips", shape.cclips);
            }
          }

          if (jscene.contains("labels")) {
            auto& jlabels = jscene.at("labels");
            scene.labels.reserve(jlabels.size());

            for (auto& jlabel : jlabels) {
              auto& label = scene.labels.emplace_back();

              get_opt(jlabel, "positions", label.positions);

              if (jlabel.contains("labels")) {
                for (auto& elem : jlabel.at("labels")) {
                  auto& texts      = label.texts.emplace_back("");
                  auto& offsets    = label.offsets.emplace_back(vec2f{0, 0});
                  auto& alignments = label.alignments.emplace_back(vec2f{0, 0});
                  get_opt(elem, "unprocessed", texts);
                  get_opt(elem, "offset", offsets);
                  get_opt(elem, "alignment", alignments);
                }
              }
            }
          }
        }
      }

    } catch (...) {
      error = "cannot parse " + filename;
      return false;
    }

    return true;
  }

  bool load_dgram(const string& filename, dgram_scenes& dgram, string& error) {
    auto ext = path_extension(filename);
    if (ext == ".json" || ext == ".JSON") {
      return load_json_dgram(filename, dgram, error);
    } else {
      error = "unsupported format " + filename;
      return false;
    }
  }

  dgram_scenes load_dgram(const string& filename) {
    auto error = string{};
    auto dgram = dgram_scenes{};
    if (!load_dgram(filename, dgram, error)) throw io_error{error};
    return dgram;
  }

}  // namespace yocto