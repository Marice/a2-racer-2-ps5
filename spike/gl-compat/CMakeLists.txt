# A2 Racer II for PS5 - Phase 0: the Compatibility-profile check as a title (decision log D1).
cmake_minimum_required(VERSION 3.16)
project(a2_gl_compat C CXX)

add_executable(a2_gl_compat main.c ${PROSPERO_SUPPORT_SOURCES})
target_link_libraries(a2_gl_compat PRIVATE OpenGL::GL)
set_property(TARGET a2_gl_compat APPEND_STRING PROPERTY LINK_FLAGS " ${PS5_FINAL_LINK_FLAGS}")
