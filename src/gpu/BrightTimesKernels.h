/*
 * Bright Times - GPU kernels.
 *
 * ONE source for every backend:
 *   - CUDA   : #included by BrightTimesCUDA.cu with BT_LANG_CUDA defined.
 *   - OpenCL : embedded as a string (cmake/EmbedKernel.cmake) and compiled at
 *              GPU device setup with "#define BT_LANG_OPENCL 1" prepended.
 *   - Metal  : same string, compiled with "#define BT_LANG_METAL 1" prepended.
 *   - Host   : without any BT_LANG_* define it is plain C/C++ (parameter struct
 *              for the host code + CPU reference used by the tests).
 *
 * Memory layout: GPU frames are B G R A, straight alpha, 32-bit float
 * (AE PF_PixelFormat_GPU_BGRA128, Premiere GPU_BGRA_4444_32f) or 16-bit half
 * (Premiere GPU_BGRA_4444_16f; srcHalf / outHalf). The accumulation buffer `dst`
 * is always 32-bit float premultiplied R G B A. bt_finalize writes the result
 * either back into `dst` (AE: the output world is the accumulator) or into a
 * separate `out` buffer (hasOut, Premiere). The math mirrors BrightTimesCore.h
 * (AccumulatePixel / FinalizePixel).
 *
 * Keep this file free of comments containing the raw-string terminator
 * sequence used by EmbedKernel.cmake.
 */

#ifndef BRIGHT_TIMES_KERNELS_H
#define BRIGHT_TIMES_KERNELS_H

#if defined(BT_LANG_OPENCL)
    #define BT_FN
    #define BT_GLOBAL __global
#elif defined(BT_LANG_METAL)
    #include <metal_stdlib>
    using namespace metal;
    #define BT_FN inline
    #define BT_GLOBAL device
#elif defined(BT_LANG_CUDA)
    #define BT_FN __device__ __forceinline__
    #define BT_GLOBAL
#else
    #define BT_LANG_HOST 1
    #define BT_FN static inline
    #define BT_GLOBAL
    #include <string.h>
#endif

/* ------------------------------------------------------------------------ */
/* Element access: 32-bit float or 16-bit half storage.                     */
/* ------------------------------------------------------------------------ */
#if defined(BT_LANG_OPENCL)
BT_FN float bt_ld(__global const float* p, int i, int h)
{
    return h ? vload_half(i, (__global const half*)p) : p[i];
}
BT_FN void bt_st(__global float* p, int i, int h, float v)
{
    if (h) vstore_half(v, i, (__global half*)p); else p[i] = v;
}
#elif defined(BT_LANG_METAL)
BT_FN float bt_ld(device const float* p, int i, int h)
{
    return h ? float(((device const half*)p)[i]) : p[i];
}
BT_FN void bt_st(device float* p, int i, int h, float v)
{
    if (h) ((device half*)p)[i] = half(v); else p[i] = v;
}
#elif defined(BT_LANG_CUDA)
BT_FN float bt_ld(const float* p, int i, int h)
{
    return h ? __half2float(((const __half*)p)[i]) : p[i];
}
BT_FN void bt_st(float* p, int i, int h, float v)
{
    if (h) ((__half*)p)[i] = __float2half(v); else p[i] = v;
}
#else
/* Host reference: IEEE 754 half <-> float. */
BT_FN float bt_half_to_float(unsigned short h)
{
    unsigned int sign = (unsigned int)(h & 0x8000u) << 16;
    unsigned int exp  = (h >> 10) & 0x1Fu;
    unsigned int man  = h & 0x3FFu;
    unsigned int bits;
    if (exp == 0) {
        if (man == 0) {
            bits = sign;
        } else { /* subnormal */
            exp = 127 - 15 + 1;
            while ((man & 0x400u) == 0) { man <<= 1; --exp; }
            man &= 0x3FFu;
            bits = sign | (exp << 23) | (man << 13);
        }
    } else if (exp == 31) {
        bits = sign | 0x7F800000u | (man << 13);
    } else {
        bits = sign | ((exp - 15 + 127) << 23) | (man << 13);
    }
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}
BT_FN unsigned short bt_float_to_half(float f)
{
    unsigned int bits;
    memcpy(&bits, &f, sizeof(bits));
    unsigned int sign = (bits >> 16) & 0x8000u;
    int          exp  = (int)((bits >> 23) & 0xFFu) - 127 + 15;
    unsigned int man  = bits & 0x7FFFFFu;
    if (exp <= 0) {
        if (exp < -10) return (unsigned short)sign;
        man = (man | 0x800000u) >> (1 - exp);
        return (unsigned short)(sign | ((man + 0x1000u) >> 13));
    }
    if (exp >= 31) return (unsigned short)(sign | 0x7C00u);
    unsigned int r = sign | ((unsigned int)exp << 10) | (man >> 13);
    if (man & 0x1000u) r += 1; /* round half up */
    return (unsigned short)r;
}
BT_FN float bt_ld(const float* p, int i, int h)
{
    return h ? bt_half_to_float(((const unsigned short*)p)[i]) : p[i];
}
BT_FN void bt_st(float* p, int i, int h, float v)
{
    if (h) ((unsigned short*)p)[i] = bt_float_to_half(v); else p[i] = v;
}
#endif

