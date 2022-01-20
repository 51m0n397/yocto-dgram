//
// Implementation for Yocto/Scene.
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

#include "yocto_scene.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "ext/HTTPRequest.hpp"
#include "ext/base64.h"
#include "ext/stb_image.h"
#include "yocto_color.h"
#include "yocto_geometry.h"
#include "yocto_image.h"
#include "yocto_noise.h"
#include "yocto_shape.h"

// -----------------------------------------------------------------------------
// USING DIRECTIVES
// -----------------------------------------------------------------------------
namespace yocto {

  // using directives
  using std::unique_ptr;
  using namespace std::string_literals;
  using std::to_string;

}  // namespace yocto

// -----------------------------------------------------------------------------
// CAMERA PROPERTIES
// -----------------------------------------------------------------------------
namespace yocto {

  // Generates a ray from a camera for yimg::image plane coordinate uv and
  // the lens coordinates luv.
  ray3f eval_camera(
      const camera_data& camera, const vec2f& image_uv, const vec2f& lens_uv) {
    auto film = camera.aspect >= 1
                    ? vec2f{camera.film, camera.film / camera.aspect}
                    : vec2f{camera.film * camera.aspect, camera.film};
    if (!camera.orthographic) {
      auto q = vec3f{film.x * (0.5f - image_uv.x), film.y * (image_uv.y - 0.5f),
          camera.lens};
      // ray direction through the lens center
      auto dc = -normalize(q);
      // point on the lens
      auto e = vec3f{
          lens_uv.x * camera.aperture / 2, lens_uv.y * camera.aperture / 2, 0};
      // point on the focus plane
      auto p = dc * camera.focus / abs(dc.z);
      // correct ray direction to account for camera focusing
      auto d = normalize(p - e);
      // done
      return ray3f{transform_point(camera.frame, e),
          transform_direction(camera.frame, d)};
    } else {
      auto scale = camera.focus / camera.lens;
      auto q     = vec3f{film.x * (0.5f - image_uv.x) * scale,
          film.y * (image_uv.y - 0.5f) * scale, camera.lens};
      // point on the lens
      auto e = vec3f{-q.x, -q.y, 0} + vec3f{lens_uv.x * camera.aperture / 2,
                                          lens_uv.y * camera.aperture / 2, 0};
      // point on the focus plane
      auto p = vec3f{-q.x, -q.y, -camera.focus};
      // correct ray direction to account for camera focusing
      auto d = normalize(p - e);
      // done
      return ray3f{transform_point(camera.frame, e),
          transform_direction(camera.frame, d)};
    }
  }

}  // namespace yocto

// -----------------------------------------------------------------------------
// TEXTURE PROPERTIES
// -----------------------------------------------------------------------------
namespace yocto {

  // pixel access
  vec4f lookup_texture(
      const texture_data& texture, int i, int j, bool as_linear) {
    auto color = vec4f{0, 0, 0, 0};
    if (!texture.pixelsf.empty()) {
      color = texture.pixelsf[j * texture.width + i];
    } else {
      color = byte_to_float(texture.pixelsb[j * texture.width + i]);
    }
    if (as_linear && !texture.linear) {
      return srgb_to_rgb(color);
    } else {
      return color;
    }
  }

