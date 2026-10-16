// Bright Times — After Effects SmartFX effect.
//
// PreRender  : reads parameters, builds the sample list (frame offsets, colours,
//              weights) and checks out the input layer at every needed time.
// SmartRender: accumulates every checked-out frame and writes the result.
//              CPU path: 8 / 16 / 32 bpc. GPU path: 32 bpc on CUDA / OpenCL /
//              Metal (gpu/BrightTimesKernels.h), selected by After Effects.

#include "BrightTimes.h"
#include "BrightTimesCore.h"
#include "gpu/BrightTimesGPU.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

namespace {

// Checkout ids 0..N-1 are the samples; this one is the unmodified current frame.
constexpr A_long kCurrentFrameCheckoutId = 10000;
// Re-checkouts of samples that fell outside the clip (Hold First/Last Frame).
constexpr A_long kHeldFrameCheckoutBase = 20000;

constexpr A_long kAppPremiere = 0x50724D72;  // 'PrMr'

inline bool IsPremiere(const PF_InData* in_data) { return in_data->appl_id == kAppPremiere; }

struct RenderData {
    bt::Settings             settings;
    std::vector<bt::Sample>  samples;
    std::vector<A_long>      checkoutIds;  // per sample: id used in PreRender checkout
    bool                     useGpu = true;
};

void DeleteRenderData(void* p)
{
    delete static_cast<RenderData*>(p);
}

// ---------------------------------------------------------------------------
// PICA suite RAII helper
// ---------------------------------------------------------------------------

template <typename Suite>
class SuiteScope {
public:
    SuiteScope(PF_InData* in_data, const char* name, int32_t version)
        : basic_(in_data->pica_basicP), name_(name), version_(version)
    {
        const void* p = nullptr;
        if (basic_ && basic_->AcquireSuite(name_, version_, &p) == kSPNoError) {
            suite_ = static_cast<const Suite*>(p);
        }
    }
    ~SuiteScope()
    {
        if (suite_) basic_->ReleaseSuite(name_, version_);
    }
    SuiteScope(const SuiteScope&)            = delete;
    SuiteScope& operator=(const SuiteScope&) = delete;

    const Suite* operator->() const { return suite_; }
    explicit operator bool() const { return suite_ != nullptr; }

private:
    SPBasicSuite* basic_;
    const char*   name_;
    int32_t       version_;
    const Suite*  suite_ = nullptr;
};

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------

class ParamReader {
public:
    explicit ParamReader(PF_InData* in_data) : in_data_(in_data) {}

    PF_Err err() const { return err_; }

    A_long Int(PF_ParamIndex idx)       { PF_ParamDef p; return Checkout(idx, p) ? Done(p, p.u.sd.value) : 0; }
    float  Float(PF_ParamIndex idx)     { PF_ParamDef p; return Checkout(idx, p) ? Done(p, static_cast<float>(p.u.fs_d.value)) : 0.f; }
    bool   Bool(PF_ParamIndex idx)      { PF_ParamDef p; return Checkout(idx, p) ? Done(p, p.u.bd.value != 0) : false; }
    A_long Popup(PF_ParamIndex idx)     { PF_ParamDef p; return Checkout(idx, p) ? Done(p, p.u.pd.value) : 1; }
    float  AngleDeg(PF_ParamIndex idx)  { PF_ParamDef p; return Checkout(idx, p) ? Done(p, static_cast<float>(p.u.ad.value) / 65536.f) : 0.f; }
    bt::Color Color(PF_ParamIndex idx)
    {
        PF_ParamDef p;
        if (!Checkout(idx, p)) return {};
        const PF_Pixel c = p.u.cd.value;
        return Done(p, bt::Color{c.red / 255.f, c.green / 255.f, c.blue / 255.f});
    }

private:
    bool Checkout(PF_ParamIndex idx, PF_ParamDef& p)
    {
        if (err_) return false;
        AEFX_CLR_STRUCT(p);
        err_ = PF_CHECKOUT_PARAM(in_data_, idx, in_data_->current_time, in_data_->time_step,
                                 in_data_->time_scale, &p);
        return err_ == PF_Err_NONE;
    }
    template <typename T>
    T Done(PF_ParamDef& p, T value)
    {
        const PF_Err e = PF_CHECKIN_PARAM(in_data_, &p);
        if (!err_) err_ = e;
        return value;
    }

