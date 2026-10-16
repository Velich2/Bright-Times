// Bright Times — Metal backend (macOS). Built without ARC (-fno-objc-arc).

#include "BrightTimesGPU.h"

#ifdef BT_ENABLE_METAL

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <algorithm>

namespace btgpu {
namespace {

const char* const kKernelNames[BT_KERNEL_COUNT] = {"bt_init", "bt_accumulate", "bt_finalize"};

struct MetalData {
    id<MTLComputePipelineState> pipelines[BT_KERNEL_COUNT] = {};
};

}  // namespace

bool MetalSetup(const Device& dev, void** outData)
{
    @autoreleasepool {
        id<MTLDevice> device = (id<MTLDevice>)dev.device;
        if (!device) return false;

        NSString* source = [NSString stringWithFormat:@"#define BT_LANG_METAL 1\n%s", kKernelSource];
        NSError*  error  = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
        if (!library) {
            NSLog(@"Bright Times: Metal compile failed: %@", error);
            return false;
        }

        MetalData* data = new MetalData;
        bool       ok   = true;
        for (int i = 0; i < BT_KERNEL_COUNT && ok; ++i) {
            id<MTLFunction> fn = [library newFunctionWithName:[NSString stringWithUTF8String:kKernelNames[i]]];
            if (fn) {
                data->pipelines[i] = [device newComputePipelineStateWithFunction:fn error:&error];
                [fn release];
            }
            ok = data->pipelines[i] != nil;
        }
        [library release];

        if (!ok) {
            NSLog(@"Bright Times: Metal pipeline failed: %@", error);
            MetalTeardown(data);
            return false;
        }
        *outData = data;
        return true;
    }
}

void MetalTeardown(void* p)
{
    MetalData* data = static_cast<MetalData*>(p);
    for (id<MTLComputePipelineState> ps : data->pipelines) {
        [ps release];
    }
    delete data;
}

bool MetalDispatch(const Device& dev, int kernel, void* src, void* dst, const BtGpuParams& p)
{
    const MetalData* data = static_cast<const MetalData*>(dev.data);
    if (!data || kernel < 0 || kernel >= BT_KERNEL_COUNT || !dst) return false;

    @autoreleasepool {
        id<MTLCommandQueue>         queue    = (id<MTLCommandQueue>)dev.queue;
        id<MTLComputePipelineState> pipeline = data->pipelines[kernel];
        id<MTLBuffer>               dstBuf   = (id<MTLBuffer>)dst;
        id<MTLBuffer>               srcBuf   = src ? (id<MTLBuffer>)src : dstBuf;

        id<MTLCommandBuffer>         cmd = [queue commandBuffer];
        id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
        [enc setComputePipelineState:pipeline];
        [enc setBuffer:srcBuf offset:0 atIndex:0];
        [enc setBuffer:dstBuf offset:0 atIndex:1];
        [enc setBytes:&p length:sizeof(BtGpuParams) atIndex:2];

        const NSUInteger tw = pipeline.threadExecutionWidth;
        const NSUInteger th = std::max<NSUInteger>(1, pipeline.maxTotalThreadsPerThreadgroup / tw);
        const MTLSize    group  = MTLSizeMake(tw, th, 1);
        const MTLSize    groups = MTLSizeMake((p.width + tw - 1) / tw, (p.height + th - 1) / th, 1);
        [enc dispatchThreadgroups:groups threadsPerThreadgroup:group];
        [enc endEncoding];
        [cmd commit];
        return true;
    }
}

}  // namespace btgpu

#endif  // BT_ENABLE_METAL