/* Must match bt::BlendMode / bt::TintMode in BrightTimesCore.h. */
#define BT_BLEND_OPTICAL  1
#define BT_BLEND_ADDITIVE 2
#define BT_BLEND_SCREEN   3
#define BT_BLEND_LIGHTEN  4
#define BT_BLEND_TRAILS   5
#define BT_TINT_FILTER    1
#define BT_TINT_LUMA      2

/* Kernel ids used by the host dispatchers. */
#define BT_KERNEL_INIT       0
#define BT_KERNEL_ACCUMULATE 1
#define BT_KERNEL_FINALIZE   2
#define BT_KERNEL_COUNT      3

/* Only 4-byte members: identical layout in C++, CUDA, OpenCL C and Metal. */
typedef struct BtGpuParams {
    int   width;       /* output size (pixels)                 */
    int   height;
    int   dstPitch;    /* output row pitch (pixels)            */
    int   srcPitch;    /* source row pitch (pixels)            */
    int   srcWidth;
    int   srcHeight;
    int   srcDx;       /* source x = output x + srcDx          */
    int   srcDy;       /* source y = output y + srcDy          */
    int   hasSrc;      /* 0 = source is fully transparent      */
    int   blend;
    int   tint;
    float kr, kg, kb, ka;
    float mix;
    float initValue;
    float gain;        /* Intensity, used by Tinted Trails in bt_finalize */
    int   srcHalf;     /* source / current frame is 16-bit half */
    int   hasOut;      /* finalize into `out` instead of in place */
    int   outPitch;    /* `out` row pitch (pixels) */
    int   outHalf;     /* `out` is 16-bit half */
} BtGpuParams;

BT_FN float bt_clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

BT_FN float bt_maxf(float a, float b)
{
    return a > b ? a : b;
}

BT_FN int bt_src_offset(BtGpuParams p, int x, int y)
{
    int sx = x + p.srcDx;
    int sy = y + p.srcDy;
    if (p.hasSrc == 0 || sx < 0 || sy < 0 || sx >= p.srcWidth || sy >= p.srcHeight) {
        return -1;
    }
    return (sy * p.srcPitch + sx) * 4;
}

BT_FN void bt_init_px(BT_GLOBAL float* dst, BtGpuParams p, int x, int y)
{
    BT_GLOBAL float* d = dst + (y * p.dstPitch + x) * 4;
    d[0] = p.initValue;
    d[1] = p.initValue;
    d[2] = p.initValue;
    d[3] = p.initValue;
}

BT_FN void bt_accumulate_px(BT_GLOBAL const float* src, BT_GLOBAL float* dst,
                            BtGpuParams p, int x, int y)
{
    int so = bt_src_offset(p, x, y);
    if (so < 0) {
        return; /* transparent pixels add nothing in every mode */
    }
    /* BGRA straight -> premultiplied RGB */
    float a = bt_clamp01(bt_ld(src, so + 3, p.srcHalf));
    float r = bt_ld(src, so + 2, p.srcHalf) * a;
    float g = bt_ld(src, so + 1, p.srcHalf) * a;
    float b = bt_ld(src, so + 0, p.srcHalf) * a;
    if (p.tint == BT_TINT_LUMA) {
        float l = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        r = l;
        g = l;
        b = l;
    }
    float cr = p.kr * r;
    float cg = p.kg * g;
    float cb = p.kb * b;
    float ca = p.ka * a;

    BT_GLOBAL float* d = dst + (y * p.dstPitch + x) * 4;
    if (p.blend == BT_BLEND_SCREEN) {
        d[0] *= 1.0f - bt_clamp01(cr);
        d[1] *= 1.0f - bt_clamp01(cg);
        d[2] *= 1.0f - bt_clamp01(cb);
        d[3] *= 1.0f - bt_clamp01(ca);
    } else if (p.blend == BT_BLEND_LIGHTEN || p.blend == BT_BLEND_TRAILS) {
        d[0] = bt_maxf(d[0], cr);
        d[1] = bt_maxf(d[1], cg);
        d[2] = bt_maxf(d[2], cb);
        d[3] = bt_maxf(d[3], ca);
    } else {
        d[0] += cr;
        d[1] += cg;
        d[2] += cb;
        d[3] += ca;
    }
}

