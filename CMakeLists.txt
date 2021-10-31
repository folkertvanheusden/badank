cmake_minimum_required(VERSION 3.7)

project(baduck)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_compile_options(-Wall)

add_executable(
  badank
  error.cpp
  gtp.cpp
  main.cpp
  proc.cpp
  str.cpp
  time.cpp
)

add_executable(
  badank_test
  error.cpp
  gtp.cpp
  proc.cpp
  str.cpp
  test.cpp
  time.cpp
)

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)
target_link_libraries(badank Threads::Threads)
