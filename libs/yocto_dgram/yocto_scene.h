//
// # Yocto/Scene: Scene reepresentation
//
// Yocto/Scene defines a simple scene representation, and related utilities,
// mostly geared towards scene creation and serialization. Scene serialization
// is implemented in Yocto/SceneIO.
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

#ifndef _YOCTO_SCENE_H_
#define _YOCTO_SCENE_H_

// -----------------------------------------------------------------------------
// INCLUDES
// -----------------------------------------------------------------------------

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "yocto_geometry.h"
#include "yocto_image.h"
#include "yocto_math.h"
#include "yocto_shape.h"

// -----------------------------------------------------------------------------
// USING DIRECTIVES
// -----------------------------------------------------------------------------
namespace yocto {

  // using directives
  using std::pair;
  using std::string;
  using std::vector;

}  // namespace yocto

// -----------------------------------------------------------------------------
// SCENE DATA
// -----------------------------------------------------------------------------
namespace yocto {

  // Handles to refer to scene elements
  inline const int invalidid = -1;

  // Camera based on a simple lens model. The camera is placed using a frame.
  // Camera projection is described in photographic terms. In particular,
  // we specify film size (35mm by default), film aspect ration,
  // the lens' focal length, the focus distance and the lens aperture.
  // All values are in meters. Here are some common aspect ratios used in video
  // and still photography.
  // 3:2    on 35 mm:  0.036 x 0.024
  // 16:9   on 35 mm:  0.036 x 0.02025 or 0.04267 x 0.024
  // 2.35:1 on 35 mm:  0.036 x 0.01532 or 0.05640 x 0.024
  // 2.39:1 on 35 mm:  0.036 x 0.01506 or 0.05736 x 0.024
  // 2.4:1  on 35 mm:  0.036 x 0.015   or 0.05760 x 0.024 (approx. 2.39 : 1)
  // To compute good apertures, one can use the F-stop number from photography
  // and set the aperture to focal length over f-stop.
  struct camera_data {
    frame3f frame        = identity3x4f;
    bool    orthographic = false;
    float   lens         = 0.050f;
    float   film         = 0.036f;
    float   aspect       = 1.500f;
    float   focus        = 10000;
    float   aperture     = 0;
  };

  // Texture data as array of float or byte pixels. Textures can be stored in
  // linear or non linear color space.
  struct texture_data {
    int           width   = 0;
    int           height  = 0;
    bool          linear  = false;
    vector<vec4f> pixelsf = {};
    vector<vec4b> pixelsb = {};
  };

  // Material for surfaces, lines and triangles.
  struct material_data {
    // material
    vec4f fill   = {0, 0, 0, 1};
    vec4f stroke = {0, 0, 0, 1};

    // textures
    int fill_tex   = invalidid;
    int stroke_tex = invalidid;

    float thickness = 2;
  };

  struct label_data {
    frame3f        frame     = identity3x4f;
    vec4f          color     = {0, 0, 0, 1};
    vector<vec3f>  positions = {};
    vector<string> text      = {};
    vector<vec2f>  offset    = {};
    vector<vec2f>  alignment = {};
    vector<int>    shapes    = {};
    vector<int>    textures  = {};
  };

  // Instance.
  struct instance_data {
    // instance data
    frame3f frame    = identity3x4f;
    int     shape    = invalidid;
    int     material = invalidid;
    int     labels   = invalidid;
  };

  // Scene comprised an array of objects whose memory is owened by the scene.
  // All members are optional,Scene objects (camera, instances) have transforms
  // defined internally. A scene can optionally contain a node hierarchy where
  // each node might point to a camera or instance, In that case, the element
  // transforms are computed from the hierarchy. Animation is also optional,
  // with keyframe data that updates node transformations only if defined.
  struct scene_data {
    vec2f                 offset    = {0, 0};
    vector<camera_data>   cameras   = {};
    vector<instance_data> instances = {};
    vector<material_data> materials = {};
    vector<shape_data>    shapes    = {};
    vector<texture_data>  textures  = {};
    vector<label_data>    labels    = {};
  };

}  // namespace yocto