  // Evaluates an image at a point `uv`.
  vec4f eval_texture(const texture_data& texture, const vec2f& uv,
      bool as_linear, bool no_interpolation, bool clamp_to_edge) {
    if (texture.width == 0 || texture.height == 0) return {0, 0, 0, 0};

    // get texture width/height
    auto size = vec2i{texture.width, texture.height};

    // get coordinates normalized for tiling
    auto s = 0.0f, t = 0.0f;
    if (clamp_to_edge) {
      s = clamp(uv.x, 0.0f, 1.0f) * size.x;
      t = clamp(uv.y, 0.0f, 1.0f) * size.y;
    } else {
      s = fmod(uv.x, 1.0f) * size.x;
      if (s < 0) s += size.x;
      t = fmod(uv.y, 1.0f) * size.y;
      if (t < 0) t += size.y;
    }

    // get image coordinates and residuals
    auto i = clamp((int)s, 0, size.x - 1), j = clamp((int)t, 0, size.y - 1);
    auto ii = (i + 1) % size.x, jj = (j + 1) % size.y;
    auto u = s - i, v = t - j;

    // handle interpolation
    if (no_interpolation) {
      return lookup_texture(texture, i, j, as_linear);
    } else {
      return lookup_texture(texture, i, j, as_linear) * (1 - u) * (1 - v) +
             lookup_texture(texture, i, jj, as_linear) * (1 - u) * v +
             lookup_texture(texture, ii, j, as_linear) * u * (1 - v) +
             lookup_texture(texture, ii, jj, as_linear) * u * v;
    }
  }

  // Helpers
  vec4f eval_texture(const scene_data& scene, int texture, const vec2f& uv,
      bool ldr_as_linear, bool no_interpolation, bool clamp_to_edge) {
    if (texture == invalidid) return {1, 1, 1, 1};
    return eval_texture(
        scene.textures[texture], uv, ldr_as_linear, no_interpolation);
  }

  // conversion from image
  texture_data image_to_texture(const image_data& image) {
    auto texture = texture_data{
        image.width, image.height, image.linear, {}, {}};
    if (image.linear) {
      texture.pixelsf = image.pixels;
    } else {
      texture.pixelsb.resize(image.pixels.size());
      float_to_byte(texture.pixelsb, image.pixels);
    }
    return texture;
  }

}  // namespace yocto

// -----------------------------------------------------------------------------
// MATERIAL PROPERTIES
// -----------------------------------------------------------------------------
namespace yocto {

  // Evaluate material
  material_point eval_material(const scene_data& scene,
      const material_data& material, const vec2f& texcoord) {
    // evaluate textures
    auto fill_tex   = eval_texture(scene, material.fill_tex, texcoord, true);
    auto stroke_tex = eval_texture(scene, material.stroke_tex, texcoord, true);

    // material point
    auto point   = material_point{};
    point.fill   = material.fill * fill_tex;
    point.stroke = material.stroke * stroke_tex;

    return point;
  }
}  // namespace yocto

// -----------------------------------------------------------------------------
// INSTANCE PROPERTIES
// -----------------------------------------------------------------------------
namespace yocto {

  // Eval position
  vec3f eval_position(const scene_data& scene, const instance_data& instance,
      int element, const vec2f& uv) {
    auto& shape = scene.shapes[instance.shape];
    if (!shape.triangles.empty()) {
      auto t = shape.triangles[element];
      return transform_point(
          instance.frame, interpolate_triangle(shape.positions[t.x],
                              shape.positions[t.y], shape.positions[t.z], uv));
    } else if (!shape.quads.empty()) {
      auto q = shape.quads[element];
      return transform_point(instance.frame,
          interpolate_quad(shape.positions[q.x], shape.positions[q.y],
              shape.positions[q.z], shape.positions[q.w], uv));
    } else if (!shape.lines.empty()) {
      auto l = shape.lines[element];
      return transform_point(instance.frame,
          interpolate_line(shape.positions[l.x], shape.positions[l.y], uv.x));
    } else if (!shape.points.empty()) {
      return transform_point(
          instance.frame, shape.positions[shape.points[element]]);
    } else {
      return {0, 0, 0};
    }
  }