    PF_InData* in_data_;
    PF_Err     err_ = PF_Err_NONE;
};

PF_Err ReadSettings(PF_InData* in_data, bt::Settings& s)
{
    ParamReader r(in_data);

    s.framesBackward  = static_cast<int>(std::lround(r.Float(BT_FRAMES_BACKWARD)));
    s.framesForward   = static_cast<int>(std::lround(r.Float(BT_FRAMES_FORWARD)));
    s.frameStep       = r.Int(BT_FRAME_STEP);
    s.includeCurrent  = r.Bool(BT_INCLUDE_CURRENT);
    s.currentUntinted = r.Bool(BT_CURRENT_UNTINTED);

    s.groups          = r.Int(BT_GROUPS);
    s.groupMode       = static_cast<bt::GroupMode>(r.Popup(BT_GROUP_MODE));
    s.colorOrder      = static_cast<bt::ColorOrder>(r.Popup(BT_COLOR_ORDER));
    s.groupSoftness   = r.Float(BT_GROUP_SOFTNESS) / 100.f;
    s.colorMode       = static_cast<bt::ColorMode>(r.Popup(BT_COLOR_MODE));
    s.hueStart        = r.AngleDeg(BT_HUE_START);
    s.hueRange        = r.Float(BT_HUE_RANGE);
    s.saturation      = r.Float(BT_SATURATION) / 100.f;
    s.reverseColors   = r.Bool(BT_REVERSE_COLORS);
    s.customCount     = r.Int(BT_PALETTE_SIZE);
    for (int i = 0; i < bt::kMaxCustomColor; ++i) {
        s.custom[i] = r.Color(static_cast<PF_ParamIndex>(BT_COLOR_1 + i));
    }

    s.falloffBackward = r.Float(BT_FALLOFF_BACKWARD) / 100.f;
    s.falloffForward  = r.Float(BT_FALLOFF_FORWARD) / 100.f;
    s.falloffCurve    = r.Float(BT_FALLOFF_CURVE);

    s.tintMode        = static_cast<bt::TintMode>(r.Popup(BT_TINT_MODE));
    s.blendMode       = static_cast<bt::BlendMode>(r.Popup(BT_BLEND_MODE));
    s.colorBalance    = r.Float(BT_COLOR_BALANCE) / 100.f;
    s.intensity       = r.Float(BT_INTENSITY) / 100.f;
    s.mix             = 1.f - r.Float(BT_BLEND_ORIGINAL) / 100.f;  // mix = share of the effect

    return r.err();
}

PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data)
{
    PF_Err      err = PF_Err_NONE;
    PF_ParamDef def;

    // --- Time sampling -----------------------------------------------------
    // Independent counts for each direction: numeric field + slider over the full
    // range, whole frames only (precision 0), keyframable.
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Frames Backward", 0, bt::kMaxFrames, 0, bt::kMaxFrames, 4, 0,
                         PF_ValueDisplayFlag_NONE, 0, ID_FRAMES_BACKWARD);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Frames Forward", 0, bt::kMaxFrames, 0, bt::kMaxFrames, 4, 0,
                         PF_ValueDisplayFlag_NONE, 0, ID_FRAMES_FORWARD);

    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Frame Step", 1, 100, 1, 10, 1, ID_FRAME_STEP);

    // What to use for samples before the first / after the last frame of the clip.
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUPX("Clip Edges", 2, BT_EDGE_HOLD,
                  "Hold First/Last Frame|Transparent", 0, ID_EDGE_MODE);

    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOXX("Include Current Frame", TRUE, PF_ParamFlag_SUPERVISE, ID_INCLUDE_CURRENT);

    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOXX("Keep Current Untinted", FALSE, 0, ID_CURRENT_UNTINTED);

    // --- Groups & colours --------------------------------------------------
    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Groups & Colors", ID_COLOR_TOPIC_START);

    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Number of Groups", 1, bt::kMaxGroups, 1, bt::kMaxGroups, 7, ID_GROUPS);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUPX("Group Order", 2, bt::GroupMode_SEQUENTIAL,
                  "Sequential (by time)|Cyclic", PF_ParamFlag_SUPERVISE, ID_GROUP_MODE);

    // Color 1 at the current frame (outwards) or at the earliest frame.
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUPX("Color Order", 2, bt::ColorOrder_OUTWARD,
                  "From Current Frame|Past to Future", 0, ID_COLOR_ORDER);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Group Softness", 0, 100, 0, 100, 0, 1,
                         PF_ValueDisplayFlag_PERCENT, 0, ID_GROUP_SOFTNESS);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUPX("Color Mode", 3, bt::ColorMode_SPECTRUM,
                  "Spectrum|RGB Tri-Color|Custom Palette", PF_ParamFlag_SUPERVISE, ID_COLOR_MODE);

    AEFX_CLR_STRUCT(def);
    PF_ADD_ANGLE("Hue Start", 0, ID_HUE_START);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Hue Range", 0, 1080, 0, 360, 300, 1,
                         PF_ValueDisplayFlag_NONE, 0, ID_HUE_RANGE);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Saturation", 0, 100, 0, 100, 100, 1,
                         PF_ValueDisplayFlag_PERCENT, 0, ID_SATURATION);

    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOXX("Reverse Colors", FALSE, 0, ID_REVERSE_COLORS);

    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Palette Size", 1, bt::kMaxCustomColor, 1, bt::kMaxCustomColor, 3, ID_PALETTE_SIZE);

    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("Color 1", 255, 0, 0, ID_COLOR_1);
    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("Color 2", 0, 255, 0, ID_COLOR_2);
    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("Color 3", 0, 0, 255, ID_COLOR_3);
    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("Color 4", 255, 255, 0, ID_COLOR_4);
    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("Color 5", 0, 255, 255, ID_COLOR_5);
    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("Color 6", 255, 0, 255, ID_COLOR_6);

    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(ID_COLOR_TOPIC_END);

    // --- Falloff -----------------------------------------------------------
    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Falloff", ID_FALLOFF_TOPIC_START);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Backward Falloff", 0, 100, 0, 100, 50, 1,
                         PF_ValueDisplayFlag_PERCENT, 0, ID_FALLOFF_BACKWARD);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Forward Falloff", 0, 100, 0, 100, 50, 1,
                         PF_ValueDisplayFlag_PERCENT, 0, ID_FALLOFF_FORWARD);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Falloff Curve", 0.1, 10, 0.1, 4, 1, 2,
                         PF_ValueDisplayFlag_NONE, 0, ID_FALLOFF_CURVE);

    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(ID_FALLOFF_TOPIC_END);

    // --- Compositing -------------------------------------------------------
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUPX("Tint Mode", 2, bt::TintMode_FILTER,
                  "Color Filter|Luminance Tint", 0, ID_TINT_MODE);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUPX("Blend Mode", 5, bt::BlendMode_OPTICAL,
                  "Optical (Balanced)|Additive|Screen|Lighten|Tinted Trails", PF_ParamFlag_SUPERVISE, ID_BLEND_MODE);

    // Optical only: how strictly static areas keep their original colour.
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Color Balance", 0, 100, 0, 100, 100, 1,
                         PF_ValueDisplayFlag_PERCENT, 0, ID_COLOR_BALANCE);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Intensity", 0, 1000, 0, 400, 100, 1,
                         PF_ValueDisplayFlag_PERCENT, 0, ID_INTENSITY);

    AEFX_CLR_STRUCT(def);
    // AE convention: 0% = full effect, 100% = original layer.
    PF_ADD_FLOAT_SLIDERX("Blend With Original", 0, 100, 0, 100, 0, 1,
                         PF_ValueDisplayFlag_PERCENT, 0, ID_BLEND_ORIGINAL);

    // Off = always render on the CPU (After Effects then skips the GPU path).
    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOXX("GPU Acceleration", TRUE, 0, ID_USE_GPU);

    out_data->num_params = BT_NUM_PARAMS;
    return err;
}

