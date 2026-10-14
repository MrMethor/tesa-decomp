cmake_minimum_required(VERSION 3.16)
project(arena_decomp C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(MSVC)
  add_compile_options(/W4 /D_CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra)
endif()

# Reconstructed game code, as a library the game and tools link against.
add_library(arena STATIC
  src/fs/afile.c
  src/fs/exedata.c
  src/gfx/compress.c
  src/gfx/img.c
  src/gfx/palette.c
  src/gfx/dfa.c
  src/gfx/cfa.c
  src/gfx/font.c
  src/world/mif.c
  src/world/inf.c
  src/world/rmd.c
  src/media/voc.c
  src/media/flc.c
  src/media/xmi.c
  src/data/text.c
  src/data/tables.c
  src/data/save.c
)
target_include_directories(arena PUBLIC src)

# Engine: new code for the port, written from the behaviour notes in docs/.
add_library(engine STATIC
  src/engine/consts.c
  src/engine/level.c
  src/engine/player.c
  src/engine/scenario.c
  src/engine/game.c
)
target_include_directories(engine PUBLIC src)
target_link_libraries(engine PUBLIC arena)

# Headless scenario runner for differential tests (tools/statecmp.py).
add_executable(arenarun src/port/arenarun.c)
target_link_libraries(arenarun PRIVATE engine)

add_executable(bsatool src/tools/bsatool.c)
target_link_libraries(bsatool PRIVATE arena)

add_executable(imgtool src/tools/imgtool.c src/tools/png.c)
target_link_libraries(imgtool PRIVATE arena)

add_executable(maptool src/tools/maptool.c)
target_link_libraries(maptool PRIVATE arena)

add_executable(soundtool src/tools/soundtool.c)
target_link_libraries(soundtool PRIVATE arena)

add_executable(datatool src/tools/datatool.c)
target_link_libraries(datatool PRIVATE arena)
