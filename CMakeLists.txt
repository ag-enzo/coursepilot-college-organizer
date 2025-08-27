cmake_minimum_required(VERSION 3.22)
project(CoursePilotSingle LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

option(CP_USE_SANITIZERS "Enable ASan/UBSan in Debug" ON)
if (CP_USE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

set(QT_MIN_VERSION "6.5.0")
find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Widgets Sql)
set(CMAKE_AUTOMOC ON)

add_executable(coursepilot_single college-course-organizer.cpp)
target_link_libraries(coursepilot_single Qt6::Widgets Qt6::Sql)

if(APPLE)
  set_target_properties(coursepilot_single PROPERTIES MACOSX_BUNDLE TRUE)
  message(STATUS "On macOS, the app bundle will be at coursepilot_single.app/Contents/MacOS/coursepilot_single")
else()
  message(STATUS "Executable will be at ./coursepilot_single in your build directory")
endif()