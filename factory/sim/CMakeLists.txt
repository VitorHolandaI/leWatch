cmake_minimum_required(VERSION 3.10)
project(twatch_sim C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

set(ROOT  ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(LVGL  ${CMAKE_CURRENT_SOURCE_DIR}/../../libraries/lvgl)

# LVGL sources (whole library)
file(GLOB_RECURSE LVGL_SRC ${LVGL}/src/*.c)

# Project UI + image/font assets (portable LVGL code)
file(GLOB UI_SRC  ${ROOT}/ui_*.cpp)
file(GLOB IMG_SRC ${ROOT}/src/*.c)

add_executable(sim
  ${ROOT}/main.cpp
  ${ROOT}/hal_interface.cpp
  ${ROOT}/app_nfc.cpp
  ${UI_SRC}
  ${IMG_SRC}
  ${LVGL_SRC}
)

target_include_directories(sim PRIVATE
  ${ROOT}
  ${LVGL}
  ${CMAKE_CURRENT_SOURCE_DIR}      # sim/lv_conf.h
  ${SDL2_INCLUDE_DIRS}
)

# LV_CONF_INCLUDE_SIMPLE -> lvgl.h includes "lv_conf.h" from include path (sim/).
# ARDUINO intentionally NOT defined -> hardware code compiles as PC stubs.
target_compile_definitions(sim PRIVATE
  LV_CONF_INCLUDE_SIMPLE
  SDL_HOR_RES=240
  SDL_VER_RES=240
)

target_link_libraries(sim ${SDL2_LIBRARIES})
