cmake_minimum_required(VERSION 3.10)
project(real_world_overlay)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-video-1.0)

include_directories(
  ${GST_INCLUDE_DIRS}
  /usr/local/cuda/include
  /opt/nvidia/deepstream/deepstream-7.1/sources/includes
  ${CMAKE_SOURCE_DIR}/include   # for toml.hpp
)

link_directories(
  ${GST_LIBRARY_DIRS}
  /usr/local/cuda/lib64
  /opt/nvidia/deepstream/deepstream-7.1/lib
)

add_executable(real_world_overlay main.cpp)

target_link_libraries(real_world_overlay
  ${GST_LIBRARIES}
  cuda
  cudart
  nvdsgst_meta
  nvds_meta
  nvds_infer
  nvinfer
  nvds_utils
  gobject-2.0
  glib-2.0
  pthread
  dl
)
