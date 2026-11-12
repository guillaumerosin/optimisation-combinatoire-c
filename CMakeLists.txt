cmake_minimum_required(VERSION 3.16)
project(jsp_pso LANGUAGES C)

option(JSP_NATIVE "Enable native CPU optimizations" OFF)
option(JSP_BUILD_TESTS "Build tests" ON)
option(JSP_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(MSVC)
  message(FATAL_ERROR "Utiliser GCC ou Clang (MinGW-w64 sous Windows) : "
                      "le projet a besoin de pthreads et de <stdatomic.h>.")
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_library(jsp_core
    src/instance.c
    src/schedule.c
    src/pso.c
    src/platform.c
)
target_include_directories(jsp_core PUBLIC src)
target_link_libraries(jsp_core PUBLIC Threads::Threads m)
target_compile_options(jsp_core PRIVATE -O3 -Wall -Wextra -Wpedantic)
if(JSP_NATIVE)
  target_compile_options(jsp_core PRIVATE -march=native)
endif()
if(JSP_SANITIZE)
  target_compile_options(jsp_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(jsp_core PUBLIC -fsanitize=address,undefined)
endif()

add_executable(jsp_pso src/main.c)
target_link_libraries(jsp_pso PRIVATE jsp_core)

if(JSP_BUILD_TESTS)
  enable_testing()
  add_executable(jsp_tests tests/test_core.c)
  target_link_libraries(jsp_tests PRIVATE jsp_core)
  add_test(NAME jsp_tests COMMAND jsp_tests)
endif()
