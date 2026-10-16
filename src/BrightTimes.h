// Bright Times — After Effects effect plug-in.
// Temporal colour separation: neighbouring frames are tinted by group colours
// and optically superimposed on the current frame.

#pragma once

#include "AEConfig.h"
#include "entry.h"
#include "AE_Effect.h"
#include "AE_PluginData.h"
#include "AE_EffectVers.h"
#include "AE_EffectCB.h"
#include "AE_EffectCBSuites.h"
#include "AE_EffectSuites.h"
#include "AE_EffectPixelFormat.h"
#include "AE_EffectGPUSuites.h"
#include "AE_Macros.h"
#include "Param_Utils.h"

#define BT_NAME          "Bright Times"
#define BT_MATCH_NAME    "DVLN BrightTimes"
#define BT_CATEGORY      "Bright Times"
#define BT_DESCRIPTION   "Temporal colour separation with optical superimposition."

#define BT_MAJOR_VERSION 1
#define BT_MINOR_VERSION 0
#define BT_BUG_VERSION   0
#define BT_STAGE_VERSION PF_Stage_DEVELOP
#define BT_BUILD_VERSION 1

// Must match AE_Reserved_Info in BrightTimesPiPL.r.
#define BT_RESERVED_INFO 8
// PiPL kind 'eFKT' (AEEffect) as passed to the plug-in data callback.
#define BT_PLUGIN_KIND_EFFECT 0x65464B54
#define BT_SUPPORT_URL "https://github.com/Velich2/Bright-Times"

// Parameter indices. Topic start/end entries are parameters too.
enum {
    BT_INPUT = 0,

    BT_FRAMES_BACKWARD,
    BT_FRAMES_FORWARD,
    BT_FRAME_STEP,
    BT_EDGE_MODE,
    BT_INCLUDE_CURRENT,
    BT_CURRENT_UNTINTED,

    BT_COLOR_TOPIC_START,
    BT_GROUPS,
    BT_GROUP_MODE,
    BT_COLOR_ORDER,
    BT_GROUP_SOFTNESS,
    BT_COLOR_MODE,
    BT_HUE_START,
    BT_HUE_RANGE,
    BT_SATURATION,
    BT_REVERSE_COLORS,
    BT_PALETTE_SIZE,
    BT_COLOR_1,
    BT_COLOR_2,
    BT_COLOR_3,
    BT_COLOR_4,
    BT_COLOR_5,
    BT_COLOR_6,
    BT_COLOR_TOPIC_END,

    BT_FALLOFF_TOPIC_START,
    BT_FALLOFF_BACKWARD,
    BT_FALLOFF_FORWARD,
    BT_FALLOFF_CURVE,
    BT_FALLOFF_TOPIC_END,

    BT_TINT_MODE,
    BT_BLEND_MODE,
    BT_COLOR_BALANCE,
    BT_INTENSITY,
    BT_MIX,
    BT_USE_GPU,

    BT_NUM_PARAMS
};

// Stable disk IDs — never renumber, only append (projects store these).
enum {
    ID_FRAMES_BACKWARD = 1,
    ID_FRAMES_FORWARD,
    ID_FRAME_STEP,
    ID_INCLUDE_CURRENT,
    ID_CURRENT_UNTINTED,
    ID_COLOR_TOPIC_START,
    ID_GROUPS,
    ID_GROUP_MODE,
    ID_GROUP_SOFTNESS,
    ID_COLOR_MODE,
    ID_HUE_START,
    ID_HUE_RANGE,
    ID_SATURATION,
    ID_REVERSE_COLORS,
    ID_PALETTE_SIZE,
    ID_COLOR_1,
    ID_COLOR_2,
    ID_COLOR_3,
    ID_COLOR_4,
    ID_COLOR_5,
    ID_COLOR_6,
    ID_COLOR_TOPIC_END,
    ID_FALLOFF_TOPIC_START,
    ID_FALLOFF_BACKWARD,
    ID_FALLOFF_FORWARD,
    ID_FALLOFF_CURVE,
    ID_FALLOFF_TOPIC_END,
    ID_TINT_MODE,
    ID_BLEND_MODE,
    ID_INTENSITY,
    ID_MIX,
    ID_USE_GPU,
    ID_EDGE_MODE,
    ID_COLOR_BALANCE,
    ID_COLOR_ORDER
};

// "Clip Edges" popup values.
enum { BT_EDGE_HOLD = 1, BT_EDGE_TRANSPARENT };

extern "C" {
DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data,
                            PF_ParamDef* params[], PF_LayerDef* output, void* extra);
}
