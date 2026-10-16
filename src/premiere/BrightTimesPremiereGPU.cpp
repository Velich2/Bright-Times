// Bright Times - Premiere Pro GPU filter (xGPUFilterEntry).
//
// Premiere Pro does not use the After Effects GPU path. GPU rendering goes
// through this separate entry point; Premiere matches it to the effect through
// the module's PiPL. Parameters, UI and the software render stay in
// BrightTimes.cpp. When the GPU filter declines (CreateInstance error) Premiere
// renders that clip with the software path instead.
//
//   - Frameworks: CUDA (Windows) and Metal (macOS) - the ones Premiere uses.
//   - Frames: GPU_BGRA_4444_32f or _16f (half). Accumulation is always 32f.
//   - Neighbouring frames: requested in GetFrameDependencies (sequence time,
//     current track); Hold First/Last Frame clamps them to the clip.
//   - Parameters: PrSDKVideoSegmentSuite::GetParam. Colours of the Custom
//     Palette are not read on the GPU (their PrParam encoding is not
//     documented): with Custom Palette the GPU filter declines and the
//     32-bit float software path renders instead.

#include "BrightTimes.h"
#include "BrightTimesCore.h"
#include "BrightTimesSettings.h"
#include "gpu/BrightTimesGPU.h"

#include "PrSDKGPUFilter.h"
#include "PrSDKGPUDeviceSuite.h"
#include "PrSDKPPixSuite.h"
#include "PrSDKVideoSegmentSuite.h"
#include "PrSDKVideoSegmentProperties.h"
#include "PrSDKMemoryManagerSuite.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <new>
#include <vector>

namespace {

// GPU pixel formats (FOURCC '@CDA' / '@CDa', same values as PrPixelFormat_GPU_*).
constexpr PrPixelFormat kGpuBGRA32f = static_cast<PrPixelFormat>(0x41444340);
constexpr PrPixelFormat kGpuBGRA16f = static_cast<PrPixelFormat>(0x61444340);

// ---------------------------------------------------------------------------
// Suites
// ---------------------------------------------------------------------------

struct PrSuites {
    SPBasicSuite*            sp   = nullptr;
    PrSDKGPUDeviceSuite*     gpu  = nullptr;
    PrSDKPPixSuite*          ppix = nullptr;
    PrSDKVideoSegmentSuite*  seg  = nullptr;
    PrSDKMemoryManagerSuite* mem  = nullptr;

    bool Acquire(piSuitesPtr pi)
    {
        if (!pi || !pi->utilFuncs || !pi->utilFuncs->getSPBasicSuite) return false;
        sp = pi->utilFuncs->getSPBasicSuite();
        if (!sp) return false;
        const void* p = nullptr;
        if (sp->AcquireSuite(kPrSDKGPUDeviceSuite, kPrSDKGPUDeviceSuiteVersion, &p) == kSPNoError)
            gpu = (PrSDKGPUDeviceSuite*)p;
        if (sp->AcquireSuite(kPrSDKPPixSuite, kPrSDKPPixSuiteVersion, &p) == kSPNoError)
            ppix = (PrSDKPPixSuite*)p;
        if (sp->AcquireSuite(kPrSDKVideoSegmentSuite, kPrSDKVideoSegmentSuiteVersion, &p) == kSPNoError)
            seg = (PrSDKVideoSegmentSuite*)p;
        if (sp->AcquireSuite(kPrSDKMemoryManagerSuite, kPrSDKMemoryManagerSuiteVersion, &p) == kSPNoError)
            mem = (PrSDKMemoryManagerSuite*)p;
        return gpu && ppix && seg && mem;
    }

