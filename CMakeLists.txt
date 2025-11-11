cmake_minimum_required(VERSION 3.10)
project(yanhua C)

set(CMAKE_C_STANDARD 11)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/extern/quickjs)

add_compile_options(-Wno-unused-function -Werror -Wall)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/extern/SDL EXCLUDE_FROM_ALL)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/extern/plutovg EXCLUDE_FROM_ALL)

add_executable(yanhua main.c)

include(ExternalProject)

ExternalProject_Add(quickjs_ep
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/quickjs"
  CONFIGURE_COMMAND ""                        # quickjs 使用 Makefile，无 configure 步骤
  BUILD_COMMAND ${CMAKE_MAKE_PROGRAM}         # 或 "make"
  BUILD_IN_SOURCE 1
  INSTALL_COMMAND ""                          # 可选：如果需要安装到 prefix，则填 install 命令
  # 如果 make 会生成 libquickjs.a 到某路径，可声明为 BYPRODUCTS（可选）
  # BUILD_BYPRODUCTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/quickjs/libquickjs.a"
)

# 等待 quickjs 编译后再链接
add_library(quickjs STATIC IMPORTED GLOBAL)
set(QUICKJS_LIB "${CMAKE_CURRENT_SOURCE_DIR}/extern/quickjs/libquickjs.a") # 根据实际产物调整路径/名称
set_target_properties(quickjs PROPERTIES IMPORTED_LOCATION "${QUICKJS_LIB}")
add_dependencies(quickjs quickjs_ep)

target_include_directories(quickjs INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/extern/quickjs")

target_link_libraries(yanhua PRIVATE quickjs m SDL3::SDL3 plutovg)
