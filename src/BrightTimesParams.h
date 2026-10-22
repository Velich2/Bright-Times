// Bright Times - parameter indices, disk IDs and popup values.
// Free of SDK headers: the Premiere Pro GPU filter includes it together with
// the Premiere SDK, whose PrSDKTypes.h clashes with the After Effects SDK's
// PrSDKAESupport.h (both define PrSDKString).

#pragma once

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
    BT_BLEND_ORIGINAL,
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
    ID_MIX_UNUSED,       // old "Mix with Original" (inverted meaning) - keep for ID stability
    ID_USE_GPU,
    ID_EDGE_MODE,
    ID_COLOR_BALANCE,
    ID_COLOR_ORDER,
    ID_BLEND_ORIGINAL
};

// "Clip Edges" popup values.
enum { BT_EDGE_HOLD = 1, BT_EDGE_TRANSPARENT };
