# apps/real_world_overlay/CMakeLists.txt
cmake_minimum_required(VERSION 3.10)
project(real_world_overlay)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-video-1.0)

include_directories(${GST_INCLUDE_DIRS})
link_directories(${GST_LIBRARY_DIRS})

add_executable(real_world_overlay main.cpp)
target_include_directories(real_world_overlay PRIVATE /opt/nvidia/deepstream/deepstream/sources/includes)
target_link_libraries(real_world_overlay ${GST_LIBRARIES} dl pthread)