  // Shape element normal.
  vec3f eval_element_normal(
      const scene_data& scene, const instance_data& instance, int element) {
    auto& shape = scene.shapes[instance.shape];
    if (!shape.triangles.empty()) {
      auto t = shape.triangles[element];
      return transform_normal(
          instance.frame, triangle_normal(shape.positions[t.x],
                              shape.positions[t.y], shape.positions[t.z]));
    } else if (!shape.quads.empty()) {
      auto q = shape.quads[element];
      return transform_normal(instance.frame,
          quad_normal(shape.positions[q.x], shape.positions[q.y],
              shape.positions[q.z], shape.positions[q.w]));
    } else if (!shape.lines.empty()) {
      auto l = shape.lines[element];
      return transform_normal(instance.frame,
          line_tangent(shape.positions[l.x], shape.positions[l.y]));
    } else if (!shape.points.empty()) {
      return {0, 0, 1};
    } else {
      return {0, 0, 0};
    }
  }

  // Eval normal
  vec3f eval_normal(const scene_data& scene, const instance_data& instance,
      int element, const vec2f& uv) {
    auto& shape = scene.shapes[instance.shape];
    if (shape.normals.empty())
      return eval_element_normal(scene, instance, element);
    if (!shape.triangles.empty()) {
      auto t = shape.triangles[element];
      return transform_normal(
          instance.frame, normalize(interpolate_triangle(shape.normals[t.x],
                              shape.normals[t.y], shape.normals[t.z], uv)));
    } else if (!shape.quads.empty()) {
      auto q = shape.quads[element];
      return transform_normal(instance.frame,
          normalize(interpolate_quad(shape.normals[q.x], shape.normals[q.y],
              shape.normals[q.z], shape.normals[q.w], uv)));
    } else if (!shape.lines.empty()) {
      auto l = shape.lines[element];
      return transform_normal(instance.frame,
          normalize(
              interpolate_line(shape.normals[l.x], shape.normals[l.y], uv.x)));
    } else if (!shape.points.empty()) {
      return transform_normal(
          instance.frame, normalize(shape.normals[shape.points[element]]));
    } else {
      return {0, 0, 0};
    }
  }

  // Eval texcoord
  vec2f eval_texcoord(const scene_data& scene, const instance_data& instance,
      int element, const vec2f& uv) {
    auto& shape = scene.shapes[instance.shape];
    if (shape.texcoords.empty()) return uv;
    if (!shape.triangles.empty()) {
      auto t = shape.triangles[element];
      return interpolate_triangle(
          shape.texcoords[t.x], shape.texcoords[t.y], shape.texcoords[t.z], uv);
    } else if (!shape.quads.empty()) {
      auto q = shape.quads[element];
      return interpolate_quad(shape.texcoords[q.x], shape.texcoords[q.y],
          shape.texcoords[q.z], shape.texcoords[q.w], uv);
    } else if (!shape.lines.empty()) {
      auto l = shape.lines[element];
      return interpolate_line(shape.texcoords[l.x], shape.texcoords[l.y], uv.x);
    } else if (!shape.points.empty()) {
      return shape.texcoords[shape.points[element]];
    } else {
      return zero2f;
    }
  }

#if 0
// Shape element normal.
static pair<vec3f, vec3f> eval_tangents(
    const trace_shape& shape, int element, const vec2f& uv) {
  if (!shape.triangles.empty()) {
    auto t = shape.triangles[element];
    if (shape.texcoords.empty()) {
      return triangle_tangents_fromuv(shape.positions[t.x],
          shape.positions[t.y], shape.positions[t.z], {0, 0}, {1, 0}, {0, 1});
    } else {
      return triangle_tangents_fromuv(shape.positions[t.x],
          shape.positions[t.y], shape.positions[t.z], shape.texcoords[t.x],
          shape.texcoords[t.y], shape.texcoords[t.z]);
    }
  } else if (!shape.quads.empty()) {
    auto q = shape.quads[element];
    if (shape.texcoords.empty()) {
      return quad_tangents_fromuv(shape.positions[q.x], shape.positions[q.y],
          shape.positions[q.z], shape.positions[q.w], {0, 0}, {1, 0}, {0, 1},
          {1, 1}, uv);
    } else {
      return quad_tangents_fromuv(shape.positions[q.x], shape.positions[q.y],
          shape.positions[q.z], shape.positions[q.w], shape.texcoords[q.x],
          shape.texcoords[q.y], shape.texcoords[q.z], shape.texcoords[q.w],
          uv);
    }
  } else {
    return {{0,0,0}, {0,0,0}};
  }
}
#endif

