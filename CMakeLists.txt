cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

if(NOT DEFINED OpenCV_DIR)
  if(DEFINED ENV{CONDA_PREFIX})
    set(OpenCV_DIR "$ENV{CONDA_PREFIX}/Library/cmake" CACHE PATH "OpenCV CMake config directory")
  else()
    message(FATAL_ERROR "OpenCV_DIR not set. Set the OpenCV_DIR or CONDA_PREFIX environment variable.")
  endif()
endif()
find_package(OpenCV REQUIRED core imgproc features2d flann calib3d imgcodecs)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${OpenCV_LIBS})
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE /W0)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-main.c)
target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/detector.cpp src/detector.h)

# --- Distribution package ---
if(MSVC)
  set(PACKAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}/_package")
  set(PACKAGE_ZIP "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}-windows-x64.zip")

  get_filename_component(_OPENCV_BIN "${OpenCV_DIR}/../bin" ABSOLUTE)
  set(_OPENCV_DLL_SUFFIX "${OpenCV_VERSION_MAJOR}${OpenCV_VERSION_MINOR}${OpenCV_VERSION_PATCH}")

  set(OPENCV_DLLS)
  foreach(mod core imgproc features2d flann calib3d imgcodecs)
    list(APPEND OPENCV_DLLS "${_OPENCV_BIN}/opencv_${mod}${_OPENCV_DLL_SUFFIX}.dll")
  endforeach()

  add_custom_target(package
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${PACKAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKAGE_DIR}/obs-plugins/64bit"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKAGE_DIR}/data/obs-plugins/${CMAKE_PROJECT_NAME}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>" "${PACKAGE_DIR}/obs-plugins/64bit/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/data" "${PACKAGE_DIR}/data/obs-plugins/${CMAKE_PROJECT_NAME}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OPENCV_DLLS} "${PACKAGE_DIR}/obs-plugins/64bit/"
    COMMAND powershell -Command "Compress-Archive -Path '${PACKAGE_DIR}\\*' -DestinationPath '${PACKAGE_ZIP}' -Force"
    COMMENT "Creating distribution package: ${CMAKE_PROJECT_NAME}-windows-x64.zip"
    VERBATIM
  )
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
