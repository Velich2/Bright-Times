// Example images for the README / release page, rendered through
// BrightTimesCore.h (the same math as the plug-in) from a synthetic scene: a
// static dusk scene, a bouncing ball and a spinning baton.
//
//   c++ -std=c++17 -O2 -Isrc tools/examples.cpp -o bt_examples
//   ./bt_examples out_dir          # PPM stills + animation frames
//   python3 tools/make_examples.py out_dir docs/examples
//
// The scene loops every kLoop frames, so the animation repeats seamlessly.

#include "BrightTimesCore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr float kPi   = 3.14159265f;
constexpr int   kLoop = 60;

struct Image {
    int                w = 0, h = 0;
    std::vector<float> px;  // premultiplied RGBA (alpha is always 1 here)
};

float Smooth(float edge0, float edge1, float x)
{
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

void Over(float* p, float r, float g, float b, float a)
{
    p[0] = p[0] * (1.f - a) + r * a;
    p[1] = p[1] * (1.f - a) + g * a;
    p[2] = p[2] * (1.f - a) + b * a;
}

// Distance from (px, py) to the segment a-b.
float SegDist(float px, float py, float ax, float ay, float bx, float by)
{
    const float vx = bx - ax, vy = by - ay;
    const float t  = std::clamp(((px - ax) * vx + (py - ay) * vy) / (vx * vx + vy * vy), 0.f, 1.f);
    return std::hypot(px - (ax + t * vx), py - (ay + t * vy));
}

Image Scene(float t, int w, int h)
{
    Image im{w, h, std::vector<float>(4 * w * h)};
    const float s       = w / 640.f;  // all sizes are given for 640 px
    const float horizon = 0.70f * h;

    // Moving things (loop over kLoop frames).
    const float phase   = std::fmod(t / kLoop + 10.f, 1.f);
    const float ballX   = -60.f * s + phase * (w + 120.f * s);
    const float bounce  = std::fabs(std::sin(kPi * t / 15.f));
    const float ballR   = 24.f * s;
    const float ballY   = horizon + 10.f * s - ballR - bounce * 150.f * s;
    const float batonCx = 0.76f * w, batonCy = 0.36f * h;
    const float angle   = kPi * t / kLoop;
    const float batonL  = 70.f * s;
    const float bx0 = batonCx - std::cos(angle) * batonL, by0 = batonCy - std::sin(angle) * batonL;
    const float bx1 = batonCx + std::cos(angle) * batonL, by1 = batonCy + std::sin(angle) * batonL;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float*      p  = &im.px[4 * (y * w + x)];
            const float fx = x + 0.5f, fy = y + 0.5f;

            // Static: dusk sky, ground, moon, posts, wires.
            if (fy < horizon) {
                const float k = fy / horizon;
                p[0] = 0.10f + 0.50f * k;
                p[1] = 0.12f + 0.34f * k;
                p[2] = 0.26f + 0.16f * k;
            } else {
                const float k = (fy - horizon) / (h - horizon);
                const bool  stripe = (static_cast<int>((fx + 2.f * (fy - horizon)) / (18.f * s)) % 2) == 0;
                const float v = (stripe ? 0.30f : 0.26f) - 0.10f * k;
                p[0] = v * 0.95f;
                p[1] = v * 0.85f;
                p[2] = v * 0.70f;
            }
            p[3] = 1.f;

            const float moon = std::hypot(fx - 0.16f * w, fy - 0.20f * h);
            Over(p, 0.98f, 0.93f, 0.78f, 1.f - Smooth(28.f * s, 30.f * s, moon));
            Over(p, 0.98f, 0.90f, 0.70f, 0.25f * (1.f - Smooth(30.f * s, 60.f * s, moon)));

            for (float postX : {0.40f * w, 0.92f * w}) {
                if (std::fabs(fx - postX) < 4.f * s && fy > 0.30f * h && fy < horizon + 6.f * s)
                    Over(p, 0.08f, 0.07f, 0.09f, 1.f);
                const float lamp = std::hypot(fx - postX, fy - 0.30f * h);
                Over(p, 1.f, 0.82f, 0.45f, 1.f - Smooth(7.f * s, 9.f * s, lamp));
            }
            const float sag  = 0.30f * h + 22.f * s * std::sin(kPi * (fx - 0.40f * w) / (0.52f * w));
            const bool  span = fx > 0.40f * w && fx < 0.92f * w;
            if (span) Over(p, 0.06f, 0.06f, 0.08f, 1.f - Smooth(0.8f * s, 1.8f * s, std::fabs(fy - sag)));

            // Moving: bouncing ball with a soft shadow, spinning baton.
            const float shadow = std::hypot((fx - ballX) / 1.8f, (fy - horizon - 12.f * s) / 0.35f);
            Over(p, 0.f, 0.f, 0.f, 0.35f * (1.f - bounce * 0.6f) * (1.f - Smooth(14.f * s, 24.f * s, shadow)));
            const float ball = std::hypot(fx - ballX, fy - ballY);
            const float shade = 0.80f + 0.20f * std::clamp((ballY - fy) / ballR, -1.f, 1.f);
            Over(p, 0.98f * shade, 0.96f * shade, 0.92f * shade, 1.f - Smooth(ballR - 1.f, ballR + 1.f, ball));

            const float baton = SegDist(fx, fy, bx0, by0, bx1, by1);
            Over(p, 0.95f, 0.95f, 0.98f, 1.f - Smooth(6.f * s, 8.f * s, baton));
            const float hub = std::hypot(fx - batonCx, fy - batonCy);
            Over(p, 0.20f, 0.20f, 0.24f, 1.f - Smooth(5.f * s, 6.5f * s, hub));
        }
    }
    return im;
}

