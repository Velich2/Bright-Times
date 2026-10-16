# Windows PiPL build (same three steps as the AE SDK sample projects):
#   1. cl /EP      BrightTimesPiPL.r   -> .rr   (C preprocessor)
#   2. PiPLtool    .rr                 -> .rrc
#   3. cl /EP      .rrc                -> .rc   (compiled by rc.exe with the plug-in)
# Args: CL, PIPLTOOL, IN, OUT_DIR, INCLUDES (;-separated)
set(rr  "${OUT_DIR}/BrightTimesPiPL.rr")
set(rrc "${OUT_DIR}/BrightTimesPiPL.rrc")
set(rc  "${OUT_DIR}/BrightTimesPiPL.rc")

set(inc_args "")
foreach(dir IN LISTS INCLUDES)
  list(APPEND inc_args "/I${dir}")
endforeach()

execute_process(COMMAND "${CL}" /nologo ${inc_args} /EP "${IN}"
                OUTPUT_FILE "${rr}" RESULT_VARIABLE res)
if(res)
  message(FATAL_ERROR "PiPL preprocess failed (${res})")
endif()

execute_process(COMMAND "${PIPLTOOL}" "${rr}" "${rrc}" RESULT_VARIABLE res)
if(res)
  message(FATAL_ERROR "PiPLtool failed (${res})")
endif()

execute_process(COMMAND "${CL}" /nologo /D "MSWindows" /EP "${rrc}"
                OUTPUT_FILE "${rc}" RESULT_VARIABLE res)
if(res)
  message(FATAL_ERROR "PiPL .rc generation failed (${res})")
endif()
