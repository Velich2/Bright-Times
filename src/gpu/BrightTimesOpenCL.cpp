// Bright Times — OpenCL backend (Windows / Intel macOS hosts set to OpenCL).

#include "BrightTimesGPU.h"

#ifdef BT_ENABLE_OPENCL

#ifndef CL_TARGET_OPENCL_VERSION
#define CL_TARGET_OPENCL_VERSION 120
#endif
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <cstring>
#include <mutex>

namespace btgpu {
namespace {

const char* const kKernelNames[BT_KERNEL_COUNT] = {"bt_init", "bt_accumulate", "bt_finalize"};

struct OpenCLData {
    cl_program program = nullptr;
    cl_kernel  kernels[BT_KERNEL_COUNT] = {};
    // clSetKernelArg on a shared cl_kernel is not thread-safe; AE may render
    // several frames concurrently (Multi-Frame Rendering).
    std::mutex lock;
};

}  // namespace

bool OpenCLSetup(const Device& dev, void** outData)
{
    cl_context   context = static_cast<cl_context>(dev.context);
    cl_device_id device  = static_cast<cl_device_id>(dev.device);
    if (!context || !device) return false;

    const char* prefix    = "#define BT_LANG_OPENCL 1\n";
    const char* sources[] = {prefix, kKernelSource};
    size_t      sizes[]   = {std::strlen(prefix), std::strlen(kKernelSource)};

    cl_int      res  = CL_SUCCESS;
    OpenCLData* data = new OpenCLData;
    data->program    = clCreateProgramWithSource(context, 2, sources, sizes, &res);
    if (res == CL_SUCCESS) {
        res = clBuildProgram(data->program, 1, &device, "-cl-single-precision-constant", nullptr, nullptr);
    }
    for (int i = 0; i < BT_KERNEL_COUNT && res == CL_SUCCESS; ++i) {
        data->kernels[i] = clCreateKernel(data->program, kKernelNames[i], &res);
    }
    if (res != CL_SUCCESS) {
        OpenCLTeardown(data);
        return false;
    }
    *outData = data;
    return true;
}

void OpenCLTeardown(void* p)
{
    OpenCLData* data = static_cast<OpenCLData*>(p);
    for (cl_kernel k : data->kernels) {
        if (k) clReleaseKernel(k);
    }
    if (data->program) clReleaseProgram(data->program);
    delete data;
}

bool OpenCLDispatch(const Device& dev, int kernel, void* src, void* dst, const BtGpuParams& p)
{
    OpenCLData* data = static_cast<OpenCLData*>(const_cast<void*>(dev.data));
    if (!data || kernel < 0 || kernel >= BT_KERNEL_COUNT || !dst) return false;

    cl_command_queue queue  = static_cast<cl_command_queue>(dev.queue);
    cl_kernel        k      = data->kernels[kernel];
    cl_mem           srcMem = static_cast<cl_mem>(src ? src : dst);
    cl_mem           dstMem = static_cast<cl_mem>(dst);

    std::lock_guard<std::mutex> guard(data->lock);
    cl_int res = clSetKernelArg(k, 0, sizeof(cl_mem), &srcMem);
    if (res == CL_SUCCESS) res = clSetKernelArg(k, 1, sizeof(cl_mem), &dstMem);
    if (res == CL_SUCCESS) res = clSetKernelArg(k, 2, sizeof(BtGpuParams), &p);
    if (res == CL_SUCCESS) {
        const size_t global[2] = {static_cast<size_t>(p.width), static_cast<size_t>(p.height)};
        res = clEnqueueNDRangeKernel(queue, k, 2, nullptr, global, nullptr, 0, nullptr, nullptr);
    }
    return res == CL_SUCCESS;
}

}  // namespace btgpu

#endif  // BT_ENABLE_OPENCL
