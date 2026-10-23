# A2 Racer II for PS5: upstream's race (a2view), menu (a2menu) and format core (a2core) as one title, with the PS5
# layer in ps5/. Configured by build.sh with ps5/ps5-gl.cmake as CMAKE_PROJECT_INCLUDE_BEFORE (decision log D1, D3).
cmake_minimum_required(VERSION 3.16)
project(a2racer2_ps5 C CXX)

set(UPSTREAM "${CMAKE_CURRENT_SOURCE_DIR}/src-upstream/port")
if(NOT EXISTS "${UPSTREAM}/CMakeLists.txt")
  message(FATAL_ERROR "src-upstream/ is missing: run tools/setup-source.sh")
endif()

# The same sources as upstream's three targets, once each (as port/web/build.sh does).
set(A2_SOURCES
  # a2core
  src/a2_io.c src/cars.c src/archive.c src/mesh.c src/track.c
  # a2view: the race
  src/main.c src/audio.c src/drive.c src/smoke.c src/skid.c src/paths.c src/anims.c src/font.c src/font3d.c
  src/gfx.c src/lang.c src/handling.c src/racers.c src/wheels.c src/contact.c src/tweak.c src/sky.c
  src/trackinfo.c src/results.c src/sprites.c src/cops.c src/traffic.c src/ai_lane.c src/ai_car.c src/ai_world.c
  src/render.c src/pcx.c src/tga.c
  # a2menu: the front end
  src/menu/a2menu.c src/menu/m_screens.c src/menu/m_garage.c src/menu/m_showroom.c src/menu/m_standings.c
  src/menu/m_options.c src/menu/m_controls.c src/menu/m_newplayer.c src/menu/maudio.c src/menu/msmacker.c
  src/menu/mui.c src/menu/massets.c src/menu/mcareer.c src/menu/mdata.c src/menu/mgfx.c)
list(TRANSFORM A2_SOURCES PREPEND "${UPSTREAM}/")

set(PS5_SOURCES ps5/a2_ps5_main.c ps5/a2_ps5_gl.c ps5/a2_ps5_disc.c ps5/a2_ps5_screen.c)

add_executable(a2racer2 ${A2_SOURCES} ${PS5_SOURCES} ${PROSPERO_SUPPORT_SOURCES})
set_source_files_properties(${A2_SOURCES} ${PS5_SOURCES} PROPERTIES COMPILE_OPTIONS "-std=gnu99")
target_compile_definitions(a2racer2 PRIVATE A2_PS5)
target_compile_options(a2racer2 PRIVATE -Wall -Wno-unused-parameter)
target_include_directories(a2racer2 PRIVATE "${UPSTREAM}/src" "${UPSTREAM}/src/menu" ps5)
target_link_libraries(a2racer2 PRIVATE SDL2::SDL2-static OpenGL::GL m)

# SDL's GL half goes to the EGL Compatibility context (ps5/a2_ps5_gl.c).
set(A2_GL_WRAPS
  -Wl,--wrap=SDL_CreateWindow -Wl,--wrap=SDL_GL_CreateContext -Wl,--wrap=SDL_GL_DeleteContext
  -Wl,--wrap=SDL_GL_MakeCurrent -Wl,--wrap=SDL_GL_SwapWindow -Wl,--wrap=SDL_GL_GetDrawableSize
  -Wl,--wrap=SDL_GL_SetAttribute -Wl,--wrap=SDL_GL_SetSwapInterval)
string(REPLACE ";" " " A2_GL_WRAPS_STR "${A2_GL_WRAPS}")
set_property(TARGET a2racer2 APPEND_STRING PROPERTY LINK_FLAGS " ${A2_GL_WRAPS_STR} ${PS5_FINAL_LINK_FLAGS}")
