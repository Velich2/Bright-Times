// Premiere Pro GPU filter test: a mock Premiere host drives xGPUFilterEntry
// (CreateInstance, GetFrameDependencies, Render) with GPU frames in OpenCL
// buffers (any OpenCL device, e.g. pocl). Every result is compared with an
// independent CPU reference built from the same parameter values with
// BrightTimesCore.h. Covers BGRA 32f and 16f frames, parameter reading through
// GetParam (index - 1), frame dependencies and Clip Edges.
#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>

#include "pr_stub.h"
#include "BrightTimes.h"
#include "BrightTimesCore.h"
#include "BrightTimesSettings.h"
#include "gpu/BrightTimesKernels.h"

#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

extern "C" prSuiteError xGPUFilterEntry(csSDK_uint32, csSDK_int32*, prBool, piSuitesPtr, PrGPUFilter*, PrGPUFilterInfo*);

namespace {

constexpr int           W = 96, H = 64;
constexpr PrPixelFormat kGpu32f = static_cast<PrPixelFormat>(0x41444340), kGpu16f = static_cast<PrPixelFormat>(0x61444340);
constexpr PrTime        kTpf = 10000, kClipStartSeq = 1000000;

cl_context       g_ctx;
cl_command_queue g_q;
cl_device_id     g_dev;

// ---- parameters (After Effects PARAMS_SETUP through the real EffectMain)
std::vector<PF_ParamDef> g_params;
PF_Err add_param(PF_ProgPtr, A_long, PF_ParamDef* d) { g_params.push_back(*d); return 0; }
const PF_ParamDef& P(int aeIndex) { return g_params[aeIndex - 1]; }

enum Kind { kFloat, kInt, kPopup, kBool, kAngle, kColor };
Kind KindOf(int i)
{
    switch (i) {
        case BT_GROUP_MODE: case BT_COLOR_ORDER: case BT_COLOR_MODE: case BT_EDGE_MODE:
        case BT_TINT_MODE: case BT_BLEND_MODE: return kPopup;
        case BT_INCLUDE_CURRENT: case BT_CURRENT_UNTINTED: case BT_REVERSE_COLORS: case BT_USE_GPU: return kBool;
        case BT_FRAME_STEP: case BT_GROUPS: case BT_PALETTE_SIZE: return kInt;
        case BT_HUE_START: return kAngle;
        default: return i >= BT_COLOR_1 && i < BT_COLOR_1 + bt::kMaxCustomColor ? kColor : kFloat;
    }
}

// Reference reader: After Effects semantics straight from the table.
struct TableReader {
    A_long    Int(int i)      { return P(i).u.sd.value; }
    float     Float(int i)    { return static_cast<float>(P(i).u.fs_d.value); }
    bool      Bool(int i)     { return P(i).u.bd.value != 0; }
    A_long    Popup(int i)    { return P(i).u.pd.value; }
    float     AngleDeg(int i) { return P(i).u.ad.value / 65536.f; }
    bt::Color Color(int i)    { const PF_Pixel c = P(i).u.cd.value; return {c.red / 255.f, c.green / 255.f, c.blue / 255.f}; }
};

// ---- mock Premiere suites
struct MockPPix {
    cl_mem        mem;
    int           w, h, rowBytes;
    PrPixelFormat fmt;
};
std::set<MockPPix*> g_live;
PPixHand            Hand(MockPPix* p) { return reinterpret_cast<PPixHand>(p); }
MockPPix*           Mock(PPixHand h) { return reinterpret_cast<MockPPix*>(h); }

MockPPix* NewPPix(PrPixelFormat fmt, int w, int h)
{
    const int bpp = fmt == kGpu16f ? 8 : 16;
    auto*     p   = new MockPPix{nullptr, w, h, w * bpp + 64, fmt};  // padded rows
    cl_int    e;
    p->mem = clCreateBuffer(g_ctx, CL_MEM_READ_WRITE, (size_t)p->rowBytes * h, nullptr, &e);
    g_live.insert(p);
    return p;
}

int g_paramReads = 0;
bool g_colorUnknownEncoding = false;  // colours as Float64 (an encoding the filter does not know)
PrTime g_durationTicks = 0;

prSuiteError GetDeviceInfo(csSDK_uint32, csSDK_uint32, PrGPUDeviceInfo* info)
{
    std::memset(info, 0, sizeof(*info));
    info->outDeviceFramework = PrGPUDeviceFramework_OpenCL;
    info->outMeetsMinimumRequirementsForAcceleration = kPrTrue;
    info->outDeviceHandle = g_dev;
    info->outContextHandle = g_ctx;
    info->outCommandQueueHandle = g_q;
    return suiteError_NoError;
}
prSuiteError CreateGPUPPix(csSDK_uint32, PrPixelFormat f, int w, int h, int, int, prFieldType, PPixHand* out)
{
    *out = Hand(NewPPix(f, w, h));
    return suiteError_NoError;
}
prSuiteError GetGPUPPixData(PPixHand h, void** d) { *d = Mock(h)->mem; return suiteError_NoError; }
prSuiteError Dispose(PPixHand h)
{
    MockPPix* p = Mock(h);
    if (!g_live.count(p)) return suiteError_InvalidParms;
    clReleaseMemObject(p->mem);
    g_live.erase(p);
    delete p;
    return suiteError_NoError;
}
prSuiteError GetBounds(PPixHand h, prRect* r) { *r = {0, 0, Mock(h)->w, Mock(h)->h}; return suiteError_NoError; }
prSuiteError GetRowBytes(PPixHand h, csSDK_int32* rb) { *rb = Mock(h)->rowBytes; return suiteError_NoError; }
prSuiteError GetPAR(PPixHand, csSDK_uint32* n, csSDK_uint32* d) { *n = 1; *d = 1; return suiteError_NoError; }
prSuiteError GetPixelFormat(PPixHand h, PrPixelFormat* f) { *f = Mock(h)->fmt; return suiteError_NoError; }
prSuiteError GetNodeProperty(csSDK_int32, const char* key, PrMemoryPtr* v)
{
    if (std::string(key) != kVideoSegmentProperty_Effect_EffectDuration || g_durationTicks <= 0) return suiteError_Fail;
    const std::string s = std::to_string((long long)g_durationTicks);
    *v = new char[s.size() + 1];
    std::memcpy(*v, s.c_str(), s.size() + 1);
    return suiteError_NoError;
}
void DisposePtr(PrMemoryPtr p) { delete[] p; }
prSuiteError GetParam(csSDK_int32, csSDK_int32 index, PrTime, PrParam* out)
{
    const int ae = index + 1;  // GPU filters do not see the input layer
    if (ae < 1 || ae > (int)g_params.size()) return suiteError_InvalidParms;
    ++g_paramReads;
    const PF_ParamDef& d = P(ae);
    switch (KindOf(ae)) {
        case kPopup: out->mType = kPrParamType_Int32; out->mInt32 = d.u.pd.value; break;
        case kInt:   out->mType = kPrParamType_Int32; out->mInt32 = d.u.sd.value; break;
        case kBool:  out->mType = kPrParamType_Bool;  out->mBool = d.u.bd.value ? 1 : 0; break;
        case kAngle: out->mType = kPrParamType_Float64; out->mFloat64 = d.u.ad.value / 65536.0; break;
        case kColor: {  // Int64, 16 bits per channel 0xAAAARRRRGGGGBBBB
            const PF_Pixel c = d.u.cd.value;
            if (g_colorUnknownEncoding) { out->mType = kPrParamType_Float64; out->mFloat64 = c.red; break; }
            out->mType  = kPrParamType_Int64;
            out->mInt64 = (csSDK_int64)((0xFFFFull << 48) | (c.red * 257ull << 32) | (c.green * 257ull << 16) | c.blue * 257ull);
            break;
        }
        default:     out->mType = kPrParamType_Float64; out->mFloat64 = d.u.fs_d.value; break;
    }
    return suiteError_NoError;
}

PrSDKGPUDeviceSuite     g_gpuSuite{GetDeviceInfo, CreateGPUPPix, GetGPUPPixData};
PrSDKPPixSuite          g_ppixSuite{Dispose, GetBounds, GetRowBytes, GetPAR, GetPixelFormat};
PrSDKVideoSegmentSuite  g_segSuite{GetNodeProperty, GetParam};
PrSDKMemoryManagerSuite g_memSuite{DisposePtr};
int g_acquired = 0;
SPErr Acquire(const char* n, int32_t, const void** s)
{
    const std::string name(n);
    if (name == kPrSDKGPUDeviceSuite) *s = &g_gpuSuite;
    else if (name == kPrSDKPPixSuite) *s = &g_ppixSuite;
    else if (name == kPrSDKVideoSegmentSuite) *s = &g_segSuite;
    else if (name == kPrSDKMemoryManagerSuite) *s = &g_memSuite;
    else return 1;
    ++g_acquired;
    return 0;
}
SPErr Release(const char*, int32_t) { --g_acquired; return 0; }
SPBasicSuite  g_basic{Acquire, Release};
SPBasicSuite* GetBasic() { return &g_basic; }
PlugUtilFuncs g_util{nullptr, nullptr, nullptr, GetBasic};
piSuites      g_pi{1, nullptr, nullptr, nullptr, &g_util, nullptr};

// ---- scene (straight RGBA) at clip frame t; transparent outside the clip
int g_clipFrames = 20;
void Scene(int t, int x, int y, float* c)
{
    if (t < 0 || t >= g_clipFrames) { c[0] = c[1] = c[2] = c[3] = 0.f; return; }
    float v = 0.5f + 0.3f * y / H;
    if ((x / 8 + y / 8) % 2) v -= 0.05f;
    c[0] = v; c[1] = v * 0.9f; c[2] = v * 0.75f; c[3] = x < 4 ? 0.5f : 1.f;
    const float cx = 20 + t * 5.f;
    if (std::fabs(x - cx) < 8 && std::fabs(y - 32.f) < 14) { c[0] = 0.95f; c[1] = 0.9f; c[2] = 0.3f; }
}

MockPPix* UploadFrame(int t, PrPixelFormat fmt)
{
    MockPPix*          p = NewPPix(fmt, W, H);
    std::vector<char>  host((size_t)p->rowBytes * H, 0);
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        float c[4]; Scene(t, x, y, c);
        const float bgra[4] = {c[2], c[1], c[0], c[3]};
        for (int k = 0; k < 4; ++k) {
            if (fmt == kGpu16f) ((unsigned short*)(host.data() + y * p->rowBytes))[x * 4 + k] = bt_float_to_half(bgra[k]);
            else ((float*)(host.data() + y * p->rowBytes))[x * 4 + k] = bgra[k];
        }
    }
    clEnqueueWriteBuffer(g_q, p->mem, CL_TRUE, 0, host.size(), host.data(), 0, nullptr, nullptr);
    return p;
}

