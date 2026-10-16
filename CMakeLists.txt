cmake_minimum_required(VERSION 3.21)
project(BrightTimes VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(AE_SDK_ROOT "" CACHE PATH "After Effects SDK root (the folder that contains Examples/)")
option(BT_BUILD_TESTS "Build the offline tests (no AE SDK needed)" OFF)

if(WIN32)
  set(_bt_default_opencl ON)
else()
  set(_bt_default_opencl OFF)
endif()
option(BT_ENABLE_CUDA   "GPU rendering through CUDA (Windows, NVIDIA; needs the CUDA Toolkit)" OFF)
option(BT_ENABLE_OPENCL "GPU rendering through OpenCL (runtime-loaded, no SDK needed)"     ${_bt_default_opencl})
option(BT_ENABLE_METAL  "GPU rendering through Metal (macOS)"                              ${APPLE})

# --- GPU kernel source embedded as a string (OpenCL / Metal compile it at runtime)
set(BT_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(BT_KERNEL_HEADER "${BT_GEN_DIR}/BrightTimesKernelsSource.h")
add_custom_command(
  OUTPUT  "${BT_KERNEL_HEADER}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${BT_GEN_DIR}"
  COMMAND "${CMAKE_COMMAND}"
          -DIN=${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/BrightTimesKernels.h
          -DOUT=${BT_KERNEL_HEADER}
          -DNAME=kBtKernelSourceText
          -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedKernel.cmake"
  DEPENDS src/gpu/BrightTimesKernels.h cmake/EmbedKernel.cmake
  COMMENT "Embedding GPU kernel source")
add_custom_target(bt_kernel_source DEPENDS "${BT_KERNEL_HEADER}")

if(BT_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(NOT AE_SDK_ROOT)
  if(NOT BT_BUILD_TESTS)
    message(FATAL_ERROR "Set -DAE_SDK_ROOT=<path to the After Effects SDK> (or -DBT_BUILD_TESTS=ON for tests only).")
  endif()
  return()
endif()

include(cmake/CheckAESDK.cmake)  # sets AE_EX, verifies files and API
set(AE_INCLUDES "${AE_EX}/Headers" "${AE_EX}/Headers/SP" "${AE_EX}/Util" "${AE_EX}/Resources")

# --- Plug-in ------------------------------------------------------------------
set(BT_SOURCES
  src/BrightTimes.cpp
  src/BrightTimes.h
  src/BrightTimesCore.h
  src/gpu/BrightTimesGPU.cpp
  src/gpu/BrightTimesGPU.h
  src/gpu/BrightTimesKernels.h)

if(APPLE)
  add_library(BrightTimes MODULE ${BT_SOURCES})
else()
  add_library(BrightTimes SHARED ${BT_SOURCES})
endif()

target_include_directories(BrightTimes PRIVATE src src/gpu "${BT_GEN_DIR}" ${AE_INCLUDES})
add_dependencies(BrightTimes bt_kernel_source)

if(BT_ENABLE_OPENCL)
  # OpenCL is loaded at runtime from the GPU driver (OpenCL.dll): nothing to link.
  target_sources(BrightTimes PRIVATE src/gpu/BrightTimesOpenCL.cpp src/gpu/BrightTimesCLApi.h)
  target_compile_definitions(BrightTimes PRIVATE BT_ENABLE_OPENCL)
  target_link_libraries(BrightTimes PRIVATE ${CMAKE_DL_LIBS})
endif()

if(BT_ENABLE_CUDA)
  enable_language(CUDA)
  if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES 61 75 86 89)
  endif()
  target_sources(BrightTimes PRIVATE src/gpu/BrightTimesCUDA.cu)
  target_compile_definitions(BrightTimes PRIVATE BT_ENABLE_CUDA)
  set_target_properties(BrightTimes PROPERTIES
    CUDA_RUNTIME_LIBRARY Static
    CUDA_ARCHITECTURES "${CMAKE_CUDA_ARCHITECTURES}")
endif()

if(BT_ENABLE_METAL)
  if(NOT APPLE)
    message(FATAL_ERROR "BT_ENABLE_METAL requires macOS")
  endif()
  enable_language(OBJCXX)
  target_sources(BrightTimes PRIVATE src/gpu/BrightTimesMetal.mm)
  set_source_files_properties(src/gpu/BrightTimesMetal.mm PROPERTIES COMPILE_OPTIONS "-fno-objc-arc")
  target_compile_definitions(BrightTimes PRIVATE BT_ENABLE_METAL)
  target_link_libraries(BrightTimes PRIVATE "-framework Metal" "-framework Foundation")
endif()

if(WIN32)
  target_compile_definitions(BrightTimes PRIVATE MSWindows WIN32 _WINDOWS NOMINMAX)
  set_target_properties(BrightTimes PROPERTIES SUFFIX ".aex" PREFIX "")

  set(BT_PIPL_RC "${BT_GEN_DIR}/BrightTimesPiPL.rc")
  add_custom_command(
    OUTPUT  "${BT_PIPL_RC}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${BT_GEN_DIR}"
    COMMAND "${CMAKE_COMMAND}"
            "-DCL=${CMAKE_CXX_COMPILER}"
            "-DPIPLTOOL=${AE_EX}/Resources/PiPLtool.exe"
            "-DIN=${CMAKE_CURRENT_SOURCE_DIR}/src/BrightTimesPiPL.r"
            "-DOUT_DIR=${BT_GEN_DIR}"
            "-DINCLUDES=${AE_INCLUDES}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildPiPL.cmake"
    DEPENDS src/BrightTimesPiPL.r cmake/BuildPiPL.cmake
    COMMENT "Building PiPL resource")
  target_sources(BrightTimes PRIVATE "${BT_PIPL_RC}")

elseif(APPLE)
  set_target_properties(BrightTimes PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "plugin"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.dvln.brighttimes"
    MACOSX_BUNDLE_BUNDLE_NAME "Bright Times"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
  if(NOT CMAKE_OSX_ARCHITECTURES)
    set_target_properties(BrightTimes PROPERTIES OSX_ARCHITECTURES "x86_64;arm64")
  endif()

  # PiPL: Rez -> data-fork resource inside the bundle.
  set(BT_RSRC "${BT_GEN_DIR}/BrightTimes.rsrc")
  set(rez_inc "")
  foreach(dir IN LISTS AE_INCLUDES)
    list(APPEND rez_inc -i "${dir}")
  endforeach()
  add_custom_command(
    OUTPUT  "${BT_RSRC}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${BT_GEN_DIR}"
    COMMAND xcrun Rez -o "${BT_RSRC}" -useDF -d __MACH__ -arch x86_64 -arch arm64
            ${rez_inc} -isysroot "${CMAKE_OSX_SYSROOT}"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/BrightTimesPiPL.r"
    DEPENDS src/BrightTimesPiPL.r
    COMMENT "Building PiPL resource (Rez)")
  target_sources(BrightTimes PRIVATE "${BT_RSRC}")
  set_source_files_properties("${BT_RSRC}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

  add_custom_command(TARGET BrightTimes POST_BUILD
    COMMAND /bin/sh -c "printf 'eFKTFXTC' > \"$<TARGET_BUNDLE_CONTENT_DIR:BrightTimes>/PkgInfo\""
    VERBATIM)
endif()