  // Shape element normal.
  pair<vec3f, vec3f> eval_element_tangents(
      const scene_data& scene, const instance_data& instance, int element) {
    auto& shape = scene.shapes[instance.shape];
    if (!shape.triangles.empty() && !shape.texcoords.empty()) {
      auto t        = shape.triangles[element];
      auto [tu, tv] = triangle_tangents_fromuv(shape.positions[t.x],
          shape.positions[t.y], shape.positions[t.z], shape.texcoords[t.x],
          shape.texcoords[t.y], shape.texcoords[t.z]);
      return {transform_direction(instance.frame, tu),
          transform_direction(instance.frame, tv)};
    } else if (!shape.quads.empty() && !shape.texcoords.empty()) {
      auto q        = shape.quads[element];
      auto [tu, tv] = quad_tangents_fromuv(shape.positions[q.x],
          shape.positions[q.y], shape.positions[q.z], shape.positions[q.w],
          shape.texcoords[q.x], shape.texcoords[q.y], shape.texcoords[q.z],
          shape.texcoords[q.w], {0, 0});
      return {transform_direction(instance.frame, tu),
          transform_direction(instance.frame, tv)};
    } else {
      return {};
    }
  }

  // Eval shading position
  vec3f eval_shading_position(const scene_data& scene,
      const instance_data& instance, int element, const vec2f& uv,
      const vec3f& outgoing) {
    auto& shape = scene.shapes[instance.shape];
    if (!shape.triangles.empty() || !shape.quads.empty()) {
      return eval_position(scene, instance, element, uv);
    } else if (!shape.lines.empty()) {
      return eval_position(scene, instance, element, uv);
    } else if (!shape.points.empty()) {
      return eval_position(shape, element, uv);
    } else {
      return {0, 0, 0};
    }
  }

  // Eval shading normal
  vec3f eval_shading_normal(const scene_data& scene,
      const instance_data& instance, int element, const vec2f& uv,
      const vec3f& outgoing) {
    auto& shape = scene.shapes[instance.shape];
    if (!shape.triangles.empty() || !shape.quads.empty()) {
      auto normal = eval_normal(scene, instance, element, uv);
      return dot(normal, outgoing) >= 0 ? normal : -normal;
    } else if (!shape.lines.empty()) {
      auto normal = eval_normal(scene, instance, element, uv);
      return orthonormalize(outgoing, normal);
    } else if (!shape.points.empty()) {
      return outgoing;
    } else {
      return {0, 0, 0};
    }
  }

  // Eval color
  vec4f eval_color(const scene_data& scene, const instance_data& instance,
      int element, const vec2f& uv) {
    auto& shape = scene.shapes[instance.shape];
    if (shape.colors.empty()) return {1, 1, 1, 1};
    if (!shape.triangles.empty()) {
      auto t = shape.triangles[element];
      return interpolate_triangle(
          shape.colors[t.x], shape.colors[t.y], shape.colors[t.z], uv);
    } else if (!shape.quads.empty()) {
      auto q = shape.quads[element];
      return interpolate_quad(shape.colors[q.x], shape.colors[q.y],
          shape.colors[q.z], shape.colors[q.w], uv);
    } else if (!shape.lines.empty()) {
      auto l = shape.lines[element];
      return interpolate_line(shape.colors[l.x], shape.colors[l.y], uv.x);
    } else if (!shape.points.empty()) {
      return shape.colors[shape.points[element]];
    } else {
      return {0, 0, 0, 0};
    }
  }

  int eval_color(const scene_data& scene, const instance_data& instance,
      int element, vec4f& color) {
    auto& shape = scene.shapes[instance.shape];
    if (shape.colors.empty()) return false;
    color = shape.colors[element];
    return true;
  }

