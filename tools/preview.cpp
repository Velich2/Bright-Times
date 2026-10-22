// Offline preview of the Bright Times algorithm (no After Effects needed).
//
// Renders a synthetic scene — a static textured background with a moving
// object — through BrightTimesCore.h and writes binary PPM images.
//
//   c++ -std=c++17 -O2 -I../src preview.cpp -o preview
//   ./preview out_dir
//
// Produces: original.ppm, optical.ppm, rgb.ppm, additive.ppm, screen.ppm

#include "BrightTimesCore.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

constexpr int W = 480, H = 270;

// Straight RGBA frame at time t (frames). Converted to premultiplied below.
void RenderFrame(float t, std::vector<float>& px)
{
    px.assign(4 * W * H, 0.f);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            float* p = &px[4 * (y * W + x)];
            // Static sepia background with "rails" and a soft gradient.
            float v = 0.55f + 0.25f * (float)y / H;
            if ((x / 24 + y / 24) % 2 == 0) v -= 0.06f;
            if (std::abs((x - W / 2) - (y - H) * 0.35f) < 3 ||
                std::abs((x - W / 2) + (y - H) * 0.35f) < 3) v = 0.9f;
            p[0] = v * 1.00f;
            p[1] = v * 0.90f;
            p[2] = v * 0.75f;
            p[3] = 1.f;
            // Moving dark "train" block.
            const float cx = 120.f + t * 14.f, cy = 120.f;
            if (std::abs(x - cx) < 45 && std::abs(y - cy) < 55) {
                const bool window = std::abs(y - (cy - 25)) < 10 && std::abs(x - cx) < 35;
                p[0] = window ? 0.85f : 0.15f;
                p[1] = window ? 0.95f : 0.30f;
                p[2] = window ? 0.95f : 0.25f;
            }
        }
    }
}

void WritePPM(const std::string& path, const std::vector<float>& premul)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; ++i) {
        const float a = premul[4 * i + 3];
        for (int c = 0; c < 3; ++c) {
            float v = a > 0.f ? premul[4 * i + c] / a : 0.f;  // unpremultiply
            v = bt::Clamp01(v);
            std::fputc(static_cast<int>(v * 255.f + 0.5f), f);
        }
    }
    std::fclose(f);
}

std::vector<float> Run(const bt::Settings& s)
{
    const auto samples = bt::BuildSamples(s);
    std::vector<float> acc(4 * W * H), frame, orig, out(4 * W * H);
    RenderFrame(0.f, orig);
    bt::InitAccumRow(s.blendMode, acc.data(), W * H);
    for (const auto& smp : samples) {
        RenderFrame(static_cast<float>(smp.frameOffset), frame);
        bt::AccumulateRow(s.blendMode, s.tintMode, smp, frame.data(), acc.data(), W * H);
    }
    bt::FinalizeRow(s.blendMode, s.mix, acc.data(), orig.data(), out.data(), W * H);
    return out;
}

}  // namespace

int main(int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";

    std::vector<float> orig;
    RenderFrame(0.f, orig);
    WritePPM(dir + "/original.ppm", orig);

    bt::Settings s;
    s.framesBackward = 6;
    s.framesForward  = 6;
    s.groups         = 7;
    WritePPM(dir + "/optical.ppm", Run(s));

    bt::Settings rgb = s;
    rgb.colorMode = bt::ColorMode_RGB;
    rgb.groups    = 3;
    rgb.frameStep = 3;
    rgb.framesBackward = rgb.framesForward = 1;
    rgb.falloffBackward = rgb.falloffForward = 0.f;
    WritePPM(dir + "/rgb.ppm", Run(rgb));

    bt::Settings add = s;
    add.blendMode = bt::BlendMode_ADDITIVE;
    WritePPM(dir + "/additive.ppm", Run(add));

    bt::Settings scr = s;
    scr.blendMode = bt::BlendMode_SCREEN;
    scr.intensity = 0.35f;
    WritePPM(dir + "/screen.ppm", Run(scr));

    std::printf("written to %s\n", dir.c_str());
    return 0;
}
