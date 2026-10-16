# Locates the Premiere Pro SDK headers needed for the Premiere GPU filter under
# PR_SDK_ROOT (zips may be nested; up to two extra levels are searched).
# Sets PR_SDK_HEADERS or stops with the list of missing headers.

set(_bt_pr_required
  PrSDKGPUFilter.h
  PrSDKGPUDeviceSuite.h
  PrSDKPPixSuite.h
  PrSDKVideoSegmentSuite.h
  PrSDKVideoSegmentProperties.h
  PrSDKMemoryManagerSuite.h)

set(_bt_pr_candidates "${PR_SDK_ROOT}/Examples/Headers" "${PR_SDK_ROOT}/Headers")
file(GLOB _bt_pr_l1 LIST_DIRECTORIES true "${PR_SDK_ROOT}/*/Examples/Headers")
file(GLOB _bt_pr_l2 LIST_DIRECTORIES true "${PR_SDK_ROOT}/*/*/Examples/Headers")
list(APPEND _bt_pr_candidates ${_bt_pr_l1} ${_bt_pr_l2})

set(PR_SDK_HEADERS "")
foreach(cand IN LISTS _bt_pr_candidates)
  if(EXISTS "${cand}/PrSDKGPUFilter.h")
    set(PR_SDK_HEADERS "${cand}")
    break()
  endif()
endforeach()
if(NOT PR_SDK_HEADERS)
  message(FATAL_ERROR "Premiere Pro SDK not found under PR_SDK_ROOT=${PR_SDK_ROOT}\n"
                      "Expected <root>/Examples/Headers/PrSDKGPUFilter.h (also searched 2 levels deeper).")
endif()

set(_bt_pr_missing "")
foreach(h IN LISTS _bt_pr_required)
  if(NOT EXISTS "${PR_SDK_HEADERS}/${h}")
    string(APPEND _bt_pr_missing "  missing: ${h}\n")
  endif()
endforeach()
if(_bt_pr_missing)
  message(FATAL_ERROR "Premiere Pro SDK at ${PR_SDK_HEADERS} is incomplete:\n${_bt_pr_missing}")
endif()
message(STATUS "Premiere Pro SDK headers: ${PR_SDK_HEADERS}")