// Independent CPU reference (straight RGBA per pixel).
std::vector<float> Reference(int cur, bool hold)
{
    bt::Settings s;
    TableReader  r;
    ReadSettingsFrom(r, s);
    const auto samples = bt::BuildSamples(s);
    std::vector<float> acc(4 * W * H), frame(4 * W * H), orig(4 * W * H), res(4 * W * H);
    auto load = [&](int t, std::vector<float>& f) {
        for (int i = 0; i < W * H; ++i) { float c[4]; Scene(t, i % W, i / W, c);
            f[4*i] = c[0] * c[3]; f[4*i+1] = c[1] * c[3]; f[4*i+2] = c[2] * c[3]; f[4*i+3] = c[3]; }
    };
    load(cur, orig);
    bt::InitAccumRow(s.blendMode, acc.data(), W * H);
    for (const auto& k : samples) {
        int t = cur + k.frameOffset;
        if (hold) t = std::max(0, std::min(t, g_clipFrames - 1));
        load(t, frame);
        bt::AccumulateRow(s.blendMode, s.tintMode, k, frame.data(), acc.data(), W * H);
    }
    bt::FinalizeRow(s.blendMode, s.mix, acc.data(), orig.data(), res.data(), W * H, s.intensity);
    for (int i = 0; i < W * H; ++i) {
        const float a = res[4*i+3], inv = a > 1e-6f ? 1.f / a : 0.f;
        res[4*i] *= inv; res[4*i+1] *= inv; res[4*i+2] *= inv;
    }
    return res;
}

