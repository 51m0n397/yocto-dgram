//
// # Yocto/Dgram: Diagram representation
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

#include "yocto_dgram.h"

// -----------------------------------------------------------------------------
// USING DIRECTIVES
// -----------------------------------------------------------------------------
namespace yocto {

  // using directives

}  // namespace yocto

// -----------------------------------------------------------------------------
// DGRAM SCENES
// -----------------------------------------------------------------------------
namespace yocto {

  ray3f eval_camera(const dgram_camera& camera, const vec2f& image_uv,
      const vec2f& size, const float& scale) {
    auto aspect = size.x / size.y;
    auto film   = aspect >= 1 ? vec2f{camera.film, camera.film / aspect}
                              : vec2f{camera.film * aspect, camera.film};

    auto frame   = lookat_frame(camera.from, camera.to, {0, 1, 0});
    auto lens    = camera.lens / size.x * scale;
    auto centerx = camera.center.x * scale / (size.x);
    auto centery = camera.center.y * scale / (size.y);

    if (!camera.orthographic) {
      auto q = vec3f{film.x * (0.5f - image_uv.x - centerx),
          film.y * (image_uv.y - 0.5f - centery), lens};
      auto e = zero3f;
      auto d = normalize(-q - e);
      return ray3f{transform_point(frame, e), transform_direction(frame, d)};
    } else {
      auto s = length(camera.from - camera.to) / lens;
      auto q = vec3f{film.x * (0.5f - image_uv.x - centerx) * s,
          film.y * (image_uv.y - 0.5f - centery) * s, lens};
      auto e = vec3f{-q.x, -q.y, 0};
      auto d = normalize(-q - e);
      return ray3f{transform_point(frame, e), transform_direction(frame, d)};
    }
  }

}  // namespace yocto
