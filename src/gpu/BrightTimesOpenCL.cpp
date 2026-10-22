// Bright Times — OpenCL backend (Windows / Intel macOS hosts set to OpenCL).
// The OpenCL runtime is loaded dynamically (BrightTimesCLApi.h): no OpenCL SDK
// is needed to build and the plug-in loads fine on machines without OpenCL.

#include "BrightTimesGPU.h"

#ifdef BT_ENABLE_OPENCL

#include "BrightTimesCLApi.h"

#include <cstring>
#include <mutex>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace btcl {

namespace {
void* OpenLibrary()
{
#if defined(_WIN32)
    return reinterpret_cast<void*>(LoadLibraryA("OpenCL.dll"));
#elif defined(__APPLE__)
    return dlopen("/System/Library/Frameworks/OpenCL.framework/OpenCL", RTLD_NOW | RTLD_LOCAL);
#else
    void* h = dlopen("libOpenCL.so.1", RTLD_NOW | RTLD_LOCAL);
    return h ? h : dlopen("libOpenCL.so", RTLD_NOW | RTLD_LOCAL);
#endif
}

void* Symbol(void* lib, const char* name)
{
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(lib), name));
#else
    return dlsym(lib, name);
#endif
}

template <typename F>
bool Bind(void* lib, const char* name, F& fn)
{
    fn = reinterpret_cast<F>(Symbol(lib, name));
    return fn != nullptr;
}

Api LoadApi(bool& ok)
{
    Api   api = {};
    void* lib = OpenLibrary();  // kept loaded for the lifetime of the process
    ok = lib &&
         Bind(lib, "clCreateProgramWithSource", api.CreateProgramWithSource) &&
         Bind(lib, "clBuildProgram", api.BuildProgram) &&
         Bind(lib, "clCreateKernel", api.CreateKernel) &&
         Bind(lib, "clReleaseKernel", api.ReleaseKernel) &&
         Bind(lib, "clReleaseProgram", api.ReleaseProgram) &&
         Bind(lib, "clSetKernelArg", api.SetKernelArg) &&
         Bind(lib, "clEnqueueNDRangeKernel", api.EnqueueNDRangeKernel) &&
         Bind(lib, "clFinish", api.Finish);
    return api;
}
}  // namespace

const Api* Load()
{
    static bool      ok  = false;
    static const Api api = LoadApi(ok);  // thread-safe one-time init
    return ok ? &api : nullptr;
}

}  // namespace btcl

namespace btgpu {
namespace {

using namespace btcl;

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
    const Api*   cl      = Load();
    cl_context   context = static_cast<cl_context>(dev.context);
    cl_device_id device  = static_cast<cl_device_id>(dev.device);
    if (!cl || !context || !device) return false;

    const char* prefix    = "#define BT_LANG_OPENCL 1\n";
    const char* sources[] = {prefix, kKernelSource};
    size_t      sizes[]   = {std::strlen(prefix), std::strlen(kKernelSource)};

    cl_int      res  = CL_SUCCESS;
    OpenCLData* data = new OpenCLData;
    data->program    = cl->CreateProgramWithSource(context, 2, sources, sizes, &res);
    if (res == CL_SUCCESS) {
        res = cl->BuildProgram(data->program, 1, &device, "-cl-single-precision-constant", nullptr, nullptr);
    }
    for (int i = 0; i < BT_KERNEL_COUNT && res == CL_SUCCESS; ++i) {
        data->kernels[i] = cl->CreateKernel(data->program, kKernelNames[i], &res);
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
    const Api*  cl   = Load();  // non-null: data only exists after a successful Load()
    for (cl_kernel k : data->kernels) {
        if (k) cl->ReleaseKernel(k);
    }
    if (data->program) cl->ReleaseProgram(data->program);
    delete data;
}

bool OpenCLDispatch(const Device& dev, int kernel, void* src, void* dst, void* out, const BtGpuParams& p)
{
    OpenCLData* data = static_cast<OpenCLData*>(const_cast<void*>(dev.data));
    const Api*  cl   = Load();
    if (!cl || !data || kernel < 0 || kernel >= BT_KERNEL_COUNT || !dst) return false;

    cl_command_queue queue  = static_cast<cl_command_queue>(dev.queue);
    cl_kernel        k      = data->kernels[kernel];
    cl_mem           srcMem = static_cast<cl_mem>(src ? src : dst);
    cl_mem           dstMem = static_cast<cl_mem>(dst);
    cl_mem           outMem = static_cast<cl_mem>(out ? out : dst);

    std::lock_guard<std::mutex> guard(data->lock);
    cl_int res = cl->SetKernelArg(k, 0, sizeof(cl_mem), &srcMem);
    if (res == CL_SUCCESS) res = cl->SetKernelArg(k, 1, sizeof(cl_mem), &dstMem);
    if (res == CL_SUCCESS) res = cl->SetKernelArg(k, 2, sizeof(cl_mem), &outMem);
    if (res == CL_SUCCESS) res = cl->SetKernelArg(k, 3, sizeof(BtGpuParams), &p);
    if (res == CL_SUCCESS) {
        const size_t global[2] = {static_cast<size_t>(p.width), static_cast<size_t>(p.height)};
        res = cl->EnqueueNDRangeKernel(queue, k, 2, nullptr, global, nullptr, 0, nullptr, nullptr);
    }
    return res == CL_SUCCESS;
}

bool OpenCLFinish(const Device& dev)
{
    const Api* cl = Load();
    return cl && cl->Finish(static_cast<cl_command_queue>(dev.queue)) == CL_SUCCESS;
}

}  // namespace btgpu

#endif  // BT_ENABLE_OPENCL