/* `cur` = unmodified current frame (BGRA straight), used for Mix. */
BT_FN void bt_finalize_px(BT_GLOBAL const float* cur, BT_GLOBAL float* dst,
                          BT_GLOBAL float* out, BtGpuParams p, int x, int y)
{
    BT_GLOBAL float* d = dst + (y * p.dstPitch + x) * 4;
    float r = d[0];
    float g = d[1];
    float b = d[2];
    float a = d[3];
    /* current frame, premultiplied RGBA (0 outside the source) */
    float o0 = 0.0f, o1 = 0.0f, o2 = 0.0f, o3 = 0.0f;
    int so = bt_src_offset(p, x, y);
    if (so >= 0) {
        o3 = bt_clamp01(bt_ld(cur, so + 3, p.srcHalf));
        o0 = bt_ld(cur, so + 2, p.srcHalf) * o3;
        o1 = bt_ld(cur, so + 1, p.srcHalf) * o3;
        o2 = bt_ld(cur, so + 0, p.srcHalf) * o3;
    }
    if (p.blend == BT_BLEND_SCREEN) {
        r = 1.0f - r;
        g = 1.0f - g;
        b = 1.0f - b;
        a = 1.0f - a;
    } else if (p.blend == BT_BLEND_TRAILS) {
        r = o0 + p.gain * bt_maxf(0.0f, r - o0);
        g = o1 + p.gain * bt_maxf(0.0f, g - o1);
        b = o2 + p.gain * bt_maxf(0.0f, b - o2);
        a = o3 + p.gain * bt_maxf(0.0f, a - o3);
    }
    float mix = bt_clamp01(p.mix);
    if (mix < 1.0f) {
        r = o0 + (r - o0) * mix;
        g = o1 + (g - o1) * mix;
        b = o2 + (b - o2) * mix;
        a = o3 + (a - o3) * mix;
    }
    a = bt_clamp01(a);
    float inv = a > 1e-6f ? 1.0f / a : 0.0f;
    /* premultiplied RGBA -> BGRA straight: in place, or into `out` */
    if (p.hasOut) {
        int oo = (y * p.outPitch + x) * 4;
        bt_st(out, oo + 0, p.outHalf, b * inv);
        bt_st(out, oo + 1, p.outHalf, g * inv);
        bt_st(out, oo + 2, p.outHalf, r * inv);
        bt_st(out, oo + 3, p.outHalf, a);
    } else {
        d[0] = b * inv;
        d[1] = g * inv;
        d[2] = r * inv;
        d[3] = a;
    }
}

/* ------------------------------------------------------------------------ */
/* Entry points. Every kernel has the same signature: (src, dst, out, p).   */
/* ------------------------------------------------------------------------ */

#if defined(BT_LANG_OPENCL)

__kernel void bt_init(__global const float* src, __global float* dst, __global float* out, BtGpuParams p)
{
    int x = (int)get_global_id(0);
    int y = (int)get_global_id(1);
    if (x < p.width && y < p.height) bt_init_px(dst, p, x, y);
}

__kernel void bt_accumulate(__global const float* src, __global float* dst, __global float* out, BtGpuParams p)
{
    int x = (int)get_global_id(0);
    int y = (int)get_global_id(1);
    if (x < p.width && y < p.height) bt_accumulate_px(src, dst, p, x, y);
}

__kernel void bt_finalize(__global const float* src, __global float* dst, __global float* out, BtGpuParams p)
{
    int x = (int)get_global_id(0);
    int y = (int)get_global_id(1);
    if (x < p.width && y < p.height) bt_finalize_px(src, dst, out, p, x, y);
}

#elif defined(BT_LANG_METAL)

kernel void bt_init(device const float* src [[buffer(0)]],
                    device float* dst [[buffer(1)]],
                    device float* out [[buffer(2)]],
                    constant BtGpuParams& p [[buffer(3)]],
                    uint2 gid [[thread_position_in_grid]])
{
    int x = (int)gid.x;
    int y = (int)gid.y;
    if (x < p.width && y < p.height) bt_init_px(dst, p, x, y);
}

kernel void bt_accumulate(device const float* src [[buffer(0)]],
                          device float* dst [[buffer(1)]],
                          device float* out [[buffer(2)]],
                          constant BtGpuParams& p [[buffer(3)]],
                          uint2 gid [[thread_position_in_grid]])
{
    int x = (int)gid.x;
    int y = (int)gid.y;
    if (x < p.width && y < p.height) bt_accumulate_px(src, dst, p, x, y);
}

kernel void bt_finalize(device const float* src [[buffer(0)]],
                        device float* dst [[buffer(1)]],
                        device float* out [[buffer(2)]],
                        constant BtGpuParams& p [[buffer(3)]],
                        uint2 gid [[thread_position_in_grid]])
{
    int x = (int)gid.x;
    int y = (int)gid.y;
    if (x < p.width && y < p.height) bt_finalize_px(src, dst, out, p, x, y);
}

#elif defined(BT_LANG_CUDA)

__global__ void bt_init(const float* src, float* dst, float* out, BtGpuParams p)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x < p.width && y < p.height) bt_init_px(dst, p, x, y);
}

__global__ void bt_accumulate(const float* src, float* dst, float* out, BtGpuParams p)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x < p.width && y < p.height) bt_accumulate_px(src, dst, p, x, y);
}

__global__ void bt_finalize(const float* src, float* dst, float* out, BtGpuParams p)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x < p.width && y < p.height) bt_finalize_px(src, dst, out, p, x, y);
}

#endif

#endif /* BRIGHT_TIMES_KERNELS_H */
