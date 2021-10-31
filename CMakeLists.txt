cmake_minimum_required(VERSION 3.7)

project(baduck)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_compile_options(-Wall)

add_executable(
  badank
  error.cpp
  proc.cpp
  str.cpp
  time.cpp
)

add_executable(
  badank_test
  error.cpp
  proc.cpp
  str.cpp
  test.cpp
  time.cpp
)