// Greys out controls that do not apply to the current modes.
PF_Err UpdateParamsUI(PF_InData* in_data, PF_ParamDef* params[])
{
    PF_Err err = PF_Err_NONE;
    SuiteScope<PF_ParamUtilsSuite3> pu(in_data, kPFParamUtilsSuite, kPFParamUtilsSuiteVersion3);
    if (!pu) return PF_Err_NONE;  // cosmetic only

    const A_long colorMode  = params[BT_COLOR_MODE]->u.pd.value;
    const bool   sequential = params[BT_GROUP_MODE]->u.pd.value == bt::GroupMode_SEQUENTIAL;
    const bool   current    = params[BT_INCLUDE_CURRENT]->u.bd.value != 0;

    auto setEnabled = [&](PF_ParamIndex idx, bool enabled) {
        if (err) return;
        PF_ParamDef copy = *params[idx];
        const bool isEnabled = (copy.ui_flags & PF_PUI_DISABLED) == 0;
        if (isEnabled == enabled) return;
        if (enabled) copy.ui_flags &= ~PF_PUI_DISABLED;
        else         copy.ui_flags |= PF_PUI_DISABLED;
        err = pu->PF_UpdateParamUI(in_data->effect_ref, idx, &copy);
    };

    setEnabled(BT_CURRENT_UNTINTED, current);
    setEnabled(BT_GROUP_SOFTNESS, sequential);
    setEnabled(BT_COLOR_BALANCE, params[BT_BLEND_MODE]->u.pd.value == bt::BlendMode_OPTICAL);

    const bool spectrum = colorMode == bt::ColorMode_SPECTRUM;
    setEnabled(BT_HUE_START, spectrum);
    setEnabled(BT_HUE_RANGE, spectrum);
    setEnabled(BT_SATURATION, spectrum);

    const bool custom = colorMode == bt::ColorMode_CUSTOM;
    setEnabled(BT_PALETTE_SIZE, custom);
    for (int i = 0; i < bt::kMaxCustomColor; ++i) {
        setEnabled(static_cast<PF_ParamIndex>(BT_COLOR_1 + i), custom);
    }
    return err;
}

// ---------------------------------------------------------------------------
// Pixel formats
// ---------------------------------------------------------------------------

template <typename P> struct PixelTraits;
template <> struct PixelTraits<PF_Pixel8> {
    static constexpr float kMax   = 255.f;
    static constexpr bool  kClamp = true;
    using Chan                    = A_u_char;
};
template <> struct PixelTraits<PF_Pixel16> {
    static constexpr float kMax   = static_cast<float>(PF_MAX_CHAN16);
    static constexpr bool  kClamp = true;
    using Chan                    = A_u_short;
};
template <> struct PixelTraits<PF_PixelFloat> {
    static constexpr float kMax   = 1.f;
    static constexpr bool  kClamp = false;
    using Chan                    = PF_FpShort;
};

// Premiere Pro native formats (PrPixelFormat_BGRA_4444_8u / _32f): B G R A order.
struct PixelBGRA8 {
    A_u_char blue, green, red, alpha;
};
struct PixelBGRA32f {
    float blue, green, red, alpha;
};
template <> struct PixelTraits<PixelBGRA8> {
    static constexpr float kMax   = 255.f;
    static constexpr bool  kClamp = true;
    using Chan                    = A_u_char;
};
template <> struct PixelTraits<PixelBGRA32f> {
    static constexpr float kMax   = 1.f;
    static constexpr bool  kClamp = false;
    using Chan                    = float;
};

// Memory layout of a world's pixels.
enum PixKind { kPixUnknown = 0, kPixARGB8, kPixARGB16, kPixARGB32f, kPixBGRA8, kPixBGRA32f };

// AE pixels are straight (un-premultiplied); the core works premultiplied.
template <typename P>
inline void LoadPremul(const P& p, float out[4])
{
    constexpr float s = 1.f / PixelTraits<P>::kMax;
    const float a = bt::Clamp01(static_cast<float>(p.alpha) * s);
    out[0] = static_cast<float>(p.red)   * s * a;
    out[1] = static_cast<float>(p.green) * s * a;
    out[2] = static_cast<float>(p.blue)  * s * a;
    out[3] = a;
}

template <typename P>
inline typename PixelTraits<P>::Chan ToChan(float v)
{
    using T = PixelTraits<P>;
    if (T::kClamp) {
        v = bt::Clamp01(v) * T::kMax + 0.5f;
    }
    return static_cast<typename T::Chan>(v);
}

template <typename P>
inline void StoreUnpremul(const float in[4], P& p)
{
    const float a   = in[3];
    const float inv = a > 1e-6f ? 1.f / a : 0.f;
    p.alpha = ToChan<P>(a);
    p.red   = ToChan<P>(in[0] * inv);
    p.green = ToChan<P>(in[1] * inv);
    p.blue  = ToChan<P>(in[2] * inv);
}

template <typename P>
inline const P* RowPtr(const PF_EffectWorld* w, A_long y)
{
    return reinterpret_cast<const P*>(reinterpret_cast<const char*>(w->data) + y * w->rowbytes);
}

template <typename P>
inline P* RowPtr(PF_EffectWorld* w, A_long y)
{
    return reinterpret_cast<P*>(reinterpret_cast<char*>(w->data) + y * w->rowbytes);
}

// Maps an output row/column range onto a source world. Worlds may have different
// sizes and origins (layer-space offsets) when the layer is cropped or trimmed.
struct SourceSpan {
    A_long sy = -1, dx = 0, x0 = 0, x1 = 0;  // x0..x1 = valid output columns
    bool valid() const { return sy >= 0 && x1 > x0; }
};

inline SourceSpan MapRow(const PF_EffectWorld* out, const PF_EffectWorld* src, A_long y)
{
    SourceSpan s;
    if (!src || !src->data) return s;
    const A_long sy = y + out->origin_y - src->origin_y;
    if (sy < 0 || sy >= src->height) return s;
    s.sy = sy;
    s.dx = out->origin_x - src->origin_x;
    s.x0 = std::max<A_long>(0, -s.dx);
    s.x1 = std::min<A_long>(out->width, src->width - s.dx);
    return s;
}

// ---------------------------------------------------------------------------
// Row workers (run in parallel through iterate_generic)
// ---------------------------------------------------------------------------

struct AccumulateCtx {
    const PF_EffectWorld* out;
    const PF_EffectWorld* src;
    const bt::Sample*     sample;
    bt::BlendMode         blend;
    bt::TintMode          tint;
    float*                acc;
};

template <typename P>
PF_Err AccumulateRowFn(void* refcon, A_long /*thread*/, A_long y, A_long /*iterations*/)
{
    const AccumulateCtx& c = *static_cast<const AccumulateCtx*>(refcon);
    const SourceSpan     s = MapRow(c.out, c.src, y);
    if (!s.valid()) return PF_Err_NONE;

    const P* row = RowPtr<P>(c.src, s.sy);
    float*   acc = c.acc + 4 * (static_cast<size_t>(y) * c.out->width + s.x0);
    float    px[4];
    for (A_long x = s.x0; x < s.x1; ++x, acc += 4) {
        LoadPremul(row[x + s.dx], px);
        bt::AccumulatePixel(c.blend, c.tint, *c.sample, px[0], px[1], px[2], px[3], acc);
    }
    return PF_Err_NONE;
}