  // Evaluate material
  material_point eval_material(const scene_data& scene,
      const instance_data& instance, int element, const vec2f& uv) {
    auto& material = scene.materials[instance.material];
    auto  texcoord = eval_texcoord(scene, instance, element, uv);

    // evaluate textures
    auto fill_tex   = eval_texture(scene, material.fill_tex, texcoord, true);
    auto stroke_tex = eval_texture(scene, material.stroke_tex, texcoord, true);

    auto color_shp = zero4f;

    // material point
    auto point   = material_point{};
    point.fill   = eval_color(scene, instance, element, color_shp)
                       ? color_shp
                       : material.fill * fill_tex;
    point.stroke = material.stroke * stroke_tex;

    return point;
  }

}  // namespace yocto

// -----------------------------------------------------------------------------
// SCENE UTILITIES
// -----------------------------------------------------------------------------
namespace yocto {

  // get camera
  int find_camera(const scene_data& scene, int id) {
    if (scene.cameras.empty()) return invalidid;
    return id;
  }

  // Updates the scene and scene's instances bounding boxes
  bbox3f compute_bounds(const scene_data& scene) {
    auto shape_bbox = vector<bbox3f>{};
    auto bbox       = invalidb3f;
    for (auto& shape : scene.shapes) {
      auto& sbvh = shape_bbox.emplace_back();
      for (auto p : shape.positions) sbvh = merge(sbvh, p);
    }
    for (auto& instance : scene.instances) {
      auto& sbvh = shape_bbox[instance.shape];
      bbox       = merge(bbox, transform_bbox(instance.frame, sbvh));
    }
    for (auto& label : scene.labels) {
      auto label_bbox = invalidb3f;
      for (auto p : label.positions) label_bbox = merge(label_bbox, p);
      bbox = merge(bbox, transform_bbox(label.frame, label_bbox));
    }
    return bbox;
  }

  vec3f intersect_plane(const ray3f& ray, const vec3f& p, const vec3f& n) {
    auto o = ray.o - p;

    auto den = dot(ray.d, n);
    if (den == 0) return vec3f{0, 0, 0};

    auto t = -dot(n, o) / den;

    auto q = ray.o + ray.d * t;

    return q;
  }

  void compute_borders(scene_data& scene) {
    for (auto& shape : scene.shapes) {
      shape.borders = vector<vec2i>{};
      if (!shape.triangles.empty()) {
        auto emap    = make_edge_map(shape.triangles);
        auto borders = shape.boundary ? get_boundary(emap) : get_edges(emap);
        shape.borders.insert(
            shape.borders.end(), borders.begin(), borders.end());
      }
      if (!shape.quads.empty()) {
        auto emap    = make_edge_map(shape.quads);
        auto borders = shape.boundary ? get_boundary(emap) : get_edges(emap);
        shape.borders.insert(
            shape.borders.end(), borders.begin(), borders.end());
      }
    }
  }

  void cull_shapes(scene_data& scene, int camera) {
    auto cam = scene.cameras[camera];

    for (auto& instance : scene.instances) {
      auto& shape = scene.shapes[instance.shape];
      if (!shape.triangles.empty()) {
        auto culled = vector<vec3i>{};
        if (shape.cull) {
          for (auto& triangle : shape.triangles) {
            auto p0 = transform_point(
                instance.frame, shape.positions[triangle.x]);
            auto p1 = transform_point(
                instance.frame, shape.positions[triangle.y]);
            auto p2 = transform_point(
                instance.frame, shape.positions[triangle.z]);
            auto fcenter = (p0 + p1 + p2) / 3;
            if (dot(cam.frame.o - fcenter, cross(p1 - p0, p2 - p0)) < 0)
              continue;
            culled.push_back(triangle);
          }
          shape.triangles = culled;
        }
      }
      if (!shape.quads.empty()) {
        auto culled = vector<vec4i>{};
        if (shape.cull) {
          for (auto& quad : shape.quads) {
            auto p0 = transform_point(instance.frame, shape.positions[quad.x]);
            auto p1 = transform_point(instance.frame, shape.positions[quad.y]);
            auto p2 = transform_point(instance.frame, shape.positions[quad.z]);
            auto p3 = transform_point(instance.frame, shape.positions[quad.w]);
            auto fcenter = (p0 + p1 + p2 + p3) / 4;
            if (dot(cam.frame.o - fcenter, cross(p1 - p0, p2 - p0)) < 0)
              continue;
            culled.push_back(quad);
          }
          shape.quads = culled;
        }
      }
    }
  }

