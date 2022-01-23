add_executable(dgram  dgram.cpp)

set_target_properties(dgram  PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
target_include_directories(dgram  PRIVATE ${CMAKE_SOURCE_DIR}/libs)
target_link_libraries(dgram PRIVATE yocto_dgram)
target_link_libraries(dgram PRIVATE yocto)