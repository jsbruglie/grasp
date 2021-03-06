# 2.8.8 required to use PROTOBUF_IMPORT_DIRS
cmake_minimum_required(VERSION 2.8.8 FATAL_ERROR)
FIND_PACKAGE( Boost COMPONENTS system REQUIRED )
set (CMAKE_CXX_FLAGS "-g -Wall -std=c++11")

# Find packages
include (FindPkgConfig)
if (PKG_CONFIG_FOUND)
  pkg_check_modules(GAZEBO gazebo)
  pkg_check_modules(SDF sdformat)
endif()

find_package(yaml-cpp REQUIRED)

# Include and link Gazebo and SDF libraries
include_directories(${GAZEBO_INCLUDE_DIRS} ${SDF_INCLUDE_DIRS})
link_directories(${GAZEBO_LIBRARY_DIRS})

# Include and link yaml-cpp library
include_directories(${YAML_CPP_INCLUDE_DIR})
link_directories(${YAML_CPP_INCLUDE_DIR})

# Include custom messages
include_directories(${PROJECT_BINARY_DIR}/msgs)
link_directories(${PROJECT_BINARY_DIR}/msgs)
# Include custom utilities
include_directories(${PROJECT_BINARY_DIR}/utils)
link_directories(${PROJECT_BINARY_DIR}/utils)

# Shared library for hand plugin interface
add_library(grasp_interface SHARED
    GraspShape.cc
    Interface.cc
    utils.cc
)
target_link_libraries(grasp_interface
    grasp_msgs)
add_dependencies(grasp_interface
    grasp_msgs)

# Configure interface header
configure_file (${PROJECT_SOURCE_DIR}/utils/GraspShape.hh
  ${CMAKE_CURRENT_BINARY_DIR}/GraspShape.hh )
configure_file (${PROJECT_SOURCE_DIR}/utils/Interface.hh
  ${CMAKE_CURRENT_BINARY_DIR}/Interface.hh )
configure_file (${PROJECT_SOURCE_DIR}/utils/utils.hh
  ${CMAKE_CURRENT_BINARY_DIR}/utils.hh )

# Configure header-only debug utilities
configure_file (${PROJECT_SOURCE_DIR}/utils/debug.hh.in
  ${CMAKE_CURRENT_BINARY_DIR}/debug.hh )

