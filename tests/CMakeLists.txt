# Offline tests: no After Effects SDK required (tests/sdk_stub stands in for it).

add_executable(bt_preview ../tools/preview.cpp)
target_include_directories(bt_preview PRIVATE ../src)

add_executable(bt_cpu_host_test cpu_host_test.cpp ../src/BrightTimes.cpp ../src/gpu/BrightTimesGPU.cpp)
target_include_directories(bt_cpu_host_test PRIVATE sdk_stub ../src ../src/gpu)
add_test(NAME cpu_host COMMAND bt_cpu_host_test ${CMAKE_CURRENT_BINARY_DIR})

find_package(OpenCL QUIET)
if(OpenCL_FOUND)
  add_executable(bt_opencl_host_test opencl_host_test.cpp
    ../src/BrightTimes.cpp ../src/gpu/BrightTimesGPU.cpp ../src/gpu/BrightTimesOpenCL.cpp)
  add_dependencies(bt_opencl_host_test bt_kernel_source)
  target_include_directories(bt_opencl_host_test PRIVATE sdk_stub ../src ../src/gpu "${BT_GEN_DIR}")
  target_compile_definitions(bt_opencl_host_test PRIVATE BT_ENABLE_OPENCL CL_TARGET_OPENCL_VERSION=120)
  # OpenCL::OpenCL is for the mock host itself; the plug-in code loads it at runtime.
  target_link_libraries(bt_opencl_host_test PRIVATE OpenCL::OpenCL ${CMAKE_DL_LIBS})
  add_test(NAME opencl_host COMMAND bt_opencl_host_test)

  # Premiere Pro GPU filter through a mock Premiere host (frames in OpenCL buffers).
  add_executable(bt_premiere_gpu_test premiere_gpu_test.cpp
    ../src/BrightTimes.cpp ../src/premiere/BrightTimesPremiereGPU.cpp
    ../src/gpu/BrightTimesGPU.cpp ../src/gpu/BrightTimesOpenCL.cpp)
  add_dependencies(bt_premiere_gpu_test bt_kernel_source)
  target_include_directories(bt_premiere_gpu_test PRIVATE sdk_stub prsdk_stub ../src ../src/gpu "${BT_GEN_DIR}")
  target_compile_definitions(bt_premiere_gpu_test PRIVATE BT_ENABLE_OPENCL BT_PREMIERE_GPU CL_TARGET_OPENCL_VERSION=120)
  target_link_libraries(bt_premiere_gpu_test PRIVATE OpenCL::OpenCL ${CMAKE_DL_LIBS})
  add_test(NAME premiere_gpu COMMAND bt_premiere_gpu_test)
  add_test(NAME premiere_gpu_zero_based_popups COMMAND bt_premiere_gpu_test zero-based-popups)
else()
  message(STATUS "OpenCL not found: GPU test skipped")
endif()
