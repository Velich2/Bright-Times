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
#include "PrSDKAESupport.h"
#include "AE_Macros.h"
#include "Param_Utils.h"

#include "BrightTimesParams.h"

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

extern "C" {
DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data,
                            PF_ParamDef* params[], PF_LayerDef* output, void* extra);
}