using RowFn = PF_Err (*)(void*, A_long, A_long, A_long);

// Row reader for a source world of the given layout (sources may differ from
// the output, e.g. in Premiere Pro when checked-out frames keep their format).
RowFn AccumulateFnFor(PixKind kind)
{
    switch (kind) {
        case kPixARGB8:   return AccumulateRowFn<PF_Pixel8>;
        case kPixARGB16:  return AccumulateRowFn<PF_Pixel16>;
        case kPixARGB32f: return AccumulateRowFn<PF_PixelFloat>;
        case kPixBGRA8:   return AccumulateRowFn<PixelBGRA8>;
        case kPixBGRA32f: return AccumulateRowFn<PixelBGRA32f>;
        default:          return nullptr;
    }
}

struct FinalizeCtx {
    PF_EffectWorld*       out;
    const PF_EffectWorld* current;  // may be null
    PixKind               currentKind;
    bt::BlendMode         blend;
    float                 mix;
    float                 gain;
    const float*          acc;
};

template <typename P, typename PCur>
void FinalizeRow(const FinalizeCtx& c, A_long y)
{
    const SourceSpan s   = MapRow(c.out, c.current, y);
    const PCur*      cur = s.valid() ? RowPtr<PCur>(c.current, s.sy) : nullptr;
    P*               dst = RowPtr<P>(c.out, y);
    const float*     acc = c.acc + 4 * static_cast<size_t>(y) * c.out->width;

    float orig[4], res[4];
    for (A_long x = 0; x < c.out->width; ++x, acc += 4) {
        const bool hasOrig = cur && x >= s.x0 && x < s.x1;
        if (hasOrig) LoadPremul(cur[x + s.dx], orig);
        bt::FinalizePixel(c.blend, c.mix, acc, hasOrig ? orig : nullptr, res, c.gain);
        StoreUnpremul(res, dst[x]);
    }
}

template <typename P>
PF_Err FinalizeRowFn(void* refcon, A_long /*thread*/, A_long y, A_long /*iterations*/)
{
    const FinalizeCtx& c = *static_cast<const FinalizeCtx*>(refcon);
    switch (c.currentKind) {
        case kPixARGB16:  FinalizeRow<P, PF_Pixel16>(c, y); break;
        case kPixARGB32f: FinalizeRow<P, PF_PixelFloat>(c, y); break;
        case kPixBGRA8:   FinalizeRow<P, PixelBGRA8>(c, y); break;
        case kPixBGRA32f: FinalizeRow<P, PixelBGRA32f>(c, y); break;
        case kPixARGB8:
        default:          FinalizeRow<P, PF_Pixel8>(c, y); break;
    }
    return PF_Err_NONE;
}

// Runs fn for every row: multi-threaded through the Iterate8 suite, or serially
// when the host does not provide it (some suites are missing in Premiere Pro).
PF_Err ForEachRow(PF_InData* in_data, A_long rows, void* ctx, RowFn fn)
{
    SuiteScope<PF_Iterate8Suite2> iter(in_data, kPFIterate8Suite, kPFIterate8SuiteVersion2);
    if (iter) return iter->iterate_generic(rows, ctx, fn);
    for (A_long y = 0; y < rows; ++y) {
        if (const PF_Err e = fn(ctx, 0, y, rows)) return e;
    }
    return PF_Err_NONE;
}

// Supplies the sample frames to the CPU renderer.
class FrameSource {
public:
    virtual ~FrameSource() = default;
    virtual PF_Err Get(size_t i, const PF_EffectWorld** world, PixKind* kind) = 0;
    virtual PF_Err Release(size_t i) = 0;
};

// After Effects (SmartFX): frames checked out in PreRender.
class SmartFrameSource : public FrameSource {
public:
    SmartFrameSource(PF_InData* in_data, PF_SmartRenderExtra* extra, const RenderData& rd, PixKind kind)
        : in_data_(in_data), extra_(extra), rd_(rd), kind_(kind) {}
    PF_Err Get(size_t i, const PF_EffectWorld** world, PixKind* kind) override
    {
        PF_EffectWorld* w = nullptr;
        const PF_Err    e = extra_->cb->checkout_layer_pixels(in_data_->effect_ref, rd_.checkoutIds[i], &w);
        *world = w;
        *kind  = kind_;  // SmartFX: inputs match the output format
        return e;
    }
    PF_Err Release(size_t i) override
    {
        return extra_->cb->checkin_layer_pixels(in_data_->effect_ref, rd_.checkoutIds[i]);
    }

private:
    PF_InData*           in_data_;
    PF_SmartRenderExtra* extra_;
    const RenderData&    rd_;
    PixKind              kind_;
};

template <typename P>
PF_Err RenderWorld(PF_InData* in_data, FrameSource& frames, const RenderData& rd,
                   PF_EffectWorld* out, const PF_EffectWorld* current, PixKind currentKind)
{
    PF_Err err = PF_Err_NONE, err2 = PF_Err_NONE;
    if (out->width <= 0 || out->height <= 0) return err;

    const bt::Settings& s = rd.settings;
    std::vector<float>  acc(4 * static_cast<size_t>(out->width) * out->height);
    bt::InitAccumRow(s.blendMode, acc.data(), out->width * out->height);

    for (size_t i = 0; i < rd.samples.size() && !err; ++i) {
        const PF_EffectWorld* src  = nullptr;
        PixKind               kind = kPixUnknown;
        ERR(frames.Get(i, &src, &kind));
        if (!err && src && src->data) {
            const RowFn fn = AccumulateFnFor(kind);
            if (!fn) err = PF_Err_UNRECOGNIZED_PARAM_TYPE;
            AccumulateCtx ctx{out, src, &rd.samples[i], s.blendMode, s.tintMode, acc.data()};
            ERR(ForEachRow(in_data, out->height, &ctx, fn));
        }
        ERR2(frames.Release(i));
        ERR(PF_ABORT(in_data));
    }

    if (!err) {
        FinalizeCtx ctx{out, current, currentKind, s.blendMode, s.mix, s.intensity, acc.data()};
        ERR(ForEachRow(in_data, out->height, &ctx, FinalizeRowFn<P>));
    }
    return err;
}

// ---------------------------------------------------------------------------
// GPU path
// ---------------------------------------------------------------------------