// -----------------------------------------------------------------------------
// CAMERA PROPERTIES
// -----------------------------------------------------------------------------
namespace yocto {

  // Generates a ray from a camera.
  ray3f eval_camera(
      const camera_data& camera, const vec2f& image_uv, const vec2f& lens_uv);

}  // namespace yocto

// -----------------------------------------------------------------------------
// TEXTURE PROPERTIES
// -----------------------------------------------------------------------------
namespace yocto {

  // Evaluates a texture
  vec4f eval_texture(const texture_data& texture, const vec2f& uv,
      bool as_linear = false, bool no_interpolation = false,
      bool clamp_to_edge = false);
  vec4f eval_texture(const scene_data& scene, int texture, const vec2f& uv,
      bool as_linear = false, bool no_interpolation = false,
      bool clamp_to_edge = false);

  // pixel access
  vec4f lookup_texture(
      const texture_data& texture, int i, int j, bool as_linear = false);

  // conversion from image
  texture_data image_to_texture(const image_data& image);

}  // namespace yocto

// -----------------------------------------------------------------------------
// MATERIAL PROPERTIES
// -----------------------------------------------------------------------------
namespace yocto {

  // Material parameters evaluated at a point on the surface
  struct material_point {
    vec4f fill   = {0, 0, 0, 1};
    vec4f stroke = {0, 0, 0, 1};
  };

  // Eval material to obtain emission, brdf and opacity.
  material_point eval_material(const scene_data& scene,
      const material_data& material, const vec2f& texcoord);

}  // namespace yocto

// -----------------------------------------------------------------------------
// INSTANCE PROPERTIES
// -----------------------------------------------------------------------------
namespace yocto {

  // Evaluate instance properties
  vec3f eval_position(const scene_data& scene, const instance_data& instance,
      int element, const vec2f& uv);
  vec3f eval_element_normal(
      const scene_data& scene, const instance_data& instance, int element);
  vec3f eval_normal(const scene_data& scene, const instance_data& instance,
      int element, const vec2f& uv);
  vec2f eval_texcoord(const scene_data& scene, const instance_data& instance,
      int element, const vec2f& uv);
  pair<vec3f, vec3f> eval_element_tangents(
      const scene_data& scene, const instance_data& instance, int element);
  vec3f eval_normalmap(const scene_data& scene, const instance_data& instance,
      int element, const vec2f& uv);
  vec3f eval_shading_position(const scene_data& scene,
      const instance_data& instance, int element, const vec2f& uv,
      const vec3f& outgoing);
  vec3f eval_shading_normal(const scene_data& scene,
      const instance_data& instance, int element, const vec2f& uv,
      const vec3f& outgoing);
  vec4f eval_color(const scene_data& scene, const instance_data& instance,
      int element, const vec2f& uv);

  // Eval material to obtain emission, brdf and opacity.
  material_point eval_material(const scene_data& scene,
      const instance_data& instance, int element, const vec2f& uv);

}  // namespace yocto

// -----------------------------------------------------------------------------
// SCENE UTILITIES
// -----------------------------------------------------------------------------
namespace yocto {

  // compute scene bounds
  bbox3f compute_bounds(const scene_data& scene);

  // get camera
  int find_camera(const scene_data& scene, int id);

  void cull_shapes(scene_data& scene, int camera);
  void compute_borders(scene_data& scene);
  void compute_radius(scene_data& scene, int camera, vec2f size, float res);
  void compute_text(
      scene_data& scene, int camera, int resolution, vec2f size, float res);
  void update_text_positions(scene_data& scene, int camera);
  void update_text_textures(scene_data& scene, int camera, int res);

}  // namespace yocto

#endif
