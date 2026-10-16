// Minimal stand-in for the Premiere Pro SDK GPU filter headers, used ONLY by the
// offline tests. Declarations mirror the Premiere SDK (checked against generated
// SDK bindings) just far enough to compile BrightTimesPremiereGPU.cpp and drive
// xGPUFilterEntry from a mock host. The real plug-in uses the official SDK.
#pragma once
#include "ae_stub.h"

typedef int32_t  csSDK_int32;
typedef uint32_t csSDK_uint32;
typedef int64_t  csSDK_int64;
typedef int8_t   csSDK_int8;
typedef int16_t  csSDK_int16;
typedef uint8_t  csSDK_uint8;
typedef size_t   csSDK_size_t;
typedef csSDK_int32 prBool;
typedef csSDK_int32 prSuiteError;
typedef int64_t  PrTime;
typedef csSDK_int32 prFieldType;
typedef int pmFieldDisplay;
typedef int PrRenderQuality;
typedef csSDK_int32 PrTimelineID;
typedef char* PrMemoryPtr;
struct PPix; typedef PPix** PPixHand;
enum { kPrFalse = 0, kPrTrue = 1 };
enum : prSuiteError { suiteError_NoError = 0, suiteError_Fail = (prSuiteError)0x80000000,
                      suiteError_InvalidParms = (prSuiteError)0x80000001, suiteError_NotImplemented = (prSuiteError)0x80000004 };

struct prRect { csSDK_int32 left, top, right, bottom; };
struct PrSDKString { csSDK_int64 opaque[2]; };

typedef int PrParamType;
enum { kPrParamType_Int8 = 1, kPrParamType_Int16, kPrParamType_Int32, kPrParamType_Int64, kPrParamType_Float32,
       kPrParamType_Float64, kPrParamType_Bool, kPrParamType_Point, kPrParamType_Guid, kPrParamType_PrMemoryPtr };
struct PrParam {
  PrParamType mType;
  union { csSDK_int8 mInt8; csSDK_int16 mInt16; csSDK_int32 mInt32; csSDK_int64 mInt64; float mFloat32;
          double mFloat64; csSDK_uint8 mBool; PrMemoryPtr mMemoryPtr; };
};

typedef int PrGPUDeviceFramework;
enum { PrGPUDeviceFramework_CUDA = 0, PrGPUDeviceFramework_OpenCL = 1, PrGPUDeviceFramework_Metal = 2, PrGPUDeviceFramework_DirectX = 3 };
struct PrGPUDeviceInfo { PrGPUDeviceFramework outDeviceFramework; prBool outMeetsMinimumRequirementsForAcceleration;
  void* outPlatformHandle; void* outDeviceHandle; void* outContextHandle; void* outCommandQueueHandle;
  void* outOffscreenOpenGLContextHandle; void* outOffscreenOpenGLDeviceHandle; };

typedef SPBasicSuite* (*plugGetSPBasicSuiteFunc)();
struct PlugUtilFuncs { void* a; void* b; void* c; plugGetSPBasicSuiteFunc getSPBasicSuite; };
struct piSuites { int piInterfaceVer; void* memFuncs; void* windFuncs; void* ppixFuncs; PlugUtilFuncs* utilFuncs; void* timelineFuncs; };
typedef piSuites* piSuitesPtr;

struct PrGPUFilterInstance { piSuitesPtr piSuites; csSDK_uint32 inDeviceIndex; PrTimelineID inTimelineID;
  csSDK_int32 inNodeID; void* ioPrivatePluginData; prBool outIsRealtime; };
struct PrGPUFilterRenderParams { PrTime inClipTime; PrTime inSequenceTime; PrRenderQuality inQuality;
  float inDownsampleFactorX, inDownsampleFactorY; csSDK_uint32 inRenderWidth, inRenderHeight, inRenderPARNum, inRenderPARDen;
  prFieldType inRenderFieldType; PrTime inRenderTicksPerFrame; pmFieldDisplay inRenderField; };
typedef int PrGPUFilterFrameDependencyType;
enum { PrGPUDependency_InputFrame = 0, PrGPUDependency_Precompute, PrGPUDependency_FieldSeparation, PrGPUDependency_TransitionInputFrame };
struct PrGPUFilterFrameDependency { PrGPUFilterFrameDependencyType outDependencyType; csSDK_int32 outTrackID; PrTime outSequenceTime;
  PrPixelFormat outPrecomputePixelFormat; csSDK_uint32 outPrecomputeFrameWidth, outPrecomputeFrameHeight,
  outPrecomputeFramePARNumerator, outPrecomputeFramePARDenominator; prFieldType outPrecomputeFrameFieldType;
  csSDK_size_t outPrecomputeCustomDataSize; prBool outNeedsFieldSeparation; prBool outReadIncomingTransition; };
struct PrGPUFilterInfo { csSDK_uint32 outInterfaceVersion; PrSDKString outMatchName; };
struct PrGPUFilter {
  prSuiteError (*CreateInstance)(PrGPUFilterInstance*);
  prSuiteError (*DisposeInstance)(PrGPUFilterInstance*);
  prSuiteError (*GetFrameDependencies)(PrGPUFilterInstance*, const PrGPUFilterRenderParams*, csSDK_int32*, PrGPUFilterFrameDependency*);
  prSuiteError (*Precompute)(PrGPUFilterInstance*, const PrGPUFilterRenderParams*, csSDK_int32, PPixHand);
  prSuiteError (*Render)(PrGPUFilterInstance*, const PrGPUFilterRenderParams*, const PPixHand*, csSDK_size_t, PPixHand*);
};
#define PrSDKGPUFilterInterfaceVersion 2

#define kPrSDKGPUDeviceSuite "MediaCore GPU Device Suite"
#define kPrSDKGPUDeviceSuiteVersion 2
struct PrSDKGPUDeviceSuite {
  prSuiteError (*GetDeviceInfo)(csSDK_uint32, csSDK_uint32, PrGPUDeviceInfo*);
  prSuiteError (*CreateGPUPPix)(csSDK_uint32, PrPixelFormat, int, int, int, int, prFieldType, PPixHand*);
  prSuiteError (*GetGPUPPixData)(PPixHand, void**);
};
#define kPrSDKPPixSuite "Premiere PPix Suite"
#define kPrSDKPPixSuiteVersion 1
struct PrSDKPPixSuite {
  prSuiteError (*Dispose)(PPixHand);
  prSuiteError (*GetBounds)(PPixHand, prRect*);
  prSuiteError (*GetRowBytes)(PPixHand, csSDK_int32*);
  prSuiteError (*GetPixelAspectRatio)(PPixHand, csSDK_uint32*, csSDK_uint32*);
  prSuiteError (*GetPixelFormat)(PPixHand, PrPixelFormat*);
};
#define kPrSDKVideoSegmentSuite "MediaCore Video Segment Suite"
#define kPrSDKVideoSegmentSuiteVersion 9
struct PrSDKVideoSegmentSuite {
  prSuiteError (*GetNodeProperty)(csSDK_int32, const char*, PrMemoryPtr*);
  prSuiteError (*GetParam)(csSDK_int32, csSDK_int32, PrTime, PrParam*);
};
#define kVideoSegmentProperty_Effect_EffectDuration "EffectNode::EffectDuration"
#define kPrSDKMemoryManagerSuite "Premiere Memory Manager Suite"
#define kPrSDKMemoryManagerSuiteVersion 4
struct PrSDKMemoryManagerSuite { void (*PrDisposePtr)(PrMemoryPtr); };