// Runs the GPU filter for clip frame `cur`; returns max |gpu - reference| or a
// negative value on failure.
PrGPUFilter g_filter;
double RunGpu(int cur, PrPixelFormat fmt, bool hold, std::string* why)
{
    PrGPUFilterInstance inst{};
    inst.piSuites = &g_pi; inst.inDeviceIndex = 0; inst.inNodeID = 7;
    if (g_filter.CreateInstance(&inst) != suiteError_NoError) { *why = "CreateInstance declined"; return -1; }

    PrGPUFilterRenderParams rp{};
    rp.inClipTime = cur * kTpf; rp.inSequenceTime = kClipStartSeq + cur * kTpf;
    rp.inRenderWidth = W; rp.inRenderHeight = H; rp.inRenderPARNum = rp.inRenderPARDen = 1;
    rp.inRenderTicksPerFrame = kTpf;

    // Collect dependencies the way the host does: increment ioQueryIndex for more.
    std::vector<PrTime> deps;
    csSDK_int32 q = 0;
    for (int guard = 0; guard < 1000; ++guard) {
        PrGPUFilterFrameDependency d{};
        const csSDK_int32 before = q;
        if (g_filter.GetFrameDependencies(&inst, &rp, &q, &d) != suiteError_NoError) break;
        if (d.outDependencyType != PrGPUDependency_InputFrame || d.outTrackID != 0) { *why = "bad dependency"; return -1; }
        deps.push_back(d.outSequenceTime);
        if (q == before) break;
    }

    std::vector<MockPPix*> frames{UploadFrame(cur, fmt)};
    for (PrTime t : deps) frames.push_back(UploadFrame((int)((t - kClipStartSeq) / kTpf), fmt));
    std::vector<PPixHand> hands;
    for (auto* f : frames) hands.push_back(Hand(f));
    PPixHand out = nullptr;
    const prSuiteError e = g_filter.Render(&inst, &rp, hands.data(), hands.size(), &out);
    g_filter.DisposeInstance(&inst);
    if (e != suiteError_NoError || !out) { *why = "Render failed"; return -1; }

    MockPPix* o = Mock(out);
    if (o->fmt != fmt || o->w != W || o->h != H) { *why = "output format/size"; return -1; }
    std::vector<char> host((size_t)o->rowBytes * H);
    clEnqueueReadBuffer(g_q, o->mem, CL_TRUE, 0, host.size(), host.data(), 0, nullptr, nullptr);
    const auto ref = Reference(cur, hold);
    double maxd = 0;
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) for (int k = 0; k < 4; ++k) {
        const float g = fmt == kGpu16f ? bt_half_to_float(((unsigned short*)(host.data() + y * o->rowBytes))[x * 4 + k])
                                       : ((float*)(host.data() + y * o->rowBytes))[x * 4 + k];
        const int   rgba = k == 0 ? 2 : k == 2 ? 0 : k;  // BGRA -> RGBA
        maxd = std::max(maxd, (double)std::fabs(g - ref[4 * (y * W + x) + rgba]));
    }
    Dispose(out);
    for (auto* f : frames) Dispose(Hand(f));
    return maxd;
}

}  // namespace