  void compute_radius(scene_data& scene, int camera, vec2f size, float res) {
    auto cam = scene.cameras[camera];

    auto plane_point   = transform_point(cam.frame, {0, 0, cam.lens});
    auto plane_dir     = transform_normal(cam.frame, {0, 0, cam.lens});
    auto camera_origin = transform_point(cam.frame, {0, 0, 0});

    for (auto& instance : scene.instances) {
      auto& shape    = scene.shapes[instance.shape];
      auto& material = scene.materials[instance.material];
      shape.radius   = vector<float>{};
      for (auto& position : shape.positions) {
        if (cam.orthographic)
          shape.radius.push_back(material.thickness / (res * 2));
        else {
          auto thickness = material.thickness / size.x * cam.film / 2;

          auto p = transform_point(instance.frame, position);

          auto po = normalize(p - camera_origin);

          auto p_on_plane = intersect_plane(
              ray3f{camera_origin, po}, plane_point, plane_dir);

          auto trans_p = transform_point(inverse(cam.frame), p_on_plane);

          auto radius = 0.0f;

          auto trasl_p        = trans_p + vec3f{0, thickness, 0};
          auto traslated_p    = transform_point(cam.frame, trasl_p);
          auto translated_dir = normalize(traslated_p - camera_origin);
          auto new_p          = intersect_plane(
                       ray3f{camera_origin, translated_dir}, p, plane_dir);
          radius += distance(p, new_p);

          trasl_p        = trans_p + vec3f{0, -thickness, 0};
          traslated_p    = transform_point(cam.frame, trasl_p);
          translated_dir = normalize(traslated_p - camera_origin);
          new_p          = intersect_plane(
                       ray3f{camera_origin, translated_dir}, p, plane_dir);
          radius += distance(p, new_p);

          trasl_p        = trans_p + vec3f{thickness, 0, 0};
          traslated_p    = transform_point(cam.frame, trasl_p);
          translated_dir = normalize(traslated_p - camera_origin);
          new_p          = intersect_plane(
                       ray3f{camera_origin, translated_dir}, p, plane_dir);
          radius += distance(p, new_p);

          trasl_p        = trans_p + vec3f{-thickness, 0, 0};
          traslated_p    = transform_point(cam.frame, trasl_p);
          translated_dir = normalize(traslated_p - camera_origin);
          new_p          = intersect_plane(
                       ray3f{camera_origin, translated_dir}, p, plane_dir);
          radius += distance(p, new_p);

          radius /= 4;

          shape.radius.push_back(radius);
        }
      }
    }
  }

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

