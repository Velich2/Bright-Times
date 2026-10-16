# Locates the After Effects SDK "Examples" folder under AE_SDK_ROOT and verifies
# that everything Bright Times needs is there. Adobe's zips are sometimes nested
# one or two folders deep, so those levels are searched too.
#
# Sets AE_EX (.../Examples) in the parent scope or stops with a list of problems.

set(_bt_sdk_candidates "${AE_SDK_ROOT}/Examples")
file(GLOB _bt_l1 LIST_DIRECTORIES true "${AE_SDK_ROOT}/*/Examples")
file(GLOB _bt_l2 LIST_DIRECTORIES true "${AE_SDK_ROOT}/*/*/Examples")
list(APPEND _bt_sdk_candidates ${_bt_l1} ${_bt_l2})

set(AE_EX "")
foreach(cand IN LISTS _bt_sdk_candidates)
  if(EXISTS "${cand}/Headers/AE_Effect.h")
    set(AE_EX "${cand}")
    break()
  endif()
endforeach()
if(NOT AE_EX)
  message(FATAL_ERROR "After Effects SDK not found under AE_SDK_ROOT=${AE_SDK_ROOT}\n"
                      "Expected <root>/Examples/Headers/AE_Effect.h (also searched 2 levels deeper).")
endif()
message(STATUS "After Effects SDK: ${AE_EX}")

set(_bt_required_files
  Headers/A.h
  Headers/AEConfig.h
  Headers/AE_Effect.h
  Headers/AE_EffectCB.h
  Headers/AE_EffectCBSuites.h
  Headers/AE_EffectSuites.h
  Headers/AE_EffectPixelFormat.h
  Headers/AE_EffectGPUSuites.h
  Headers/AE_EffectVers.h
  Headers/AE_Macros.h
  Headers/AE_PluginData.h
  Headers/SP/SPBasic.h
  Util/entry.h
  Util/Param_Utils.h)
if(WIN32)
  list(APPEND _bt_required_files Resources/PiPLtool.exe)
elseif(APPLE)
  list(APPEND _bt_required_files Resources/AE_General.r)
endif()

set(_bt_problems "")
foreach(f IN LISTS _bt_required_files)
  if(NOT EXISTS "${AE_EX}/${f}")
    string(APPEND _bt_problems "  missing file: Examples/${f}\n")
  endif()
endforeach()

# API used by the plug-in: (file, symbol, minimum SDK that has it)
set(_bt_required_symbols
  "Headers/AE_PluginData.h|PF_REGISTER_EFFECT_EXT2|AE 2022"
  "Headers/AE_Effect.h|PF_OutFlag2_AUTOMATIC_WIDE_TIME_INPUT|CS5.5"
  "Headers/AE_Effect.h|PF_OutFlag2_SUPPORTS_GPU_RENDER_F32|CC 2015"
  "Headers/AE_Effect.h|PF_OutFlag2_SUPPORTS_THREADED_RENDERING|AE 2022"
  "Headers/AE_EffectGPUSuites.h|PF_GPUDeviceSuite1|CC 2015"
  "Headers/AE_EffectPixelFormat.h|PF_PixelFormat_GPU_BGRA128|CC 2015"
  "Util/Param_Utils.h|PF_ADD_POPUPX|-"
  "Util/Param_Utils.h|PF_ADD_FLOAT_SLIDERX|-"
  "Util/Param_Utils.h|PF_ADD_CHECKBOXX|-")
foreach(entry IN LISTS _bt_required_symbols)
  string(REPLACE "|" ";" parts "${entry}")
  list(GET parts 0 f)
  list(GET parts 1 sym)
  list(GET parts 2 since)
  if(EXISTS "${AE_EX}/${f}")
    file(STRINGS "${AE_EX}/${f}" hits REGEX "${sym}")
    if(NOT hits)
      if(since STREQUAL "-")
        string(APPEND _bt_problems "  ${sym} not found in Examples/${f}\n")
      else()
        string(APPEND _bt_problems "  ${sym} not found in Examples/${f} (needs SDK ${since} or newer)\n")
      endif()
    endif()
  endif()
endforeach()

if(_bt_problems)
  message(FATAL_ERROR "After Effects SDK at ${AE_EX} is incomplete:\n${_bt_problems}")
endif()

# Report the plug-in API version of this SDK.
file(STRINGS "${AE_EX}/Headers/AE_Effect.h" _bt_ver REGEX "#define[ \t]+PF_PLUG_IN_(VERSION|SUBVERS)[ \t]")
message(STATUS "After Effects SDK API: ${_bt_ver}")
