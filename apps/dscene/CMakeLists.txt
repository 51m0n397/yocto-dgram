add_executable(dscene  dscene.cpp)

set_target_properties(dscene  PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
target_include_directories(dscene  PRIVATE ${CMAKE_SOURCE_DIR}/libs)
target_link_libraries(dscene PRIVATE yocto_dgram)