# (C) 2021-2023 by Folkert van Heusden <mail@vanheusden.com>
# Released under MIT license

cmake_minimum_required(VERSION 3.7)

project(baduck)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_compile_options(-Wall -pedantic)

include_directories("Glicko2")

set(CMAKE_BUILD_TYPE Debug)

add_executable(
  badank
  error.cpp
  gtp.cpp
  log.cpp
  main.cpp
  proc.cpp
  sgf.cpp
  str.cpp
  time.cpp
  Glicko2/glicko/rating.cpp
)

set(CMAKE_BUILD_TYPE Debug)

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)
target_link_libraries(badank Threads::Threads)

include(FindPkgConfig)

pkg_check_modules(LIBCONFIG REQUIRED libconfig++)
target_link_libraries(badank ${LIBCONFIG_LIBRARIES})
target_include_directories(badank PUBLIC ${LIBCONFIG_INCLUDE_DIRS})
target_compile_options(badank PUBLIC ${LIBCONFIG_CFLAGS_OTHER})
