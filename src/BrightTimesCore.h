// Bright Times — core temporal colour-separation math.
//
// This header has no dependency on the After Effects SDK so the algorithm can be
// unit-tested and rendered outside of AE (see tools/examples.cpp).
//
// Principle (optical printing / "Harris shutter" style):
//   * The target frame and N neighbouring frames (backward / forward in time) are
//     sampled.
//   * The samples are split into colour groups. Each group gets its own colour
//     filter (spectrum, RGB tri-colour or custom palette).
//   * Each sample is weighted by a smooth, adjustable temporal falloff.
//   * The filtered samples are superimposed. In "Optical" mode the per-channel
//     weights are normalised so that the colour filters sum to white: everything
//     that does not move keeps its original colour, everything that moves splits
//     into coloured ghosts.
//
// All pixel math is done on premultiplied float RGBA in [0..1] (values > 1 allowed
// for 32 bpc).

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace bt {

struct Color {
    float r = 1.f, g = 1.f, b = 1.f;
};

enum ColorMode { ColorMode_SPECTRUM = 1, ColorMode_RGB, ColorMode_CUSTOM };
enum GroupMode { GroupMode_SEQUENTIAL = 1, GroupMode_CYCLIC };
// Which frames get Color 1: the ones nearest the current frame (colours run
// outwards, mirrored on both sides) or the earliest ones (past -> future).
enum ColorOrder { ColorOrder_OUTWARD = 1, ColorOrder_TIME };
enum TintMode  { TintMode_FILTER = 1, TintMode_LUMA };
enum BlendMode { BlendMode_OPTICAL = 1, BlendMode_ADDITIVE, BlendMode_SCREEN, BlendMode_LIGHTEN,
                 BlendMode_TRAILS };

constexpr int kMaxFrames      = 50;  // per direction
constexpr int kMaxGroups      = 16;
constexpr int kMaxCustomColor = 6;

struct Settings {
    // Time sampling
    int  framesBackward    = 4;
    int  framesForward     = 4;
    int  frameStep         = 1;      // distance (in frames) between samples
    bool includeCurrent    = true;
    bool currentUntinted   = false;  // current frame keeps its own colours (white filter)

    // Groups & colours
    int       groups        = 7;
    GroupMode groupMode     = GroupMode_SEQUENTIAL;
    ColorOrder colorOrder   = ColorOrder_OUTWARD;
    float     groupSoftness = 0.f;   // 0..1, blends colours of neighbouring groups
    ColorMode colorMode     = ColorMode_SPECTRUM;
    float     hueStart      = 0.f;   // degrees
    float     hueRange      = 300.f; // degrees
    float     saturation    = 1.f;   // 0..1
    bool      reverseColors = false;
    Color     custom[kMaxCustomColor];
    int       customCount   = 3;

    // Falloff
    float falloffBackward = 0.5f;    // 0..1
    float falloffForward  = 0.5f;    // 0..1
    float falloffCurve    = 1.f;     // > 0, exponent of the falloff curve

    // Compositing
    TintMode  tintMode  = TintMode_FILTER;
    BlendMode blendMode = BlendMode_OPTICAL;
    float     intensity = 1.f;       // gain
    float     mix       = 1.f;       // 0 = original, 1 = full effect
    float     colorBalance = 1.f;    // Optical only: 1 = per-channel balance, 0 = none
};

// One frame that has to be fetched and composited.
struct Sample {
    int   frameOffset = 0;           // in frames relative to the current frame
    float weight      = 1.f;         // temporal falloff weight, 0..1
    Color tint;                      // colour filter
    // Final per-channel coefficients used by the accumulator (premultiplied space).
    float kr = 0.f, kg = 0.f, kb = 0.f, ka = 0.f;
};

// ---------------------------------------------------------------------------
// Colour helpers
// ---------------------------------------------------------------------------

inline float Clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

inline Color HsvToRgb(float hueDeg, float s, float v)
{
    float h = std::fmod(hueDeg, 360.f);
    if (h < 0.f) h += 360.f;
    h /= 60.f;
    const int   i = static_cast<int>(h) % 6;
    const float f = h - std::floor(h);
    const float p = v * (1.f - s);
    const float q = v * (1.f - s * f);
    const float t = v * (1.f - s * (1.f - f));
    switch (i) {
        case 0:  return {v, t, p};
        case 1:  return {q, v, p};
        case 2:  return {p, v, t};
        case 3:  return {p, q, v};
        case 4:  return {t, p, v};
        default: return {v, p, q};
    }
}

inline Color Lerp(const Color& a, const Color& b, float t)
{
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

// Colour of group g (0..groups-1).
inline Color GroupColor(const Settings& s, int g)
{
    const int n = std::max(1, s.groups);
    g = std::max(0, std::min(n - 1, g));
    if (s.reverseColors) g = n - 1 - g;

    switch (s.colorMode) {
        case ColorMode_RGB: {
            static const Color rgb[3] = {{1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}};
            return rgb[g % 3];
        }
        case ColorMode_CUSTOM: {
            const int count = std::max(1, std::min(kMaxCustomColor, s.customCount));
            return s.custom[g % count];
        }
        case ColorMode_SPECTRUM:
        default: {
            // A full circle must not repeat the first hue on the last group.
            const bool  fullCircle = std::fabs(s.hueRange) >= 359.999f;
            const float div        = (n <= 1) ? 1.f : static_cast<float>(fullCircle ? n : n - 1);
            const float hue        = s.hueStart + s.hueRange * (n <= 1 ? 0.5f : g / div);
            return HsvToRgb(hue, Clamp01(s.saturation), 1.f);
        }
    }
}

// Smooth colour for a sample located at continuous group position `pos`
// (pos in [0, groups)). Softness 0 = hard steps, 1 = linear blend between groups.
inline Color GroupColorSmooth(const Settings& s, float pos)
{
    const int n = std::max(1, s.groups);
    if (n == 1 || s.groupSoftness <= 0.f) {
        return GroupColor(s, std::min(n - 1, static_cast<int>(std::floor(pos))));
    }
    const float centered = pos - 0.5f;               // group centres at integers
    const int   g0       = static_cast<int>(std::floor(centered));
    const float frac     = centered - g0;
    // Width of the transition zone around each group border.
    const float soft = std::max(1e-4f, std::min(1.f, s.groupSoftness));
    float t = (frac - 0.5f) / soft + 0.5f;
    t       = Clamp01(t);
    t       = t * t * (3.f - 2.f * t);                // smoothstep
    const Color a = GroupColor(s, g0);
    const Color b = GroupColor(s, g0 + 1);
    return Lerp(a, b, t);
}

// ---------------------------------------------------------------------------
// Sample list
// ---------------------------------------------------------------------------

inline float FalloffWeight(float falloff, float curve, int distance, int maxDistance)
{
    if (distance == 0 || maxDistance <= 0) return 1.f;
    // t in (0,1): the farthest sample still gets a non-zero weight.
    const float t = static_cast<float>(distance) / static_cast<float>(maxDistance + 1);
    const float c = std::max(0.01f, curve);
    return Clamp01(1.f - Clamp01(falloff) * std::pow(t, c));
}

// Builds the ordered (past -> future) list of samples, with group colours and
// temporal weights, and computes the final per-channel coefficients.
inline std::vector<Sample> BuildSamples(const Settings& sIn)
{
    Settings s     = sIn;
    s.framesBackward = std::max(0, std::min(kMaxFrames, s.framesBackward));
    s.framesForward  = std::max(0, std::min(kMaxFrames, s.framesForward));
    s.frameStep      = std::max(1, s.frameStep);
    s.groups         = std::max(1, std::min(kMaxGroups, s.groups));

    std::vector<Sample> out;
    out.reserve(s.framesBackward + s.framesForward + 1);

    for (int k = -s.framesBackward; k <= s.framesForward; ++k) {
        if (k == 0 && !s.includeCurrent) continue;
        Sample smp;
        smp.frameOffset = k * s.frameStep;
        smp.weight      = (k < 0)
            ? FalloffWeight(s.falloffBackward, s.falloffCurve, -k, s.framesBackward)
            : FalloffWeight(s.falloffForward,  s.falloffCurve,  k, s.framesForward);
        out.push_back(smp);
    }
    if (out.empty()) return out;

    // Assign colours. The untinted current frame does not take part in grouping.
    std::vector<Sample*> grouped;
    for (auto& smp : out) {
        if (smp.frameOffset == 0 && s.currentUntinted) {
            smp.tint = Color{1.f, 1.f, 1.f};
        } else {
            grouped.push_back(&smp);
        }
    }
    const int m = static_cast<int>(grouped.size());
    // rank[i]: position of the sample in colour order, 0..ranks-1.
    std::vector<int> rank(m);
    int              ranks = m;
    if (s.colorOrder == ColorOrder_OUTWARD) {
        // By distance from the current frame; both sides share a rank.
        std::vector<int> dists;
        for (const Sample* p : grouped) dists.push_back(p->frameOffset < 0 ? -p->frameOffset : p->frameOffset);
        std::vector<int> uniq = dists;
        std::sort(uniq.begin(), uniq.end());
        uniq.erase(std::unique(uniq.begin(), uniq.end()), uniq.end());
        ranks = static_cast<int>(uniq.size());
        for (int i = 0; i < m; ++i) {
            rank[i] = static_cast<int>(std::lower_bound(uniq.begin(), uniq.end(), dists[i]) - uniq.begin());
        }
    } else {
        for (int i = 0; i < m; ++i) rank[i] = i;  // samples are ordered past -> future
    }
    for (int i = 0; i < m; ++i) {
        if (s.groupMode == GroupMode_CYCLIC) {
            grouped[i]->tint = GroupColor(s, rank[i] % s.groups);
        } else {
            // Continuous position of the sample in group space: [0, groups).
            const float pos = (static_cast<float>(rank[i]) + 0.5f) * s.groups / static_cast<float>(ranks);
            grouped[i]->tint = GroupColorSmooth(s, pos);
        }
    }

    // Luminance tint uses luma*colour, filter uses rgb*colour; the coefficients
    // are the same, only the source value differs.
    double sumW = 0.0, sumR = 0.0, sumG = 0.0, sumB = 0.0, maxW = 0.0;
    for (const auto& smp : out) {
        sumW += smp.weight;
        sumR += smp.weight * smp.tint.r;
        sumG += smp.weight * smp.tint.g;
        sumB += smp.weight * smp.tint.b;
        maxW  = std::max<double>(maxW, smp.weight);
    }
    const double eps  = 1e-6;
    const float  gain = std::max(0.f, s.intensity);

    for (auto& smp : out) {
        switch (s.blendMode) {
            case BlendMode_OPTICAL: {
                // Per-channel normalisation: filters of all samples add up to white,
                // so static areas reproduce the original colour exactly.
                // Color Balance blends each channel's divisor towards the common one
                // (the brightest channel, as in Additive). With a palette that lacks
                // a channel (e.g. only blues) full balance must amplify the little
                // red/green there is, producing strong complementary fringes; lower
                // balance keeps the palette's own colours instead.
                const double bal    = Clamp01(s.colorBalance);
                const double maxSum = std::max({sumR, sumG, sumB});
                auto norm = [&](double sumC, float tintC) -> float {
                    const double den = (1.0 - bal) * maxSum + bal * sumC;
                    if (den > eps) return static_cast<float>(gain * smp.weight * tintC / den);
                    return static_cast<float>(gain * smp.weight / std::max(sumW, eps));
                };
                smp.kr = norm(sumR, smp.tint.r);
                smp.kg = norm(sumG, smp.tint.g);
                smp.kb = norm(sumB, smp.tint.b);
                smp.ka = static_cast<float>(smp.weight / std::max(sumW, eps));
                break;
            }
            case BlendMode_ADDITIVE: {
                // Common normalisation for all channels: the brightest channel of
                // the summed filters reaches 1, the colour balance is not corrected.
                const double den = std::max({sumR, sumG, sumB, eps});
                smp.kr = static_cast<float>(gain * smp.weight * smp.tint.r / den);
                smp.kg = static_cast<float>(gain * smp.weight * smp.tint.g / den);
                smp.kb = static_cast<float>(gain * smp.weight * smp.tint.b / den);
                smp.ka = static_cast<float>(smp.weight / std::max(sumW, eps));
                break;
            }
            case BlendMode_TRAILS: {
                // Ghosts at exactly the palette colours (no gain here: Intensity is
                // applied to the excess over the current frame in FinalizePixel).
                const float w = static_cast<float>(smp.weight / std::max(maxW, eps));
                smp.kr = w * smp.tint.r;
                smp.kg = w * smp.tint.g;
                smp.kb = w * smp.tint.b;
                smp.ka = w;
                break;
            }
            case BlendMode_SCREEN:
            case BlendMode_LIGHTEN:
            default: {
                const float w = static_cast<float>(smp.weight / std::max(maxW, eps));
                smp.kr = gain * w * smp.tint.r;
                smp.kg = gain * w * smp.tint.g;
                smp.kb = gain * w * smp.tint.b;
                smp.ka = w;
                break;
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Pixel accumulation (premultiplied float RGBA, 4 floats per pixel: r g b a)
// ---------------------------------------------------------------------------

inline void InitAccumRow(BlendMode mode, float* acc, int n)
{
    // Screen accumulates the product of (1 - x); all others accumulate sums / max.
    const float v = (mode == BlendMode_SCREEN) ? 1.f : 0.f;
    std::fill(acc, acc + 4 * n, v);
}

// Adds one premultiplied pixel (r, g, b, a) of sample `k` into `acc` (4 floats).
inline void AccumulatePixel(BlendMode mode, TintMode tint, const Sample& k,
                            float r, float g, float b, float a, float* acc)
{
    if (tint == TintMode_LUMA) {
        const float l = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        r = g = b = l;
    }
    const float cr = k.kr * r, cg = k.kg * g, cb = k.kb * b, ca = k.ka * a;
    switch (mode) {
        case BlendMode_SCREEN:
            acc[0] *= 1.f - Clamp01(cr);
            acc[1] *= 1.f - Clamp01(cg);
            acc[2] *= 1.f - Clamp01(cb);
            acc[3] *= 1.f - Clamp01(ca);
            break;
        case BlendMode_LIGHTEN:
        case BlendMode_TRAILS:
            acc[0] = std::max(acc[0], cr);
            acc[1] = std::max(acc[1], cg);
            acc[2] = std::max(acc[2], cb);
            acc[3] = std::max(acc[3], ca);
            break;
        default:
            acc[0] += cr;
            acc[1] += cg;
            acc[2] += cb;
            acc[3] += ca;
            break;
    }
}

// Converts an accumulated pixel into the final premultiplied pixel `dst`.
// `orig` is the premultiplied current frame pixel (nullptr = transparent), used by Mix.
// `gain` (Intensity) is only used by Tinted Trails.
inline void FinalizePixel(BlendMode mode, float mix, const float* acc, const float* orig, float* dst,
                          float gain = 1.f)
{
    static const float kZero[4] = {0.f, 0.f, 0.f, 0.f};
    float r = acc[0], g = acc[1], b = acc[2], a = acc[3];
    if (mode == BlendMode_SCREEN) {
        r = 1.f - r; g = 1.f - g; b = 1.f - b; a = 1.f - a;
    } else if (mode == BlendMode_TRAILS) {
        // Current frame untouched; tinted ghosts only where they are brighter
        // than it (per channel), scaled by Intensity. Static areas never change.
        const float* o = orig ? orig : kZero;
        r = o[0] + gain * std::max(0.f, r - o[0]);
        g = o[1] + gain * std::max(0.f, g - o[1]);
        b = o[2] + gain * std::max(0.f, b - o[2]);
        a = o[3] + gain * std::max(0.f, a - o[3]);
    }
    mix = Clamp01(mix);
    if (mix < 1.f) {
        const float* o = orig ? orig : kZero;
        r = o[0] + (r - o[0]) * mix;
        g = o[1] + (g - o[1]) * mix;
        b = o[2] + (b - o[2]) * mix;
        a = o[3] + (a - o[3]) * mix;
    }
    dst[0] = r; dst[1] = g; dst[2] = b; dst[3] = Clamp01(a);
}

// Row helpers (used by tools/examples.cpp). `src` == nullptr = transparent row.
inline void AccumulateRow(BlendMode mode, TintMode tint, const Sample& k,
                          const float* src, float* acc, int n)
{
    if (!src) return;  // transparent pixels add nothing in every mode
    for (int x = 0; x < n; ++x, src += 4, acc += 4) {
        AccumulatePixel(mode, tint, k, src[0], src[1], src[2], src[3], acc);
    }
}

inline void FinalizeRow(BlendMode mode, float mix, const float* acc, const float* orig,
                        float* dst, int n, float gain = 1.f)
{
    for (int x = 0; x < n; ++x) {
        FinalizePixel(mode, mix, acc + 4 * x, orig ? orig + 4 * x : nullptr, dst + 4 * x, gain);
    }
}

}  // namespace bt