static_assert(BT_BLEND_OPTICAL == bt::BlendMode_OPTICAL && BT_BLEND_ADDITIVE == bt::BlendMode_ADDITIVE &&
              BT_BLEND_SCREEN == bt::BlendMode_SCREEN && BT_BLEND_LIGHTEN == bt::BlendMode_LIGHTEN &&
              BT_BLEND_TRAILS == bt::BlendMode_TRAILS,
              "GPU blend constants out of sync");
static_assert(BT_TINT_FILTER == bt::TintMode_FILTER && BT_TINT_LUMA == bt::TintMode_LUMA,
              "GPU tint constants out of sync");

constexpr A_long kGpuBytesPerPixel = 4 * sizeof(float);  // PF_PixelFormat_GPU_BGRA128

btgpu::Device MakeDevice(int framework, const PF_GPUDeviceInfo& info, const void* data)
{
    btgpu::Device d;
    d.framework = static_cast<btgpu::Framework>(framework);
    d.device    = info.devicePV;
    d.context   = info.contextPV;
    d.queue     = info.command_queuePV;
    d.data      = data;
    return d;
}

PF_Err GpuDeviceSetup(PF_InData* in_data, PF_OutData* out_data, PF_GPUDeviceSetupExtra* extra)
{
    PF_Err err = PF_Err_NONE;
    extra->output->gpu_data = nullptr;
    out_data->out_flags2 &= ~PF_OutFlag2_SUPPORTS_GPU_RENDER_F32;

    const int framework = extra->input->what_gpu;
    if (!btgpu::IsSupported(framework)) return err;  // AE falls back to the CPU path

    SuiteScope<PF_GPUDeviceSuite1> gpu(in_data, kPFGPUDeviceSuite, kPFGPUDeviceSuiteVersion1);
    if (!gpu) return err;

    PF_GPUDeviceInfo info;
    AEFX_CLR_STRUCT(info);
    ERR(gpu->GetDeviceInfo(in_data->effect_ref, extra->input->device_index, &info));

    void* data = nullptr;
    if (!err && btgpu::Setup(MakeDevice(framework, info, nullptr), &data)) {
        extra->output->gpu_data = data;
        out_data->out_flags2    = PF_OutFlag2_SUPPORTS_GPU_RENDER_F32;
    }
    return err;
}

PF_Err GpuDeviceSetdown(PF_GPUDeviceSetdownExtra* extra)
{
    btgpu::Teardown(extra->input->what_gpu, extra->input->gpu_data);
    return PF_Err_NONE;
}

// Source mapping for a GPU world (same convention as MapRow on the CPU).
void MapGpuSource(const PF_EffectWorld* out, const PF_EffectWorld* src, void* mem, BtGpuParams& p)
{
    p.hasSrc = (src && mem) ? 1 : 0;
    if (!p.hasSrc) return;
    p.srcPitch  = src->rowbytes / kGpuBytesPerPixel;
    p.srcWidth  = src->width;
    p.srcHeight = src->height;
    p.srcDx     = out->origin_x - src->origin_x;
    p.srcDy     = out->origin_y - src->origin_y;
}

