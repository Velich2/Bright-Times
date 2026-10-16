// Bright Times — GPU backend selection.

#include "BrightTimesGPU.h"

#if defined(BT_ENABLE_OPENCL) || defined(BT_ENABLE_METAL)
#include "BrightTimesKernelsSource.h"  // generated: kBtKernelSourceText
#endif

namespace btgpu {

#if defined(BT_ENABLE_OPENCL) || defined(BT_ENABLE_METAL)
const char* const kKernelSource = kBtKernelSourceText;
#else
const char* const kKernelSource = "";
#endif

bool IsSupported(int fw)
{
    switch (fw) {
#ifdef BT_ENABLE_OPENCL
        case kOpenCL: return true;
#endif
#ifdef BT_ENABLE_METAL
        case kMetal: return true;
#endif
#ifdef BT_ENABLE_CUDA
        case kCUDA: return true;
#endif
        default: return false;
    }
}

bool Setup(const Device& dev, void** outData)
{
    *outData = nullptr;
    switch (dev.framework) {
#ifdef BT_ENABLE_OPENCL
        case kOpenCL: return OpenCLSetup(dev, outData);
#endif
#ifdef BT_ENABLE_METAL
        case kMetal: return MetalSetup(dev, outData);
#endif
#ifdef BT_ENABLE_CUDA
        case kCUDA: return true;  // kernels are linked statically
#endif
        default: return false;
    }
}

void Teardown(int fw, void* data)
{
    if (!data) return;
    switch (fw) {
#ifdef BT_ENABLE_OPENCL
        case kOpenCL: OpenCLTeardown(data); break;
#endif
#ifdef BT_ENABLE_METAL
        case kMetal: MetalTeardown(data); break;
#endif
        default: break;
    }
}

bool Dispatch(const Device& dev, int kernel, void* src, void* dst, const BtGpuParams& p)
{
    (void)kernel; (void)src; (void)dst;  // unused when no backend is compiled in
    if (p.width <= 0 || p.height <= 0) return true;
    switch (dev.framework) {
#ifdef BT_ENABLE_OPENCL
        case kOpenCL: return OpenCLDispatch(dev, kernel, src, dst, p);
#endif
#ifdef BT_ENABLE_METAL
        case kMetal: return MetalDispatch(dev, kernel, src, dst, p);
#endif
#ifdef BT_ENABLE_CUDA
        case kCUDA:
            return CUDADispatch(kernel, static_cast<const float*>(src ? src : dst),
                                static_cast<float*>(dst), p);
#endif
        default: return false;
    }
}

}  // namespace btgpu
