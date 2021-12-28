add_library(yocto_dgram STATIC
  yocto_math.h yocto_color.h yocto_geometry.h
  yocto_noise.h yocto_sampling.h
  yocto_modelio.h yocto_modelio.cpp
  yocto_bvh.h yocto_bvh.cpp
  yocto_shape.h yocto_shape.cpp
  yocto_image.h yocto_image.cpp
  yocto_scene.h yocto_scene.cpp
  yocto_trace.h yocto_trace.cpp
  yocto_sceneio.h yocto_sceneio.cpp
  yocto_shading.h
  yocto_gui.h yocto_gui.cpp
  yocto_parallel.h yocto_cli.h
  ext/stb_image.h ext/stb_image_resize.h ext/stb_image_write.h ext/stb_image.cpp
  ext/tinyexr.h ext/tinyexr.cpp ext/json.hpp ext/fast_float.h ext/base64.h ext/base64.cpp
  ext/HTTPRequest.hpp
)

set_target_properties(yocto_dgram PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
target_include_directories(yocto_dgram PRIVATE ext/)

if(UNIX AND NOT APPLE)
  find_package(Threads REQUIRED)
  target_link_libraries(yocto_dgram PUBLIC Threads::Threads)
endif(UNIX AND NOT APPLE)

if(YOCTO_OPENGL)
  target_compile_definitions(yocto_dgram PUBLIC -DYOCTO_OPENGL)
  find_package(OpenGL REQUIRED)
  target_link_libraries(yocto_dgram PUBLIC glad imgui glfw ${OPENGL_gl_LIBRARY})
endif(YOCTO_OPENGL)

# warning flags
if(APPLE)
  target_compile_options(yocto_dgram PUBLIC -Wall -Wconversion -Wno-sign-conversion -Wno-implicit-float-conversion)
endif(APPLE)
if(MSVC)
  # target_compile_options(yocto PUBLIC /D_CRT_SECURE_NO_WARNINGS /wd4018 /wd4244 /wd4305 /wd4800 /wd4267)
  target_compile_options(yocto_dgram PUBLIC /D_CRT_SECURE_NO_WARNINGS)
  target_compile_options(yocto_dgram PUBLIC /EHsc)
endif(MSVC)