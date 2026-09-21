cmake_minimum_required(VERSION 3.20)

# Configure only the C language. The project does not invoke a C++ compiler.
# The include/dt.h header also rejects C++ compilation.
project(cmsc_124_ps1_data_types LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}")

option(DT_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

add_executable(cmsc-124-ps1
    src/main.c
    src/driver.c
    src/print.c
    src/dt_value.c
    src/dt_int.c
    src/dt_str.c
    src/dt_enum.c
    src/dt_array.c
    src/dt_map.c
    src/dt_record.c
    src/dt_tuple.c
    src/dt_list.c
    src/dt_ref.c
)

target_include_directories(cmsc-124-ps1 PRIVATE include)

if(NOT MSVC)
    target_compile_options(cmsc-124-ps1 PRIVATE -Wall -Wextra)
endif()

if(DT_SANITIZE)
    target_compile_options(cmsc-124-ps1 PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer -g)
    target_link_options(cmsc-124-ps1 PRIVATE -fsanitize=address,undefined)
endif()
