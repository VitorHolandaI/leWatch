cmake_minimum_required(VERSION 3.10)
project(twatch_sim C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

set(ROOT  ${CMAKE_CURRENT_SOURCE_DIR}/..)
# Repo: factory/libraries/lvgl (symlink). Arduino: $HOME/Arduino/libraries/lvgl.
if(EXISTS ${ROOT}/libraries/lvgl)
  set(LVGL ${ROOT}/libraries/lvgl)
else()
  set(LVGL ${CMAKE_CURRENT_SOURCE_DIR}/../../libraries/lvgl)
endif()

# LVGL sources (whole library)
file(GLOB_RECURSE LVGL_SRC ${LVGL}/src/*.c)

# Project UI + image/font assets (portable LVGL code).
# GLOB_RECURSE: as fontes moram em src/font/*.c (o Makefile usa find, recursivo).
file(GLOB UI_SRC  ${ROOT}/ui_*.cpp)
file(GLOB_RECURSE IMG_SRC ${ROOT}/src/*.c)

add_executable(sim
  ${ROOT}/main.cpp
  ${ROOT}/hal_interface.cpp
  ${ROOT}/app_nfc.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs.cpp   # fakes de HW (radio...), como no Makefile
  ${UI_SRC}
  ${IMG_SRC}
  ${LVGL_SRC}
)

target_include_directories(sim PRIVATE
  ${ROOT}
  ${LVGL}
  ${LVGL}/src                      # LVGL 9: main.cpp inclui "drivers/sdl/..."
  ${CMAKE_CURRENT_SOURCE_DIR}      # sim/lv_conf.h
  ${SDL2_INCLUDE_DIRS}
)

# -include compat.h: mesmo force-include do sim/Makefile (itoa() do Arduino).
target_compile_options(sim PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/compat.h)

# LV_CONF_INCLUDE_SIMPLE -> lvgl.h includes "lv_conf.h" from include path (sim/).
# ARDUINO_T_WATCH_S3/USING_IR_REMOTE: escolhem a variante de placa no
# hal_interface.h (define MAIN_FONT, USING_TOUCHPAD...) — sem eles nao compila.
target_compile_definitions(sim PRIVATE
  LV_CONF_INCLUDE_SIMPLE
  SDL_HOR_RES=240
  SDL_VER_RES=240
  ARDUINO_T_WATCH_S3
  USING_IR_REMOTE
)

target_link_libraries(sim ${SDL2_LIBRARIES})
