// Bright Times — CUDA backend (Windows, NVIDIA). Compiled with nvcc.

#include <cuda_fp16.h>
#include <cuda_runtime.h>

#define BT_LANG_CUDA 1
#include "BrightTimesKernels.h"

namespace btgpu {

// Kernels run on the default stream of the context After Effects made current,
// in order with AE's own work (same pattern as the SDK GPU sample).
bool CUDADispatch(int kernel, const float* src, float* dst, float* out, const BtGpuParams& p)
{
    const dim3 block(16, 16, 1);
    const dim3 grid((p.width + block.x - 1) / block.x, (p.height + block.y - 1) / block.y, 1);
    switch (kernel) {
        case BT_KERNEL_INIT:       bt_init<<<grid, block>>>(src, dst, out, p); break;
        case BT_KERNEL_ACCUMULATE: bt_accumulate<<<grid, block>>>(src, dst, out, p); break;
        case BT_KERNEL_FINALIZE:   bt_finalize<<<grid, block>>>(src, dst, out, p); break;
        default:                   return false;
    }
    return cudaPeekAtLastError() == cudaSuccess;
}

bool CUDAFinish()
{
    return cudaStreamSynchronize(0) == cudaSuccess;
}

}  // namespace btgpu
