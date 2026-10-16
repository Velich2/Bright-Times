// Bright Times - parameters -> bt::Settings, shared by the After Effects API
// (ParamReader) and the Premiere Pro GPU filter (PrParamReader).
//
// Reader requirements: Int, Float, Bool, Popup, AngleDeg, Color taking an
// After Effects parameter index (BT_*).

#pragma once

#include "BrightTimes.h"
#include "BrightTimesCore.h"

#include <cmath>

template <class Reader>
void ReadSettingsFrom(Reader& r, bt::Settings& s)
{
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
    if (s.colorMode == bt::ColorMode_CUSTOM) {
        for (int i = 0; i < bt::kMaxCustomColor; ++i) {
            s.custom[i] = r.Color(static_cast<int>(BT_COLOR_1 + i));
        }
    }

    s.falloffBackward = r.Float(BT_FALLOFF_BACKWARD) / 100.f;
    s.falloffForward  = r.Float(BT_FALLOFF_FORWARD) / 100.f;
    s.falloffCurve    = r.Float(BT_FALLOFF_CURVE);

    s.tintMode        = static_cast<bt::TintMode>(r.Popup(BT_TINT_MODE));
    s.blendMode       = static_cast<bt::BlendMode>(r.Popup(BT_BLEND_MODE));
    s.colorBalance    = r.Float(BT_COLOR_BALANCE) / 100.f;
    s.intensity       = r.Float(BT_INTENSITY) / 100.f;
    s.mix             = 1.f - r.Float(BT_BLEND_ORIGINAL) / 100.f;  // mix = share of the effect
}
