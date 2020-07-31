cmake_minimum_required(VERSION 3.5)

set(PLAYER_TARGET "IAVFExtract")

project(${PLAYER_TARGET} LANGUAGES C)

add_executable(${PLAYER_TARGET} main.c)

target_compile_options(${PLAYER_TARGET} PRIVATE -Werror -Wall)
