cmake_minimum_required(VERSION 3.17)
project(projekt_fat C)

set(CMAKE_C_STANDARD 11)

add_compile_options(
        "-ggdb3"
        "-Wall"
        "-Wno-parentheses"
        "-DINSIDE_DANTE"
        "-Werror=vla"
        "-Wno-error=parentheses"
        "-pedantic"
        "-D_ANSI_OUTPUT"
        "-Wno-error=implicit-fallthrough"
        "-D_GNU_SOURCE"
        "-Wextra"
        "-Werror"
        "-fdiagnostics-color"
        "-fmax-errors=5"
        "-Wno-error=unused-parameter"
        "-xc"
        "-std=c11"

)

add_link_options(
        "-ggdb3"
        "-fuse-ld=gold"
        "-Wl,-Map=main.map"
        "-Wl,-cref"
        "-Wl,-wrap,main"
)

#add_executable(projekt_fat main.c file_reader.c file_reader.h)
add_executable(project_fat
        "main.c"
        "file_reader.c"
        "unit_helper_v2.c"
        "unit_test_v2.c"
        "rdebug.c"
    )