    void Release()
    {
        if (!sp) return;
        if (gpu)  sp->ReleaseSuite(kPrSDKGPUDeviceSuite, kPrSDKGPUDeviceSuiteVersion);
        if (ppix) sp->ReleaseSuite(kPrSDKPPixSuite, kPrSDKPPixSuiteVersion);
        if (seg)  sp->ReleaseSuite(kPrSDKVideoSegmentSuite, kPrSDKVideoSegmentSuiteVersion);
        if (mem)  sp->ReleaseSuite(kPrSDKMemoryManagerSuite, kPrSDKMemoryManagerSuiteVersion);
        gpu = nullptr; ppix = nullptr; seg = nullptr; mem = nullptr;
    }
};

// ---------------------------------------------------------------------------
// Compiled kernels per device (shared by all instances; instances are created
// on every parameter change, compiling each time would be slow).
// ---------------------------------------------------------------------------

struct BackendKey {
    int      framework;
    unsigned device;
    bool operator<(const BackendKey& o) const
    {
        return framework != o.framework ? framework < o.framework : device < o.device;
    }
};

std::mutex                       g_backendLock;
std::map<BackendKey, void*>      g_backends;

bool GetBackend(const btgpu::Device& dev, unsigned deviceIndex, void** data)
{
    std::lock_guard<std::mutex> guard(g_backendLock);
    const BackendKey key{dev.framework, deviceIndex};
    auto it = g_backends.find(key);
    if (it != g_backends.end()) {
        *data = it->second;
        return true;
    }
    if (!btgpu::Setup(dev, data)) return false;
    g_backends[key] = *data;
    return true;
}

void ReleaseBackends()
{
    std::lock_guard<std::mutex> guard(g_backendLock);
    for (auto& kv : g_backends) btgpu::Teardown(kv.first.framework, kv.second);
    g_backends.clear();
}

int FrameworkOf(PrGPUDeviceFramework fw)
{
    switch (fw) {
        case PrGPUDeviceFramework_CUDA:   return btgpu::kCUDA;
        case PrGPUDeviceFramework_OpenCL: return btgpu::kOpenCL;
        case PrGPUDeviceFramework_Metal:  return btgpu::kMetal;
        default:                          return btgpu::kNone;
    }
}

// ---------------------------------------------------------------------------
// Parameters through PrSDKVideoSegmentSuite::GetParam. GPU filters do not see
// the input layer, so the index is the After Effects index minus one.
// ---------------------------------------------------------------------------

class PrParamReader {
public:
    PrParamReader(PrSDKVideoSegmentSuite* seg, csSDK_int32 node, PrTime time)
        : seg_(seg), node_(node), time_(time) {}

    bool ok() const { return ok_; }

    double Num(int aeIndex)
    {
        PrParam p;
        std::memset(&p, 0, sizeof(p));
        if (seg_->GetParam(node_, aeIndex - 1, time_, &p) != suiteError_NoError) {
            ok_ = false;
            return 0.0;
        }
        switch (p.mType) {
            case kPrParamType_Int8:    return p.mInt8;
            case kPrParamType_Int16:   return p.mInt16;
            case kPrParamType_Int32:   return p.mInt32;
            case kPrParamType_Int64:   return static_cast<double>(p.mInt64);
            case kPrParamType_Float32: return p.mFloat32;
            case kPrParamType_Float64: return p.mFloat64;
            case kPrParamType_Bool:    return p.mBool ? 1.0 : 0.0;
            default:                   ok_ = false; return 0.0;
        }
    }