  void compute_text(
      scene_data& scene, int camera, int resolution, vec2f size, float res) {
    auto cam = scene.cameras[camera];

    auto width  = resolution;
    auto height = (int)round(resolution / cam.aspect);
    auto film   = vec2f{cam.film, cam.film / cam.aspect};
    if (cam.aspect < 1) {
      width  = (int)round(resolution * cam.aspect);
      height = resolution;
      film   = vec2f{cam.film * cam.aspect, cam.film};
    }

    auto plane_point   = transform_point(cam.frame, {0, 0, cam.lens});
    auto plane_dir     = transform_normal(cam.frame, {0, 0, cam.lens});
    auto camera_origin = transform_point(cam.frame, {0, 0, 0});

    http::Request request{"localhost:5500/rasterize"};

    for (auto& label : scene.labels) {
      for (auto idx = 0; idx < label.text.size(); idx++) {
        auto color = rgb_to_srgb(label.color);
        auto body  = "text=" + escape_string(label.text[idx]) +
                    "&width=" + to_string(width) +
                    "&height=" + to_string(height) +
                    "&film=" + to_string(film.x) +
                    "&align_x=" + to_string(label.alignment[idx].x) +
                    "&r=" + to_string((int)round(color.x * 255)) +
                    "&g=" + to_string((int)round(color.y * 255)) +
                    "&b=" + to_string((int)round(color.z * 255)) +
                    "&a=" + to_string((int)round(color.w * 255));
        auto response = request.send(
            "POST", body, {"Content-Type: application/x-www-form-urlencoded"});
        auto string64 = string{response.body.begin(), response.body.end()};
        auto buffer   = base64_decode(string64);

        auto texture = texture_data{};
        auto ncomp   = 0;
        auto pixels  = stbi_load_from_memory(buffer.data(), (int)buffer.size(),
             &texture.width, &texture.height, &ncomp, 4);
        texture.linear  = false;
        texture.pixelsb = vector<vec4b>{
            (vec4b*)pixels, (vec4b*)pixels + texture.width * texture.height};
        free(pixels);

        scene.textures.push_back(texture);

        auto shape = shape_data{};

        auto p = transform_point(label.frame, label.positions[idx]);

        auto offset   = label.offset[idx];
        auto baseline = 7.0f;

        auto align_x0 = 0.0f;
        auto align_x1 = 0.0f;
        auto align_x2 = 0.0f;
        auto align_x3 = 0.0f;

        if (label.alignment[idx].x > 0) {
          align_x0 = 1.0f;
          align_x1 = 0.0f;
          align_x2 = 0.0f;
          align_x3 = 1.0f;
        } else if (label.alignment[idx].x < 0) {
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

        if (cam.orthographic) {
          auto poff = p +
                      vec3f{offset.x / res, (-baseline - offset.y) / res, 0};
          p0 = poff -
               vec3f{align_x0 * size.x / res, align_y0 * size.y / res, 0};
          p1 = poff -
               vec3f{align_x1 * size.x / res, align_y1 * size.y / res, 0};
          p2 = poff -
               vec3f{align_x2 * size.x / res, align_y2 * size.y / res, 0};
          p3 = poff -
               vec3f{align_x3 * size.x / res, align_y3 * size.y / res, 0};
        } else {
          auto po = normalize(p - camera_origin);

          auto p_on_plane = intersect_plane(
              ray3f{camera_origin, po}, plane_point, plane_dir);

          auto trans_p = transform_point(inverse(cam.frame), p_on_plane);
          trans_p += vec3f{-offset.x / size.x * film.x,
              (baseline + offset.y) / size.x * film.x, 0};

          auto p0_on_plane = transform_point(cam.frame,
              trans_p + vec3f{align_x0 * film.x, align_y0 * film.y, 0});
          auto p1_on_plane = transform_point(cam.frame,
              trans_p + vec3f{align_x1 * film.x, align_y1 * film.y, 0});
          auto p2_on_plane = transform_point(cam.frame,
              trans_p + vec3f{align_x2 * film.x, align_y2 * film.y, 0});
          auto p3_on_plane = transform_point(cam.frame,
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

        shape.positions.push_back(p0);
        shape.positions.push_back(p1);
        shape.positions.push_back(p2);
        shape.positions.push_back(p3);
        shape.radius = vector<float>(4, 0);

        shape.quads.push_back(vec4i{0, 1, 2, 3});
        scene.shapes.push_back(shape);

        label.shapes.push_back((int)scene.shapes.size() - 1);
        label.textures.push_back((int)scene.textures.size() - 1);
      }
    }
  }

  void update_text_positions(scene_data& scene, int camera) {
    auto cam = scene.cameras[camera];

    auto film = vec2f{cam.film, cam.film / cam.aspect};
    if (cam.aspect < 1) {
      film = vec2f{cam.film * cam.aspect, cam.film};
    }

    auto plane_point   = transform_point(cam.frame, {0, 0, cam.lens});
    auto plane_dir     = transform_normal(cam.frame, {0, 0, cam.lens});
    auto camera_origin = transform_point(cam.frame, {0, 0, 0});

    for (auto& label : scene.labels) {
      for (auto idx = 0; idx < label.text.size(); idx++) {
        auto p = label.positions[idx];

        auto po = normalize(p - camera_origin);

        auto p_on_plane = intersect_plane(
            ray3f{camera_origin, po}, plane_point, plane_dir);

        auto trans_p = transform_point(inverse(cam.frame), p_on_plane);

        auto offset = label.offset[idx];
        trans_p += vec3f{-offset.x * film.x / 100, offset.y * film.y / 100, 0};

        auto p0_on_plane = transform_point(cam.frame, trans_p);
        auto p1_on_plane = transform_point(
            cam.frame, trans_p + vec3f{-film.x, 0, 0});
        auto p2_on_plane = transform_point(
            cam.frame, trans_p + vec3f{-film.x, film.y, 0});
        auto p3_on_plane = transform_point(
            cam.frame, trans_p + vec3f{0, film.y, 0});

        if (label.alignment[idx].x < 0) {
          p0_on_plane = transform_point(
              cam.frame, trans_p + vec3f{film.x, 0, 0});
          p1_on_plane = transform_point(cam.frame, trans_p);
          p2_on_plane = transform_point(
              cam.frame, trans_p + vec3f{0, film.y, 0});
          p3_on_plane = transform_point(
              cam.frame, trans_p + vec3f{film.x, film.y, 0});
        }

        auto p0 = intersect_plane(
            ray3f{camera_origin, p0_on_plane - camera_origin}, p, plane_dir);
        auto p1 = intersect_plane(
            ray3f{camera_origin, p1_on_plane - camera_origin}, p, plane_dir);
        auto p2 = intersect_plane(
            ray3f{camera_origin, p2_on_plane - camera_origin}, p, plane_dir);
        auto p3 = intersect_plane(
            ray3f{camera_origin, p3_on_plane - camera_origin}, p, plane_dir);

        auto& shape        = scene.shapes[label.shapes[idx]];
        shape.positions[0] = p0;
        shape.positions[1] = p1;
        shape.positions[2] = p2;
        shape.positions[3] = p3;
      }
    }
  }

  void update_text_textures(scene_data& scene, int camera, int res) {
    auto cam = scene.cameras[camera];

    auto width  = res;
    auto height = (int)round(res / cam.aspect);
    auto film   = vec2f{cam.film, cam.film / cam.aspect};
    if (cam.aspect < 1) {
      width  = (int)round(res * cam.aspect);
      height = res;
      film   = vec2f{cam.film * cam.aspect, cam.film};
    }

    http::Request request{"localhost:5500/rasterize"};

    for (auto& label : scene.labels) {
      for (auto idx = 0; idx < label.text.size(); idx++) {
        auto body = "text=" + label.text[idx] + "&width=" + to_string(width) +
                    "&height=" + to_string(height) +
                    "&film=" + to_string(film.x);
        auto response = request.send(
            "POST", body, {"Content-Type: application/x-www-form-urlencoded"});
        auto string64 = string{response.body.begin(), response.body.end()};
        auto buffer   = base64_decode(string64);

        auto texture = texture_data{};
        auto ncomp   = 0;
        auto pixels  = stbi_load_from_memory(buffer.data(), (int)buffer.size(),
             &texture.width, &texture.height, &ncomp, 4);
        texture.linear  = false;
        texture.pixelsb = vector<vec4b>{
            (vec4b*)pixels, (vec4b*)pixels + texture.width * texture.height};
        free(pixels);

        scene.textures[label.textures[idx]] = texture;
      }
    }
  }

}  // namespace yocto