int main()
{
    cl_platform_id plat; cl_uint np = 0; clGetPlatformIDs(1, &plat, &np);
    if (!np) { std::printf("no OpenCL\n"); return 1; }
    clGetDeviceIDs(plat, CL_DEVICE_TYPE_ALL, 1, &g_dev, nullptr);
    cl_int e; g_ctx = clCreateContext(nullptr, 1, &g_dev, nullptr, nullptr, &e);
    g_q = clCreateCommandQueue(g_ctx, g_dev, 0, &e);

    // Parameters through the real PARAMS_SETUP.
    PF_InData in{}; PF_OutData out{};
    in.inter.add_param = add_param;
    EffectMain(PF_Cmd_PARAMS_SETUP, &in, &out, nullptr, nullptr, nullptr);

    csSDK_int32 index = 0;
    PrGPUFilterInfo info{};
    if (xGPUFilterEntry(1, &index, kPrTrue, &g_pi, &g_filter, &info) != suiteError_NoError || index != 0 ||
        !g_filter.CreateInstance || !g_filter.Render) { std::printf("FAIL: entry\n"); return 1; }

    int fails = 0;
    struct Case { int blend; PrPixelFormat fmt; int back, fwd; int cur; int edge; double tol; const char* name; };
    const Case cases[] = {
        {1, kGpu32f, 4, 4, 10, BT_EDGE_HOLD, 1e-5, "optical 32f"},
        {2, kGpu32f, 3, 2, 10, BT_EDGE_HOLD, 1e-5, "additive 32f"},
        {3, kGpu16f, 4, 4, 10, BT_EDGE_HOLD, 4e-3, "screen 16f"},
        {4, kGpu16f, 2, 5, 10, BT_EDGE_HOLD, 4e-3, "lighten 16f"},
        {5, kGpu32f, 6, 0, 10, BT_EDGE_HOLD, 1e-5, "trails 32f"},
        {1, kGpu32f, 5, 2, 1, BT_EDGE_HOLD, 1e-5, "clip start, hold"},
        {1, kGpu32f, 5, 2, 1, BT_EDGE_TRANSPARENT, 1e-5, "clip start, transparent"},
        {2, kGpu16f, 2, 6, 17, BT_EDGE_HOLD, 4e-3, "clip end, hold 16f"},
    };
    g_durationTicks = g_clipFrames * kTpf;
    for (const Case& c : cases) {
        g_params[BT_BLEND_MODE - 1].u.pd.value = c.blend;
        g_params[BT_FRAMES_BACKWARD - 1].u.fs_d.value = c.back;
        g_params[BT_FRAMES_FORWARD - 1].u.fs_d.value = c.fwd;
        g_params[BT_EDGE_MODE - 1].u.pd.value = c.edge;
        g_params[BT_BLEND_ORIGINAL - 1].u.fs_d.value = c.blend == 2 ? 30 : 0;
        std::string why;
        const double d = RunGpu(c.cur, c.fmt, c.edge == BT_EDGE_HOLD, &why);
        const bool ok = d >= 0 && d <= c.tol;
        fails += !ok;
        std::printf("%-26s max|gpu-ref| = %.2e  %s %s\n", c.name, d, ok ? "OK" : "FAIL", why.c_str());
    }

    // Custom Palette on the GPU: colours read through GetParam.
    {
        const std::vector<PF_ParamDef> saved = g_params;
        g_params[BT_COLOR_MODE - 1].u.pd.value   = bt::ColorMode_CUSTOM;
        g_params[BT_GROUPS - 1].u.sd.value       = 3;
        g_params[BT_PALETTE_SIZE - 1].u.sd.value = 3;
        g_params[BT_COLOR_1 - 1].u.cd.value      = PF_Pixel{255, 255, 128, 0};
        g_params[BT_COLOR_1].u.cd.value          = PF_Pixel{255, 0, 64, 255};
        g_params[BT_COLOR_1 + 1].u.cd.value      = PF_Pixel{255, 200, 0, 180};
        for (int blend : {1, 5}) {
            g_params[BT_BLEND_MODE - 1].u.pd.value = blend;
            std::string why;
            const double d = RunGpu(10, kGpu32f, true, &why);
            const bool ok = d >= 0 && d <= 1e-5;
            fails += !ok;
            std::printf("%-26s max|gpu-ref| = %.2e  %s %s\n", blend == 1 ? "custom palette optical" : "custom palette trails",
                        d, ok ? "OK" : "FAIL", why.c_str());
        }
        g_params = saved;
    }

    // Declines (-> Premiere software path): palette colours in an unknown
    // encoding, GPU Acceleration off.
    auto declines = [&](int param, A_long value, bool popup) {
        PF_ParamDef saved = g_params[param - 1];
        if (popup) g_params[param - 1].u.pd.value = value; else g_params[param - 1].u.bd.value = value;
        PrGPUFilterInstance inst{}; inst.piSuites = &g_pi; inst.inNodeID = 7;
        const bool declined = g_filter.CreateInstance(&inst) != suiteError_NoError;
        if (!declined) g_filter.DisposeInstance(&inst);
        g_params[param - 1] = saved;
        return declined;
    };
    g_colorUnknownEncoding = true;
    const bool d1 = declines(BT_COLOR_MODE, bt::ColorMode_CUSTOM, true);
    const bool k1 = !declines(BT_COLOR_MODE, bt::ColorMode_SPECTRUM, true);  // colours are not read then
    g_colorUnknownEncoding = false;
    const bool d2 = declines(BT_USE_GPU, 0, false);
    std::printf("declines: unknown colour encoding %s, GPU off %s; spectrum kept %s\n", d1 ? "OK" : "FAIL",
                d2 ? "OK" : "FAIL", k1 ? "OK" : "FAIL");
    fails += !k1;
    fails += !d1 + !d2;

    index = 0;
    xGPUFilterEntry(1, &index, kPrFalse, &g_pi, &g_filter, &info);  // shutdown
    const bool clean = g_live.empty() && g_acquired == 0 && g_paramReads > 0;
    std::printf("resources: %zu live buffers, %d suites held -> %s\n", g_live.size(), g_acquired, clean ? "OK" : "FAIL");
    fails += !clean;
    std::printf("%s\n", fails ? "FAILURES" : "ALL OK");
    return fails;
}
