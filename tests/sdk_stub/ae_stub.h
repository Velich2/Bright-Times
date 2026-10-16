// Minimal stand-in for the After Effects SDK, used ONLY by the offline tests.
// Declarations mirror the SDK (layouts/values checked against the AE SDK bindings)
// just far enough to compile BrightTimes.cpp and drive EffectMain from a mock host.
// The real plug-in is always built against the official SDK.
#pragma once
#include <cstdint>
#include <cstring>
typedef int32_t A_long; typedef uint32_t A_u_long; typedef int16_t A_short; typedef uint8_t A_u_char; typedef uint16_t A_u_short; typedef char A_char; typedef float PF_FpShort; typedef double PF_FpLong;
typedef int32_t PF_Err; typedef int32_t PF_Cmd; typedef int32_t PF_ParamIndex; typedef int32_t PF_Fixed; typedef unsigned char PF_Boolean; typedef int32_t SPErr;
typedef struct _PF_ProgPtr* PF_ProgPtr; typedef void* PF_PluginDataPtr; typedef A_long PF_PixelFormat;
#define DllExport
#define TRUE 1
#define FALSE 0
enum { PF_Err_NONE=0, PF_Err_OUT_OF_MEMORY=4, PF_Err_INTERNAL_STRUCT_DAMAGED=512, PF_Err_INVALID_CALLBACK=515, PF_Err_BAD_CALLBACK_PARAM=516 };
enum { kSPNoError = 0 };
struct SPBasicSuite { SPErr (*AcquireSuite)(const char*, int32_t, const void**); SPErr (*ReleaseSuite)(const char*, int32_t); };
struct PF_Pixel { A_u_char alpha, red, green, blue; }; typedef PF_Pixel PF_Pixel8;
struct PF_Pixel16 { A_u_short alpha, red, green, blue; };
struct PF_PixelFloat { PF_FpShort alpha, red, green, blue; };
#define PF_MAX_CHAN16 32768
struct PF_LRect { A_long left, top, right, bottom; };
typedef void* PF_PixelPtr;
struct PF_LayerDef { void* r0; void* r1; A_long world_flags; PF_PixelPtr data; A_long rowbytes, width, height; A_long pad[8]; A_long origin_x, origin_y; };
typedef PF_LayerDef PF_EffectWorld;
// ---- GPU
enum { PF_GPU_Framework_NONE=0, PF_GPU_Framework_OPENCL=1, PF_GPU_Framework_METAL=2, PF_GPU_Framework_CUDA=3 };
typedef A_long PF_GPU_Framework;
enum { PF_OutFlag2_SUPPORTS_GPU_RENDER_F32 = 33554432 };
enum { PF_RenderOutputFlag_GPU_RENDER_POSSIBLE = 2 };
enum { PF_Cmd_GPU_DEVICE_SETUP = 32, PF_Cmd_GPU_DEVICE_SETDOWN = 33 };
enum { PF_PixelFormat_GPU_BGRA128 = 1094992704 };
enum { PF_Err_UNRECOGNIZED_PARAM_TYPE = 514 };
struct PF_GPUDeviceInfo { PF_GPU_Framework device_framework; PF_Boolean compatibleB; void* platformPV; void* devicePV; void* contextPV; void* command_queuePV; void* a; void* b; };
struct PF_GPUDeviceSetupInput { PF_GPU_Framework what_gpu; A_u_long device_index; };
struct PF_GPUDeviceSetupOutput { void* gpu_data; };
struct PF_GPUDeviceSetupExtra { PF_GPUDeviceSetupInput* input; PF_GPUDeviceSetupOutput* output; };
struct PF_GPUDeviceSetdownInput { void* gpu_data; PF_GPU_Framework what_gpu; A_u_long device_index; };
struct PF_GPUDeviceSetdownExtra { PF_GPUDeviceSetdownInput* input; };
#define kPFGPUDeviceSuite "PF GPU Device Suite"
#define kPFGPUDeviceSuiteVersion1 1
struct PF_GPUDeviceSuite1 { PF_Err (*GetDeviceInfo)(PF_ProgPtr, A_u_long, PF_GPUDeviceInfo*); PF_Err (*GetGPUWorldData)(PF_ProgPtr, PF_EffectWorld*, void**); };
struct PF_SliderDef { A_long value; }; struct PF_FloatSliderDef { PF_FpLong value; }; struct PF_CheckBoxDef { A_long value; }; struct PF_PopupDef { A_long value; }; struct PF_AngleDef { PF_Fixed value; }; struct PF_ColorDef { PF_Pixel value; };
union PF_ParamDefUnion { PF_SliderDef sd; PF_FloatSliderDef fs_d; PF_CheckBoxDef bd; PF_PopupDef pd; PF_AngleDef ad; PF_ColorDef cd; };
struct PF_ParamDef { A_long id; A_long ui_flags; A_short ui_width, ui_height; A_long param_type; A_char name[32]; A_long flags; A_long unused; PF_ParamDefUnion u; };
struct PF_InData;
struct PF_InteractCallbacks { PF_Err (*checkout_param)(PF_ProgPtr, PF_ParamIndex, A_long, A_long, A_u_long, PF_ParamDef*); PF_Err (*checkin_param)(PF_ProgPtr, PF_ParamDef*); PF_Err (*add_param)(PF_ProgPtr, A_long, PF_ParamDef*); PF_Err (*abort)(PF_ProgPtr); };
struct PF_InData { PF_InteractCallbacks inter; PF_ProgPtr effect_ref; A_long current_time, time_step; A_u_long time_scale; SPBasicSuite* pica_basicP; };
struct PF_OutData { A_u_long my_version; A_char return_msg[256]; A_long out_flags, out_flags2, num_params; };
#define PF_CHECKOUT_PARAM(IN, I, T, S, SC, P) ((IN)->inter.checkout_param((IN)->effect_ref, (I), (T), (S), (SC), (P)))
#define PF_CHECKIN_PARAM(IN, P) ((IN)->inter.checkin_param((IN)->effect_ref, (P)))
#define PF_ABORT(IN) ((IN)->inter.abort((IN)->effect_ref))
#define AEFX_CLR_STRUCT(S) memset(&(S), 0, sizeof(S))
#define ERR(F) do { if (!err) { err = (F); } } while (0)
#define ERR2(F) do { if (((err2 = (F)) != PF_Err_NONE) && !err) err = err2; } while (0)
#define PF_VERSION(a,b,c,d,e) (((a)<<19)|((b)<<15)|((c)<<11)|((d)<<9)|(e))
enum { PF_Stage_DEVELOP = 0 };
enum { PF_Cmd_ABOUT=0, PF_Cmd_GLOBAL_SETUP, PF_Cmd_PARAMS_SETUP=4, PF_Cmd_USER_CHANGED_PARAM=13, PF_Cmd_UPDATE_PARAMS_UI=14, PF_Cmd_SMART_PRE_RENDER=23, PF_Cmd_SMART_RENDER };
enum { PF_OutFlag_WIDE_TIME_INPUT=2, PF_OutFlag_DEEP_COLOR_AWARE=33554432, PF_OutFlag_SEND_UPDATE_PARAMS_UI=67108864 };
enum { PF_OutFlag2_SUPPORTS_SMART_RENDER=1024, PF_OutFlag2_FLOAT_COLOR_AWARE=4096, PF_OutFlag2_AUTOMATIC_WIDE_TIME_INPUT=131072, PF_OutFlag2_SUPPORTS_THREADED_RENDERING=134217728 };
enum { PF_PUI_DISABLED = 32 }; enum { PF_ParamFlag_SUPERVISE = 64 }; enum { PF_ValueDisplayFlag_NONE=0, PF_ValueDisplayFlag_PERCENT=1 };
enum { PF_PixelFormat_ARGB32=1650946657, PF_PixelFormat_ARGB64=909206881, PF_PixelFormat_ARGB128=842229089, PF_PixelFormat_INVALID=1717854562 };
struct PF_RenderRequest { PF_LRect rect; A_long field, channel_mask; PF_Boolean preserve_rgb_of_zero_alpha; char unused[3]; A_long reserved[4]; };
struct PF_CheckoutResult { PF_LRect result_rect, max_result_rect; A_long par[2]; PF_Boolean solid; PF_Boolean rb[3]; A_long ref_width, ref_height; A_long reserved[6]; };
typedef void (*PF_DeletePreRenderDataFunc)(void*);
struct PF_PreRenderInput { PF_RenderRequest output_request; short bitdepth; const void* gpu_data; A_long what_gpu; A_u_long device_index; };
struct PF_PreRenderOutput { PF_LRect result_rect, max_result_rect; PF_Boolean solid, reserved; A_long flags; void* pre_render_data; PF_DeletePreRenderDataFunc delete_pre_render_data_func; };
struct PF_PreRenderCallbacks { PF_Err (*checkout_layer)(PF_ProgPtr, PF_ParamIndex, A_long, const PF_RenderRequest*, A_long, A_long, A_u_long, PF_CheckoutResult*); };
struct PF_PreRenderExtra { PF_PreRenderInput* input; PF_PreRenderOutput* output; PF_PreRenderCallbacks* cb; };
struct PF_SmartRenderInput { PF_RenderRequest output_request; short bitdepth; void* pre_render_data; const void* gpu_data; A_long what_gpu; A_u_long device_index; };
struct PF_SmartRenderCallbacks { PF_Err (*checkout_layer_pixels)(PF_ProgPtr, A_long, PF_EffectWorld**); PF_Err (*checkin_layer_pixels)(PF_ProgPtr, A_long); PF_Err (*checkout_output)(PF_ProgPtr, PF_EffectWorld**); };
struct PF_SmartRenderExtra { PF_SmartRenderInput* input; PF_SmartRenderCallbacks* cb; };
#define kPFWorldSuite "PF World Suite"
#define kPFWorldSuiteVersion2 2
struct PF_WorldSuite2 { void* a; void* b; PF_Err (*PF_GetPixelFormat)(const PF_EffectWorld*, PF_PixelFormat*); };
#define kPFIterate8Suite "PF Iterate8 Suite"
#define kPFIterate8SuiteVersion2 2
struct PF_Iterate8Suite2 { PF_Err (*iterate_generic)(A_long, void*, PF_Err (*)(void*, A_long, A_long, A_long)); };
#define kPFParamUtilsSuite "PF Param Utils Suite"
#define kPFParamUtilsSuiteVersion3 3
struct PF_ParamUtilsSuite3 { PF_Err (*PF_UpdateParamUI)(PF_ProgPtr, PF_ParamIndex, const PF_ParamDef*); };
#define PF_PLUG_IN_VERSION 13
#define PF_PLUG_IN_SUBVERS 29
typedef PF_Err (*PF_PluginDataCB2)(PF_PluginDataPtr, const A_u_char*, const A_u_char*, const A_u_char*, const A_u_char*, A_long, A_long, A_long, A_long, const A_u_char*);
#define PF_ADD_PARAM(IN, I, D) ((IN)->inter.add_param((IN)->effect_ref, (I), (D)))
#define PF_ADD_SLIDER(N, VMIN, VMAX, SMIN, SMAX, D, ID) do { strcpy(def.name, N); def.u.sd.value = (D); def.id = (ID); if (!err) err = PF_ADD_PARAM(in_data, -1, &def); } while (0)
#define PF_ADD_FLOAT_SLIDERX(N, VMIN, VMAX, SMIN, SMAX, D, PREC, DISP, FL, ID) do { strcpy(def.name, N); def.u.fs_d.value = (D); def.flags |= (FL); def.id = (ID); if (!err) err = PF_ADD_PARAM(in_data, -1, &def); } while (0)
#define PF_ADD_CHECKBOXX(N, D, FL, ID) do { strcpy(def.name, N); def.u.bd.value = (D); def.flags |= (FL); def.id = (ID); if (!err) err = PF_ADD_PARAM(in_data, -1, &def); } while (0)
#define PF_ADD_POPUPX(N, NC, D, STR, FL, ID) do { strcpy(def.name, N); def.u.pd.value = (D); def.flags |= (FL); def.id = (ID); (void)(STR); if (!err) err = PF_ADD_PARAM(in_data, -1, &def); } while (0)
#define PF_ADD_ANGLE(N, D, ID) do { strcpy(def.name, N); def.u.ad.value = (D) << 16; def.id = (ID); if (!err) err = PF_ADD_PARAM(in_data, -1, &def); } while (0)
#define PF_ADD_COLOR(N, R, G, B, ID) do { strcpy(def.name, N); def.u.cd.value.red = (R); def.u.cd.value.green = (G); def.u.cd.value.blue = (B); def.id = (ID); if (!err) err = PF_ADD_PARAM(in_data, -1, &def); } while (0)
#define PF_ADD_TOPIC(N, ID) do { strcpy(def.name, N); def.id = (ID); if (!err) err = PF_ADD_PARAM(in_data, -1, &def); } while (0)
#define PF_END_TOPIC(ID) do { def.id = (ID); if (!err) err = PF_ADD_PARAM(in_data, -1, &def); } while (0)
typedef PF_LayerDef PF_LayerDef;