PF_Err RenderGPU(PF_InData* in_data, PF_SmartRenderExtra* extra, const RenderData& rd,
                 PF_EffectWorld* out, PF_EffectWorld* current)
{
    PF_Err err = PF_Err_NONE, err2 = PF_Err_NONE;

    SuiteScope<PF_GPUDeviceSuite1> gpu(in_data, kPFGPUDeviceSuite, kPFGPUDeviceSuiteVersion1);
    SuiteScope<PF_WorldSuite2>     ws(in_data, kPFWorldSuite, kPFWorldSuiteVersion2);
    if (!gpu || !ws) return PF_Err_BAD_CALLBACK_PARAM;

    PF_PixelFormat fmt = PF_PixelFormat_INVALID;
    ERR(ws->PF_GetPixelFormat(out, &fmt));
    if (!err && fmt != PF_PixelFormat_GPU_BGRA128) err = PF_Err_UNRECOGNIZED_PARAM_TYPE;

    PF_GPUDeviceInfo info;
    AEFX_CLR_STRUCT(info);
    ERR(gpu->GetDeviceInfo(in_data->effect_ref, extra->input->device_index, &info));
    const btgpu::Device dev = MakeDevice(extra->input->what_gpu, info, extra->input->gpu_data);

    void* dstMem = nullptr;
    ERR(gpu->GetGPUWorldData(in_data->effect_ref, out, &dstMem));
    if (err) return err;

    const bt::Settings& s = rd.settings;
    BtGpuParams base;
    std::memset(&base, 0, sizeof(base));
    base.width    = out->width;
    base.height   = out->height;
    base.dstPitch = out->rowbytes / kGpuBytesPerPixel;
    base.blend    = s.blendMode;
    base.tint     = s.tintMode;
    base.mix      = s.mix;
    base.gain     = s.intensity;

    auto dispatch = [&](int kernel, void* src, const BtGpuParams& p) {
        if (!err && !btgpu::Dispatch(dev, kernel, src, dstMem, p)) err = PF_Err_INTERNAL_STRUCT_DAMAGED;
    };

    // The output world is the accumulation buffer.
    BtGpuParams init = base;
    init.initValue   = (s.blendMode == bt::BlendMode_SCREEN) ? 1.f : 0.f;
    dispatch(BT_KERNEL_INIT, nullptr, init);

    for (size_t i = 0; i < rd.samples.size() && !err; ++i) {
        PF_EffectWorld* src    = nullptr;
        void*           srcMem = nullptr;
        ERR(extra->cb->checkout_layer_pixels(in_data->effect_ref, rd.checkoutIds[i], &src));
        if (!err && src) ERR(gpu->GetGPUWorldData(in_data->effect_ref, src, &srcMem));
        if (!err && srcMem) {
            const bt::Sample& k = rd.samples[i];
            BtGpuParams       p = base;
            MapGpuSource(out, src, srcMem, p);
            p.kr = k.kr; p.kg = k.kg; p.kb = k.kb; p.ka = k.ka;
            dispatch(BT_KERNEL_ACCUMULATE, srcMem, p);
        }
        ERR2(extra->cb->checkin_layer_pixels(in_data->effect_ref, rd.checkoutIds[i]));
        ERR(PF_ABORT(in_data));
    }

    void* currentMem = nullptr;
    if (!err && current) ERR(gpu->GetGPUWorldData(in_data->effect_ref, current, &currentMem));
    if (!err) {
        BtGpuParams p = base;
        MapGpuSource(out, current, currentMem, p);
        dispatch(BT_KERNEL_FINALIZE, currentMem, p);
    }
    return err;
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

PF_Err About(PF_OutData* out_data)
{
    std::snprintf(out_data->return_msg, sizeof(out_data->return_msg),
                  "%s v%d.%d\r%s", BT_NAME, BT_MAJOR_VERSION, BT_MINOR_VERSION, BT_DESCRIPTION);
    return PF_Err_NONE;
}

PF_Err GlobalSetup(PF_InData* in_data, PF_OutData* out_data)
{
    out_data->my_version = PF_VERSION(BT_MAJOR_VERSION, BT_MINOR_VERSION, BT_BUG_VERSION,
                                      BT_STAGE_VERSION, BT_BUILD_VERSION);

    // Must match AE_Effect_Global_OutFlags / _2 in BrightTimesPiPL.r.
    out_data->out_flags  = PF_OutFlag_WIDE_TIME_INPUT |
                           PF_OutFlag_DEEP_COLOR_AWARE |
                           PF_OutFlag_SEND_UPDATE_PARAMS_UI;
    out_data->out_flags2 = PF_OutFlag2_SUPPORTS_SMART_RENDER |
                           PF_OutFlag2_FLOAT_COLOR_AWARE |
                           PF_OutFlag2_AUTOMATIC_WIDE_TIME_INPUT |
                           PF_OutFlag2_SUPPORTS_THREADED_RENDERING |
                           PF_OutFlag2_SUPPORTS_GPU_RENDER_F32;

    // Premiere Pro supports neither SmartFX nor the After Effects GPU path: it
    // uses the classic PF_Cmd_RENDER (ClassicRender) with 8-bit frames.
    if (IsPremiere(in_data)) {
        out_data->out_flags  = PF_OutFlag_WIDE_TIME_INPUT | PF_OutFlag_SEND_UPDATE_PARAMS_UI;
        out_data->out_flags2 = 0;

        // Render in Premiere's native BGRA, 32-bit float when the sequence asks
        // for maximum depth, 8-bit otherwise.
        SuiteScope<PF_PixelFormatSuite1> pf(in_data, kPFPixelFormatSuite, kPFPixelFormatSuiteVersion1);
        if (pf) {
            pf->ClearSupportedPixelFormats(in_data->effect_ref);
            pf->AddSupportedPixelFormat(in_data->effect_ref, PrPixelFormat_BGRA_4444_32f);
            pf->AddSupportedPixelFormat(in_data->effect_ref, PrPixelFormat_BGRA_4444_8u);
        }
        // Neighbouring frames from PF_CHECKOUT_PARAM in the same format as the output
        // (otherwise they arrive as 8-bit ARGB; the renderer handles both).
        SuiteScope<PF_UtilitySuite4> util(in_data, kPFUtilitySuite, kPFUtilitySuiteVersion4);
        if (util) util->EffectWantsCheckedOutFramesToMatchRenderPixelFormat(in_data->effect_ref);
    }
    return PF_Err_NONE;
}

inline void UnionRect(const PF_LRect& src, PF_LRect& dst)
{
    if (src.left >= src.right || src.top >= src.bottom) return;
    if (dst.left >= dst.right || dst.top >= dst.bottom) { dst = src; return; }
    dst.left   = std::min(dst.left, src.left);
    dst.top    = std::min(dst.top, src.top);
    dst.right  = std::max(dst.right, src.right);
    dst.bottom = std::max(dst.bottom, src.bottom);
}

PF_Err PreRender(PF_InData* in_data, PF_PreRenderExtra* extra)
{
    PF_Err err = PF_Err_NONE;

    std::unique_ptr<RenderData> rd(new RenderData);
    ERR(ReadSettings(in_data, rd->settings));
    if (err) return err;
    bool holdEdges = true;
    {
        ParamReader r(in_data);
        rd->useGpu = r.Bool(BT_USE_GPU);
        holdEdges  = r.Popup(BT_EDGE_MODE) == BT_EDGE_HOLD;
        ERR(r.err());
        if (err) return err;
    }
    rd->samples = bt::BuildSamples(rd->settings);

    const PF_RenderRequest req = extra->input->output_request;
    PF_LRect result = {0, 0, 0, 0}, maxResult = {0, 0, 0, 0};

    // Returns true if the layer has content at time t.
    auto checkout = [&](A_long id, A_long t) -> bool {
        if (err) return false;
        PF_CheckoutResult cr;
        AEFX_CLR_STRUCT(cr);
        err = extra->cb->checkout_layer(in_data->effect_ref, BT_INPUT, id, &req, t,
                                        in_data->time_step, in_data->time_scale, &cr);
        if (err) return false;
        UnionRect(cr.result_rect, result);
        UnionRect(cr.max_result_rect, maxResult);
        return cr.result_rect.right > cr.result_rect.left && cr.result_rect.bottom > cr.result_rect.top;
    };

    // Unmodified current frame: used for Mix and as fallback extent.
    checkout(kCurrentFrameCheckoutId, in_data->current_time);

    // Hold First/Last Frame: samples outside the clip use its first / last frame
    // instead of transparent pixels (which darken the result near the edges).
    //  1. clamp sample times to the layer duration [0, total_time - one frame];
    //  2. if a sample still has no content (e.g. trimmed footage), re-check it
    //     out at the nearest sample time that has content, walking outwards
    //     from the current frame.
    const A_long step  = in_data->time_step;
    const bool   clamp = holdEdges && step > 0 && in_data->total_time > 0;
    const A_long tLast = in_data->total_time - step;
    auto sampleTime = [&](const bt::Sample& smp) {
        A_long t = in_data->current_time + smp.frameOffset * step;
        if (clamp) t = std::max<A_long>(0, std::min(t, tLast));
        return t;
    };

    const size_t n = rd->samples.size();
    rd->checkoutIds.assign(n, 0);
    // Past samples nearest-first, then future samples nearest-first.
    std::vector<size_t> order;
    for (size_t i = n; i-- > 0;) if (rd->samples[i].frameOffset < 0) order.push_back(i);
    const size_t firstFuture = order.size();
    for (size_t i = 0; i < n; ++i) if (rd->samples[i].frameOffset >= 0) order.push_back(i);

    A_long lastGood = in_data->current_time;
    for (size_t k = 0; k < order.size() && !err; ++k) {
        if (k == firstFuture) lastGood = in_data->current_time;  // restart for the future side
        const size_t i = order[k];
        const A_long t = sampleTime(rd->samples[i]);
        rd->checkoutIds[i] = static_cast<A_long>(i);
        if (checkout(static_cast<A_long>(i), t)) {
            lastGood = t;
        } else if (!err && holdEdges && t != lastGood) {
            rd->checkoutIds[i] = kHeldFrameCheckoutBase + static_cast<A_long>(i);
            checkout(rd->checkoutIds[i], lastGood);
        }
    }
    if (err) return err;

    if (rd->useGpu && extra->input->what_gpu != PF_GPU_Framework_NONE &&
        btgpu::IsSupported(extra->input->what_gpu)) {
        extra->output->flags |= PF_RenderOutputFlag_GPU_RENDER_POSSIBLE;
    }
    extra->output->result_rect                = result;
    extra->output->max_result_rect            = maxResult;
    extra->output->pre_render_data            = rd.release();
    extra->output->delete_pre_render_data_func = DeleteRenderData;
    return err;
}

// After Effects sends PF_Cmd_SMART_RENDER_GPU when PreRender allowed the GPU and
// the device was set up, and PF_Cmd_SMART_RENDER for the CPU.
PF_Err SmartRender(PF_InData* in_data, PF_SmartRenderExtra* extra, bool gpu)
{
    PF_Err err = PF_Err_NONE;
    const RenderData* rd = static_cast<const RenderData*>(extra->input->pre_render_data);
    if (!rd) return PF_Err_BAD_CALLBACK_PARAM;

    // AE requires at least one input to be checked out before the output. The
    // unmodified current frame (needed for Mix) is held for the whole render.
    PF_Err          err2    = PF_Err_NONE;
    PF_EffectWorld* current = nullptr;
    ERR(extra->cb->checkout_layer_pixels(in_data->effect_ref, kCurrentFrameCheckoutId, &current));

    PF_EffectWorld* out = nullptr;
    ERR(extra->cb->checkout_output(in_data->effect_ref, &out));
    if (!err && !out) err = PF_Err_BAD_CALLBACK_PARAM;

    if (!err) {
        if (gpu) {
            err = (extra->input->what_gpu == PF_GPU_Framework_NONE)
                      ? PF_Err_BAD_CALLBACK_PARAM
                      : RenderGPU(in_data, extra, *rd, out, current);
        } else {
            PF_PixelFormat fmt = PF_PixelFormat_INVALID;
            SuiteScope<PF_WorldSuite2> ws(in_data, kPFWorldSuite, kPFWorldSuiteVersion2);
            if (!ws) err = PF_Err_BAD_CALLBACK_PARAM;
            ERR(ws->PF_GetPixelFormat(out, &fmt));
            if (!err) {
                switch (fmt) {
                    case PF_PixelFormat_ARGB128: {
                        SmartFrameSource frames(in_data, extra, *rd, kPixARGB32f);
                        err = RenderWorld<PF_PixelFloat>(in_data, frames, *rd, out, current, kPixARGB32f);
                        break;
                    }
                    case PF_PixelFormat_ARGB64: {
                        SmartFrameSource frames(in_data, extra, *rd, kPixARGB16);
                        err = RenderWorld<PF_Pixel16>(in_data, frames, *rd, out, current, kPixARGB16);
                        break;
                    }
                    case PF_PixelFormat_ARGB32: {
                        SmartFrameSource frames(in_data, extra, *rd, kPixARGB8);
                        err = RenderWorld<PF_Pixel8>(in_data, frames, *rd, out, current, kPixARGB8);
                        break;
                    }
                    default: err = PF_Err_BAD_CALLBACK_PARAM; break;
                }
            }
        }
    }

    ERR2(extra->cb->checkin_layer_pixels(in_data->effect_ref, kCurrentFrameCheckoutId));
    return err;
}

// ---------------------------------------------------------------------------
// Classic (non-SmartFX) render: Premiere Pro does not support SmartFX and sends
// PF_Cmd_RENDER. Neighbouring frames come from PF_CHECKOUT_PARAM on the layer.
// ---------------------------------------------------------------------------

// Pixel layout of a world in classic render. Premiere Pro reports it through the
// PF Pixel Format Suite (formats registered in GlobalSetup); otherwise the world
// is After Effects ARGB, 8 or 16 bit.
class PixelKindQuery {
public:
    explicit PixelKindQuery(PF_InData* in_data)
        : pf_(in_data, kPFPixelFormatSuite, kPFPixelFormatSuiteVersion1), premiere_(IsPremiere(in_data)) {}

    PixKind Of(PF_EffectWorld* w) const
    {
        if (premiere_ && pf_) {
            PrPixelFormat fmt = PrPixelFormat_Invalid;
            if (pf_->GetPixelFormat(w, &fmt) == PF_Err_NONE) {
                switch (fmt) {
                    case PrPixelFormat_BGRA_4444_32f: return kPixBGRA32f;
                    case PrPixelFormat_BGRA_4444_8u:  return kPixBGRA8;
                    case PrPixelFormat_ARGB_4444_8u:  return kPixARGB8;
                    default:                          return kPixUnknown;
                }
            }
        }
        return (w->world_flags & PF_WorldFlag_DEEP) ? kPixARGB16 : kPixARGB8;
    }

private:
    SuiteScope<PF_PixelFormatSuite1> pf_;
    bool                             premiere_;
};

// Copy of a world with the SmartFX-only origin cleared (unused in classic render).
PF_EffectWorld PlainWorld(const PF_EffectWorld& w)
{
    PF_EffectWorld c = w;
    c.origin_x = 0;
    c.origin_y = 0;
    return c;
}

class ParamFrameSource : public FrameSource {
public:
    ParamFrameSource(PF_InData* in_data, const RenderData& rd, bool holdEdges, const PixelKindQuery& kinds)
        : in_data_(in_data), rd_(rd), hold_(holdEdges), kinds_(kinds),
          params_(rd.samples.size()), worlds_(rd.samples.size()), checkedOut_(rd.samples.size(), false) {}

    PF_Err Get(size_t i, const PF_EffectWorld** world, PixKind* kind) override
    {
        *world = nullptr;
        *kind  = kPixUnknown;
        const A_long step = in_data_->time_step;
        A_long       t    = in_data_->current_time + rd_.samples[i].frameOffset * step;
        // Hold First/Last Frame: stay inside the clip.
        if (hold_ && step > 0 && in_data_->total_time > 0) {
            t = std::max<A_long>(0, std::min<A_long>(t, in_data_->total_time - step));
        }
        PF_Err err = Checkout(i, t);
        if (!err && hold_ && Empty(i) && t != in_data_->current_time) {
            // Still nothing there (e.g. trimmed clip): use the current frame.
            err = Release(i);
            if (!err) err = Checkout(i, in_data_->current_time);
        }
        if (!err) {
            *kind      = kinds_.Of(&params_[i].u.ld);  // query the host's own world
            worlds_[i] = PlainWorld(params_[i].u.ld);
            *world     = &worlds_[i];
        }
        return err;
    }

    PF_Err Release(size_t i) override
    {
        if (!checkedOut_[i]) return PF_Err_NONE;
        checkedOut_[i] = false;
        return PF_CHECKIN_PARAM(in_data_, &params_[i]);
    }

private:
    PF_Err Checkout(size_t i, A_long t)
    {
        AEFX_CLR_STRUCT(params_[i]);
        const PF_Err err = PF_CHECKOUT_PARAM(in_data_, BT_INPUT, t, in_data_->time_step,
                                             in_data_->time_scale, &params_[i]);
        checkedOut_[i] = (err == PF_Err_NONE);
        return err;
    }
    bool Empty(size_t i) const
    {
        const PF_LayerDef& l = params_[i].u.ld;
        return !l.data || l.width <= 0 || l.height <= 0;
    }

    PF_InData*                  in_data_;
    const RenderData&           rd_;
    bool                        hold_;
    const PixelKindQuery&       kinds_;
    std::vector<PF_ParamDef>    params_;
    std::vector<PF_EffectWorld> worlds_;
    std::vector<bool>           checkedOut_;
};

PF_Err ClassicRender(PF_InData* in_data, PF_ParamDef* params[], PF_LayerDef* output)
{
    PF_Err     err  = PF_Err_NONE;
    bool       hold = true;
    RenderData rd;
    ERR(ReadSettings(in_data, rd.settings));
    if (!err) {
        ParamReader r(in_data);
        hold = r.Popup(BT_EDGE_MODE) == BT_EDGE_HOLD;
        ERR(r.err());
    }
    if (err) return err;
    rd.samples = bt::BuildSamples(rd.settings);

    const PixelKindQuery kinds(in_data);
    const PixKind        outKind = kinds.Of(output);
    const PixKind        curKind = kinds.Of(&params[BT_INPUT]->u.ld);
    PF_EffectWorld       out     = PlainWorld(*output);  // pixels are written through out.data
    PF_EffectWorld       current = PlainWorld(params[BT_INPUT]->u.ld);
    ParamFrameSource     frames(in_data, rd, hold, kinds);

    switch (outKind) {
        case kPixBGRA32f: return RenderWorld<PixelBGRA32f>(in_data, frames, rd, &out, &current, curKind);
        case kPixBGRA8:   return RenderWorld<PixelBGRA8>(in_data, frames, rd, &out, &current, curKind);
        case kPixARGB16:  return RenderWorld<PF_Pixel16>(in_data, frames, rd, &out, &current, curKind);
        case kPixARGB8:   return RenderWorld<PF_Pixel8>(in_data, frames, rd, &out, &current, curKind);
        default:          return PF_Err_UNRECOGNIZED_PARAM_TYPE;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------------

extern "C" DllExport PF_Err PluginDataEntryFunction2(PF_PluginDataPtr inPtr,
                                                     PF_PluginDataCB2 inPluginDataCallBackPtr,
                                                     SPBasicSuite*    /*inSPBasicSuitePtr*/,
                                                     const char*      /*inHostName*/,
                                                     const char*      /*inHostVersion*/)
{
    if (!inPluginDataCallBackPtr) return PF_Err_INVALID_CALLBACK;
    // Same call PF_REGISTER_EFFECT_EXT2 expands to; done directly because the
    // macro's location differs between SDK releases (not in AE_PluginData.h in 26.5).
    // Values must match BrightTimesPiPL.r.
    return (*inPluginDataCallBackPtr)(inPtr,
                                      reinterpret_cast<const A_u_char*>(BT_NAME),
                                      reinterpret_cast<const A_u_char*>(BT_MATCH_NAME),
                                      reinterpret_cast<const A_u_char*>(BT_CATEGORY),
                                      reinterpret_cast<const A_u_char*>("EffectMain"),
                                      BT_PLUGIN_KIND_EFFECT,
                                      PF_PLUG_IN_VERSION,
                                      PF_PLUG_IN_SUBVERS,
                                      BT_RESERVED_INFO,
                                      reinterpret_cast<const A_u_char*>(BT_SUPPORT_URL));
}

extern "C" DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data,
                                       PF_ParamDef* params[], PF_LayerDef* output, void* extra)
{
    PF_Err err = PF_Err_NONE;
    try {
        switch (cmd) {
            case PF_Cmd_ABOUT:
                err = About(out_data);
                break;
            case PF_Cmd_GLOBAL_SETUP:
                err = GlobalSetup(in_data, out_data);
                break;
            case PF_Cmd_PARAMS_SETUP:
                err = ParamsSetup(in_data, out_data);
                break;
            case PF_Cmd_UPDATE_PARAMS_UI:
            case PF_Cmd_USER_CHANGED_PARAM:
                err = UpdateParamsUI(in_data, params);
                break;
            case PF_Cmd_SMART_PRE_RENDER:
                err = PreRender(in_data, static_cast<PF_PreRenderExtra*>(extra));
                break;
            case PF_Cmd_RENDER:  // hosts without SmartFX, e.g. Premiere Pro
                err = ClassicRender(in_data, params, output);
                break;
            case PF_Cmd_SMART_RENDER:
                err = SmartRender(in_data, static_cast<PF_SmartRenderExtra*>(extra), false);
                break;
            case PF_Cmd_SMART_RENDER_GPU:
                err = SmartRender(in_data, static_cast<PF_SmartRenderExtra*>(extra), true);
                break;
            case PF_Cmd_GPU_DEVICE_SETUP:
                err = GpuDeviceSetup(in_data, out_data, static_cast<PF_GPUDeviceSetupExtra*>(extra));
                break;
            case PF_Cmd_GPU_DEVICE_SETDOWN:
                err = GpuDeviceSetdown(static_cast<PF_GPUDeviceSetdownExtra*>(extra));
                break;
            default:
                break;
        }
    } catch (const std::bad_alloc&) {
        err = PF_Err_OUT_OF_MEMORY;
    } catch (PF_Err thrown) {
        err = thrown;
    } catch (...) {
        err = PF_Err_INTERNAL_STRUCT_DAMAGED;
    }
    return err;
}
