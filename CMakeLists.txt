cmake_minimum_required(VERSION 3.16)
project(truemeasure VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV 4.6 REQUIRED COMPONENTS core imgproc imgcodecs calib3d aruco)
if(OpenCV_VERSION VERSION_GREATER_EQUAL 4.7)
  message(FATAL_ERROR "TrueMeasure is pinned to OpenCV 4.6.x. Found ${OpenCV_VERSION}.")
endif()
find_package(nlohmann_json 3.11.3 REQUIRED)

option(TRUEMEASURE_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

add_library(truemeasure_core
  src/sha256.cpp
  src/format.cpp
  src/engine.cpp
  src/annotate.cpp
  src/viewer.cpp
)
target_include_directories(truemeasure_core PUBLIC include)
target_include_directories(truemeasure_core SYSTEM PUBLIC ${OpenCV_INCLUDE_DIRS})
target_link_libraries(truemeasure_core PUBLIC ${OpenCV_LIBS} nlohmann_json::nlohmann_json)
target_compile_definitions(truemeasure_core
  PUBLIC TRUEMEASURE_VERSION="${PROJECT_VERSION}"
  PRIVATE TRUEMEASURE_SHARE_DIR="${CMAKE_SOURCE_DIR}/share"
)
target_compile_options(truemeasure_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(truemeasure src/cli.cpp)
target_link_libraries(truemeasure PRIVATE truemeasure_core)
target_compile_options(truemeasure PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(truemeasure_tests tests/test_main.cpp)
target_link_libraries(truemeasure_tests PRIVATE truemeasure_core)
target_compile_options(truemeasure_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(TRUEMEASURE_SANITIZERS)
  foreach(target truemeasure_core truemeasure truemeasure_tests)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endforeach()
endif()

enable_testing()
add_test(NAME unit COMMAND truemeasure_tests $<TARGET_FILE:truemeasure> ${CMAKE_SOURCE_DIR})
set_tests_properties(unit PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180)
add_test(NAME viewer
         COMMAND python3 ${CMAKE_SOURCE_DIR}/tests/viewer_smoke.py $<TARGET_FILE:truemeasure>
                 ${CMAKE_SOURCE_DIR})
set_tests_properties(viewer PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 30)

install(TARGETS truemeasure RUNTIME DESTINATION bin)
install(DIRECTORY share/ DESTINATION share/truemeasure)