Image Effect(const bt::Settings& s, float t, int w, int h)
{
    const auto samples = bt::BuildSamples(s);
    const Image orig = Scene(t, w, h);
    std::vector<float> acc(4 * w * h);
    bt::InitAccumRow(s.blendMode, acc.data(), w * h);
    for (const auto& smp : samples) {
        const Image frame = Scene(t + static_cast<float>(smp.frameOffset), w, h);
        bt::AccumulateRow(s.blendMode, s.tintMode, smp, frame.px.data(), acc.data(), w * h);
    }
    Image out{w, h, std::vector<float>(4 * w * h)};
    bt::FinalizeRow(s.blendMode, s.mix, acc.data(), orig.px.data(), out.px.data(), w * h, s.intensity);
    return out;
}

void WritePPM(const std::string& path, const Image& im)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", im.w, im.h);
    for (int i = 0; i < im.w * im.h; ++i) {
        const float a = im.px[4 * i + 3];
        for (int c = 0; c < 3; ++c) {
            const float v = bt::Clamp01(a > 0.f ? im.px[4 * i + c] / a : 0.f);
            std::fputc(static_cast<int>(v * 255.f + 0.5f), f);
        }
    }
    std::fclose(f);
}

}  // namespace

int main(int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    constexpr int W = 640, H = 360;
    constexpr float T = 20.f;  // still frame time

    bt::Settings optical;
    optical.framesBackward = 6;
    optical.framesForward  = 6;
    optical.groups         = 7;

    bt::Settings harris = optical;  // classic RGB "Harris shutter"
    harris.colorMode      = bt::ColorMode_RGB;
    harris.groups         = 3;
    harris.groupMode      = bt::GroupMode_CYCLIC;
    harris.frameStep      = 3;
    harris.framesBackward = 1;
    harris.framesForward  = 1;
    harris.falloffBackward = harris.falloffForward = 0.f;

    bt::Settings trail = optical;  // rainbow through time, past only
    trail.framesBackward  = 12;
    trail.framesForward   = 0;
    trail.colorOrder      = bt::ColorOrder_TIME;
    trail.groupSoftness   = 1.f;
    trail.falloffBackward = 0.6f;

    bt::Settings palette = optical;  // custom palette, exact colours
    palette.colorMode   = bt::ColorMode_CUSTOM;
    palette.customCount = 3;
    palette.custom[0]   = {0.10f, 0.45f, 1.00f};
    palette.custom[1]   = {0.00f, 0.90f, 0.95f};
    palette.custom[2]   = {0.85f, 0.20f, 1.00f};
    palette.groups      = 3;
    palette.blendMode   = bt::BlendMode_TRAILS;

    bt::Settings glow = optical;  // lighten: brightest of the tinted copies
    glow.blendMode = bt::BlendMode_LIGHTEN;

    WritePPM(dir + "/original.ppm", Scene(T, W, H));
    WritePPM(dir + "/optical.ppm", Effect(optical, T, W, H));
    WritePPM(dir + "/harris.ppm", Effect(harris, T, W, H));
    WritePPM(dir + "/trail.ppm", Effect(trail, T, W, H));
    WritePPM(dir + "/palette.ppm", Effect(palette, T, W, H));
    WritePPM(dir + "/glow.ppm", Effect(glow, T, W, H));

    // Animation: original and Optical side by side, one loop.
    constexpr int AW = 400, AH = 225;
    for (int f = 0; f < kLoop; ++f) {
        char name[64];
        std::snprintf(name, sizeof(name), "/anim_orig_%02d.ppm", f);
        WritePPM(dir + name, Scene(static_cast<float>(f), AW, AH));
        std::snprintf(name, sizeof(name), "/anim_fx_%02d.ppm", f);
        WritePPM(dir + name, Effect(optical, static_cast<float>(f), AW, AH));
    }
    std::printf("written to %s\n", dir.c_str());
    return 0;
}
