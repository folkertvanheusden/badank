cmake_minimum_required(VERSION 3.7)

project(baduck)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_compile_options(-Wall)

include_directories("Glicko2")

add_executable(
  badank
  error.cpp
  gtp.cpp
  log.cpp
  main.cpp
  proc.cpp
  str.cpp
  time.cpp
  Glicko2/glicko/rating.cpp
)

add_executable(
  badank_test
  error.cpp
  gtp.cpp
  log.cpp
  proc.cpp
  str.cpp
  test.cpp
  time.cpp
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