    A_long    Int(int i)      { return static_cast<A_long>(std::lround(Num(i))); }
    float     Float(int i)    { return static_cast<float>(Num(i)); }
    bool      Bool(int i)     { return Num(i) != 0.0; }
    A_long    Popup(int i)    { return Int(i); }
    float     AngleDeg(int i) { return Float(i); }
    bt::Color Color(int)      { return bt::Color{}; }  // not read on the GPU, see top of file

private:
    PrSDKVideoSegmentSuite* seg_;
    csSDK_int32             node_;
    PrTime                  time_;
    bool                    ok_ = true;
};

// Reads the settings at clip time `t`. Returns false if the GPU cannot render
// them (unreadable parameter, Custom Palette, GPU Acceleration off).
bool ReadPrSettings(PrSDKVideoSegmentSuite* seg, csSDK_int32 node, PrTime t, bt::Settings& s,
                    bool& holdEdges)
{
    PrParamReader r(seg, node, t);
    ReadSettingsFrom(r, s);
    const bool useGpu = r.Bool(BT_USE_GPU);
    A_long     edge   = r.Popup(BT_EDGE_MODE);
    if (!r.ok()) return false;

    // Popups are 1-based in the After Effects API. A 0 can only come from a
    // 0-based host encoding; shift all popups in that case.
    if (edge == 0 || s.groupMode == 0 || s.colorOrder == 0 || s.colorMode == 0 || s.tintMode == 0 ||
        s.blendMode == 0) {
        edge += 1;
        s.groupMode  = static_cast<bt::GroupMode>(s.groupMode + 1);
        s.colorOrder = static_cast<bt::ColorOrder>(s.colorOrder + 1);
        s.colorMode  = static_cast<bt::ColorMode>(s.colorMode + 1);
        s.tintMode   = static_cast<bt::TintMode>(s.tintMode + 1);
        s.blendMode  = static_cast<bt::BlendMode>(s.blendMode + 1);
    }
    holdEdges = edge == BT_EDGE_HOLD;
    return useGpu && s.colorMode != bt::ColorMode_CUSTOM;
}

// Effect duration in ticks (0 if unknown).
PrTime EffectDuration(const PrSuites& su, csSDK_int32 node)
{
    PrMemoryPtr value = nullptr;
    if (su.seg->GetNodeProperty(node, kVideoSegmentProperty_Effect_EffectDuration, &value) != suiteError_NoError ||
        !value) {
        return 0;
    }
    const PrTime d = std::strtoll(value, nullptr, 10);
    su.mem->PrDisposePtr(value);
    return d > 0 ? d : 0;
}

// ---------------------------------------------------------------------------
// Instance
// ---------------------------------------------------------------------------

struct Instance {
    PrSuites      suites;
    btgpu::Device dev;
    unsigned      deviceIndex = 0;
};

// Sequence time of every sample; -1 = the current frame (inFrames[0]).
struct SamplePlan {
    bt::Settings             settings;
    std::vector<bt::Sample>  samples;
    std::vector<PrTime>      times;       // per sample, sequence time
    std::vector<PrTime>      dependencies;  // distinct times other than the current one
};

bool BuildPlan(const Instance& inst, csSDK_int32 node, const PrGPUFilterRenderParams* rp, SamplePlan& plan)
{
    bool hold = true;
    if (!ReadPrSettings(inst.suites.seg, node, rp->inClipTime, plan.settings, hold)) return false;
    plan.samples = bt::BuildSamples(plan.settings);

    const PrTime tpf       = rp->inRenderTicksPerFrame > 0 ? rp->inRenderTicksPerFrame : 1;
    const PrTime clipStart = rp->inSequenceTime - rp->inClipTime;  // sequence time of the clip start
    const PrTime duration  = hold ? EffectDuration(inst.suites, node) : 0;

    plan.times.clear();
    plan.dependencies.clear();
    for (const bt::Sample& smp : plan.samples) {
        PrTime clipT = rp->inClipTime + static_cast<PrTime>(smp.frameOffset) * tpf;
        if (hold) {
            if (clipT < 0) clipT = 0;
            if (duration > tpf && clipT > duration - tpf) clipT = duration - tpf;
        }
        const PrTime seqT = clipStart + clipT;
        plan.times.push_back(seqT);
        if (seqT != rp->inSequenceTime) {
            bool known = false;
            for (PrTime d : plan.dependencies) known = known || d == seqT;
            if (!known) plan.dependencies.push_back(seqT);
        }
    }
    return true;
}

prSuiteError CreateInstance(PrGPUFilterInstance* io)
{
    Instance* inst = new (std::nothrow) Instance;
    if (!inst) return suiteError_Fail;
    inst->deviceIndex = io->inDeviceIndex;

    bool ok = inst->suites.Acquire(io->piSuites);
    PrGPUDeviceInfo info;
    std::memset(&info, 0, sizeof(info));
    ok = ok && inst->suites.gpu->GetDeviceInfo(kPrSDKGPUDeviceSuiteVersion, io->inDeviceIndex, &info) ==
                   suiteError_NoError;
    ok = ok && info.outMeetsMinimumRequirementsForAcceleration;

    if (ok) {
        inst->dev.framework = static_cast<btgpu::Framework>(FrameworkOf(info.outDeviceFramework));
        inst->dev.device    = info.outDeviceHandle;
        inst->dev.context   = info.outContextHandle;
        inst->dev.queue     = info.outCommandQueueHandle;
        void* data          = nullptr;
        ok = btgpu::IsSupported(inst->dev.framework) && GetBackend(inst->dev, io->inDeviceIndex, &data);
        inst->dev.data = data;
    }
    if (ok) {
        // Opt out when the settings need the software path (checked at the
        // clip start; GPU instances are recreated on every parameter change).
        bt::Settings s;
        bool         hold = true;
        ok = ReadPrSettings(inst->suites.seg, io->inNodeID, 0, s, hold);
    }
    if (!ok) {
        inst->suites.Release();
        delete inst;
        return suiteError_Fail;  // -> software rendering for this clip
    }
    io->ioPrivatePluginData = inst;
    io->outIsRealtime       = kPrTrue;
    return suiteError_NoError;
}

prSuiteError DisposeInstance(PrGPUFilterInstance* io)
{
    Instance* inst = static_cast<Instance*>(io->ioPrivatePluginData);
    if (inst) {
        inst->suites.Release();
        delete inst;
    }
    io->ioPrivatePluginData = nullptr;
    return suiteError_NoError;
}

prSuiteError GetFrameDependencies(PrGPUFilterInstance* io, const PrGPUFilterRenderParams* rp,
                                  csSDK_int32* ioQueryIndex, PrGPUFilterFrameDependency* out)
{
    const Instance* inst = static_cast<const Instance*>(io->ioPrivatePluginData);
    SamplePlan      plan;
    if (!inst || !BuildPlan(*inst, io->inNodeID, rp, plan)) return suiteError_NotImplemented;

    const csSDK_int32 index = *ioQueryIndex;
    if (index < 0 || index >= static_cast<csSDK_int32>(plan.dependencies.size())) {
        return suiteError_NotImplemented;  // only the current frame
    }
    std::memset(out, 0, sizeof(*out));
    out->outDependencyType = PrGPUDependency_InputFrame;
    out->outTrackID        = 0;  // current track
    out->outSequenceTime   = plan.dependencies[index];
    if (index + 1 < static_cast<csSDK_int32>(plan.dependencies.size())) {
        ++*ioQueryIndex;  // more to come
    }
    return suiteError_NoError;
}

prSuiteError Precompute(PrGPUFilterInstance*, const PrGPUFilterRenderParams*, csSDK_int32, PPixHand)
{
    return suiteError_NotImplemented;
}

struct GpuFrame {
    void*         mem    = nullptr;
    int           width  = 0;
    int           height = 0;
    int           pitch  = 0;  // pixels
    bool          half   = false;
    PrPixelFormat format = 0;
};

bool Describe(const PrSuites& su, PPixHand frame, GpuFrame& f)
{
    prRect      bounds;
    csSDK_int32 rowBytes = 0;
    if (!frame ||
        su.ppix->GetBounds(frame, &bounds) != suiteError_NoError ||
        su.ppix->GetRowBytes(frame, &rowBytes) != suiteError_NoError ||
        su.ppix->GetPixelFormat(frame, &f.format) != suiteError_NoError ||
        su.gpu->GetGPUPPixData(frame, &f.mem) != suiteError_NoError) {
        return false;
    }
    if (f.format != kGpuBGRA32f && f.format != kGpuBGRA16f) return false;
    f.half   = f.format == kGpuBGRA16f;
    f.width  = bounds.right - bounds.left;
    f.height = bounds.bottom - bounds.top;
    f.pitch  = rowBytes / (f.half ? 8 : 16);
    return f.mem != nullptr;
}

prSuiteError Render(PrGPUFilterInstance* io, const PrGPUFilterRenderParams* rp, const PPixHand* inFrames,
                    csSDK_size_t inFrameCount, PPixHand* outFrame)
{
    Instance* inst = static_cast<Instance*>(io->ioPrivatePluginData);
    if (!inst || !inFrames || inFrameCount < 1 || !outFrame) return suiteError_Fail;
    const PrSuites& su = inst->suites;

    SamplePlan plan;
    if (!BuildPlan(*inst, io->inNodeID, rp, plan)) return suiteError_Fail;
    if (inFrameCount < plan.dependencies.size() + 1) return suiteError_Fail;

    GpuFrame current;
    if (!Describe(su, inFrames[0], current)) return suiteError_Fail;

    csSDK_uint32 parNum = 1, parDen = 1;
    su.ppix->GetPixelAspectRatio(inFrames[0], &parNum, &parDen);

    // Output in the input format, plus a 32f accumulation buffer.
    PPixHand outHand = nullptr, accHand = nullptr;
    GpuFrame out, acc;
    bool ok = su.gpu->CreateGPUPPix(inst->deviceIndex, current.format, current.width, current.height,
                                    static_cast<int>(parNum), static_cast<int>(parDen),
                                    rp->inRenderFieldType, &outHand) == suiteError_NoError &&
              su.gpu->CreateGPUPPix(inst->deviceIndex, kGpuBGRA32f, current.width, current.height,
                                    static_cast<int>(parNum), static_cast<int>(parDen),
                                    rp->inRenderFieldType, &accHand) == suiteError_NoError &&
              Describe(su, outHand, out) && Describe(su, accHand, acc);

    const bt::Settings& s = plan.settings;
    BtGpuParams base;
    std::memset(&base, 0, sizeof(base));
    base.width    = current.width;
    base.height   = current.height;
    base.dstPitch = acc.pitch;
    base.blend    = s.blendMode;
    base.tint     = s.tintMode;
    base.mix      = s.mix;
    base.gain     = s.intensity;

    auto source = [&](const GpuFrame& f, BtGpuParams& p) {
        p.hasSrc    = f.mem ? 1 : 0;
        p.srcPitch  = f.pitch;
        p.srcWidth  = f.width;
        p.srcHeight = f.height;
        p.srcHalf   = f.half ? 1 : 0;
    };

    if (ok) {
        BtGpuParams init = base;
        init.initValue   = (s.blendMode == bt::BlendMode_SCREEN) ? 1.f : 0.f;
        ok = btgpu::Dispatch(inst->dev, BT_KERNEL_INIT, nullptr, acc.mem, nullptr, init);
    }
    for (size_t i = 0; ok && i < plan.samples.size(); ++i) {
        // inFrames[0] = current time, then the dependencies in the order returned.
        size_t frameIndex = 0;
        for (size_t d = 0; d < plan.dependencies.size(); ++d) {
            if (plan.dependencies[d] == plan.times[i]) frameIndex = d + 1;
        }
        GpuFrame src;
        if (!Describe(su, inFrames[frameIndex], src)) continue;  // nothing there: transparent
        const bt::Sample& k = plan.samples[i];
        BtGpuParams       p = base;
        source(src, p);
        p.kr = k.kr; p.kg = k.kg; p.kb = k.kb; p.ka = k.ka;
        ok = btgpu::Dispatch(inst->dev, BT_KERNEL_ACCUMULATE, src.mem, acc.mem, nullptr, p);
    }
    if (ok) {
        BtGpuParams p = base;
        source(current, p);
        p.hasOut   = 1;
        p.outPitch = out.pitch;
        p.outHalf  = out.half ? 1 : 0;
        ok = btgpu::Dispatch(inst->dev, BT_KERNEL_FINALIZE, current.mem, acc.mem, out.mem, p);
    }

    // The accumulation buffer may only be released once the GPU is done with it.
    if (accHand) {
        btgpu::Finish(inst->dev);
        su.ppix->Dispose(accHand);
    }
    if (!ok) {
        if (outHand) su.ppix->Dispose(outHand);
        return suiteError_Fail;
    }
    *outFrame = outHand;
    return suiteError_NoError;
}

}  // namespace

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

extern "C" DllExport prSuiteError xGPUFilterEntry(csSDK_uint32 /*inHostInterfaceVersion*/,
                                                  csSDK_int32* ioIndex, prBool inStartup,
                                                  piSuitesPtr /*piSuites*/, PrGPUFilter* outFilter,
                                                  PrGPUFilterInfo* outFilterInfo)
{
    if (!inStartup) {
        ReleaseBackends();
        return suiteError_NoError;
    }
    if (!ioIndex || !outFilter || !outFilterInfo || *ioIndex != 0) return suiteError_InvalidParms;

    outFilter->CreateInstance       = CreateInstance;
    outFilter->DisposeInstance      = DisposeInstance;
    outFilter->GetFrameDependencies = GetFrameDependencies;
    outFilter->Precompute           = Precompute;
    outFilter->Render               = Render;

    // One filter in this module; empty match name = the module's PiPL effect.
    std::memset(&outFilterInfo->outMatchName, 0, sizeof(outFilterInfo->outMatchName));
    outFilterInfo->outInterfaceVersion = PrSDKGPUFilterInterfaceVersion;
    return suiteError_NoError;
}
