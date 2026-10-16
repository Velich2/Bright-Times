// Bright Times — PiPL resource.
// Compiled with PiPLtool (Windows) or Rez (macOS); see CMakeLists.txt / README.md.
// The flag values must stay in sync with GlobalSetup() in BrightTimes.cpp.

#include "AEConfig.h"
#include "AE_EffectVers.h"

#ifndef AE_OS_WIN
	#include <AE_General.r>
#endif

resource 'PiPL' (16000) {
	{
		Kind {
			AEEffect
		},
		Name {
			"Bright Times"
		},
		Category {
			"Time"
		},
#ifdef AE_OS_WIN
	#if defined(AE_PROC_INTELx64)
		CodeWin64X86 {"EffectMain"},
	#elif defined(AE_PROC_ARM64)
		CodeWinARM64 {"EffectMain"},
	#endif
#elif defined(AE_OS_MAC)
		CodeMacIntel64 {"EffectMain"},
		CodeMacARM64 {"EffectMain"},
#endif
		AE_PiPL_Version {
			2,
			0
		},
		AE_Effect_Spec_Version {
			PF_PLUG_IN_VERSION,
			PF_PLUG_IN_SUBVERS
		},
		/* PF_VERSION(1, 0, 0, PF_Stage_DEVELOP, 1) */
		AE_Effect_Version {
			524289
		},
		AE_Effect_Info_Flags {
			0
		},
		/* PF_OutFlag_WIDE_TIME_INPUT        0x00000002
		   PF_OutFlag_DEEP_COLOR_AWARE       0x02000000
		   PF_OutFlag_SEND_UPDATE_PARAMS_UI  0x04000000 */
		AE_Effect_Global_OutFlags {
			0x06000002
		},
		/* PF_OutFlag2_SUPPORTS_SMART_RENDER       0x00000400
		   PF_OutFlag2_FLOAT_COLOR_AWARE           0x00001000
		   PF_OutFlag2_AUTOMATIC_WIDE_TIME_INPUT   0x00020000
		   PF_OutFlag2_SUPPORTS_GPU_RENDER_F32     0x02000000
		   PF_OutFlag2_SUPPORTS_THREADED_RENDERING 0x08000000 */
		AE_Effect_Global_OutFlags_2 {
			0x0A021400
		},
		AE_Effect_Match_Name {
			"DVLN BrightTimes"
		},
		AE_Reserved_Info {
			8
		},
		AE_Effect_Support_URL {
			"https://github.com/Velich2/Bright-Times"
		}
	}
};
