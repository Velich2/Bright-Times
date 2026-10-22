// Bright Times — GPU backend interface (CUDA / OpenCL / Metal).
//
// Backends are compiled in with BT_ENABLE_CUDA / BT_ENABLE_OPENCL / BT_ENABLE_METAL.
// The AE glue lives in BrightTimes.cpp; this layer only knows about raw device
// handles and memory, which keeps every backend small and testable.

#pragma once

#include "BrightTimesKernels.h"

namespace btgpu {

// Mirrors PF_GPU_Framework values so this header does not need the AE SDK.
enum Framework { kNone = 0, kOpenCL = 1, kMetal = 2, kCUDA = 3, kDirectX = 4 };

struct Device {
    Framework   framework = kNone;
    void*       device    = nullptr;  // cl_device_id / id<MTLDevice>
    void*       context   = nullptr;  // cl_context
    void*       queue     = nullptr;  // cl_command_queue / id<MTLCommandQueue>
    const void* data      = nullptr;  // per-device data returned by Setup()
};

// True if this build contains a backend for `fw`.
bool IsSupported(int fw);

// Compiles kernels for the device. On success *outData receives per-device data
// that must be released with Teardown().
bool Setup(const Device& dev, void** outData);
void Teardown(int fw, void* data);

// Enqueues one kernel (BT_KERNEL_*) over params.width x params.height.
// `src` and `out` may be null; buffers are cl_mem / id<MTLBuffer> / CUDA device pointers.
bool Dispatch(const Device& dev, int kernel, void* src, void* dst, void* out, const BtGpuParams& params);

// Waits until all work enqueued by Dispatch has finished (before freeing buffers).
bool Finish(const Device& dev);

// Backend entry points (defined only when the backend is enabled).
bool OpenCLSetup(const Device& dev, void** outData);
void OpenCLTeardown(void* data);
bool OpenCLDispatch(const Device& dev, int kernel, void* src, void* dst, void* out, const BtGpuParams& p);
bool OpenCLFinish(const Device& dev);

bool MetalSetup(const Device& dev, void** outData);
void MetalTeardown(void* data);
bool MetalDispatch(const Device& dev, int kernel, void* src, void* dst, void* out, const BtGpuParams& p);
bool MetalFinish(const Device& dev);

bool CUDADispatch(int kernel, const float* src, float* dst, float* out, const BtGpuParams& p);
bool CUDAFinish();

// Kernel source string (generated from BrightTimesKernels.h at build time).
extern const char* const kKernelSource;

}  // namespace btgpu
