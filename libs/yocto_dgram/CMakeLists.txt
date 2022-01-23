add_library(yocto_dgram STATIC
  yocto_dgram.h yocto_dgram.cpp
  yocto_dgramio.h yocto_dgramio.cpp 
  yocto_dgram_bvh.h yocto_dgram_bvh.cpp
  yocto_dgram_trace.h yocto_dgram_trace.cpp
  yocto_dgram_shape.h yocto_dgram_shape.cpp
  yocto_dgram_text.h yocto_dgram_text.cpp
  ext/base64.h ext/base64.cpp
  ext/HTTPRequest.hpp
  ext/earcut.hpp
)

set_target_properties(yocto_dgram PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
target_include_directories(yocto_dgram PRIVATE ext/)
target_include_directories(yocto_dgram PUBLIC ${CMAKE_SOURCE_DIR}/libs)

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