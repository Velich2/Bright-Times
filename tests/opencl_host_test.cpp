// GPU path test: a mock AE host runs the real EffectMain twice per case - CPU
// (ARGB128) and GPU (OpenCL, GPU_BGRA128) - and requires identical results.
// Works with any OpenCL device (e.g. pocl on CPU-only machines).
#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>
#include "ae_stub.h"
#include "BrightTimes.h"
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
static std::vector<PF_ParamDef> g_params;
static int W = 200, H = 120;
static bool g_gpu = false;
static int g_srcOriginShift = 0;  // makes source worlds smaller/offset to test mapping
static cl_context g_ctx; static cl_command_queue g_q; static cl_device_id g_dev;
struct World { PF_EffectWorld w{}; std::vector<float> host; cl_mem mem = nullptr; };
static std::map<PF_EffectWorld*, World*> g_worlds;
static World* newWorld(int w, int h, int ox, int oy) {
  auto* r = new World; r->w.width = w; r->w.height = h; r->w.rowbytes = (w + 3) * 16; r->w.origin_x = ox; r->w.origin_y = oy;
  r->host.assign(r->w.rowbytes / 4 * h, 0.f);
  if (g_gpu) { cl_int e; r->mem = clCreateBuffer(g_ctx, CL_MEM_READ_WRITE, r->host.size()*4, nullptr, &e); r->w.data = nullptr; }
  else r->w.data = r->host.data();
  g_worlds[&r->w] = r; return r;
}
static void upload(World* r) { if (g_gpu) clEnqueueWriteBuffer(g_q, r->mem, CL_TRUE, 0, r->host.size()*4, r->host.data(), 0, nullptr, nullptr); }
static void download(World* r) { if (g_gpu) clEnqueueReadBuffer(g_q, r->mem, CL_TRUE, 0, r->host.size()*4, r->host.data(), 0, nullptr, nullptr); }
static void freeWorld(World* r) { if (r->mem) clReleaseMemObject(r->mem); g_worlds.erase(&r->w); delete r; }
// pixel in layer coords
static void scene(float t, int x, int y, float* rgba) {
  float v = 0.5f + 0.3f*y/H; if ((x/16+y/16)%2) v -= 0.05f;
  rgba[0]=v; rgba[1]=v*0.9f; rgba[2]=v*0.75f; rgba[3]=1.f;
  if (x < 6) rgba[3] = 0.5f;  // some partial alpha
  float cx = 50 + t*8.f; if (std::fabs(x-cx)<20 && std::fabs(y-60.f)<30) { rgba[0]=0.1f; rgba[1]=0.3f; rgba[2]=0.25f; }
}
static World* renderFrame(A_long t) {
  int sh = g_srcOriginShift; World* r = newWorld(W - sh, H - sh, sh, sh);
  for (int y=0;y<r->w.height;++y) for (int x=0;x<r->w.width;++x) {
    float c[4]; scene((float)t, x + sh, y + sh, c); float* p = &r->host[y * r->w.rowbytes/4 + x*4];
    if (g_gpu) { p[0]=c[2]; p[1]=c[1]; p[2]=c[0]; p[3]=c[3]; } else { p[0]=c[3]; p[1]=c[0]; p[2]=c[1]; p[3]=c[2]; } // BGRA vs ARGB
  }
  upload(r); return r;
}
static PF_Err add_param(PF_ProgPtr, A_long, PF_ParamDef* d) { g_params.push_back(*d); return 0; }
static PF_Err checkout_param(PF_ProgPtr, PF_ParamIndex i, A_long, A_long, A_u_long, PF_ParamDef* p) { *p = g_params[i-1]; return 0; }
static PF_Err checkin_param(PF_ProgPtr, PF_ParamDef*) { return 0; }
static PF_Err abort_cb(PF_ProgPtr) { return 0; }
static std::map<A_long, A_long> g_checkTime; static std::map<A_long, World*> g_pix; static World* g_out;
static PF_Err checkout_layer(PF_ProgPtr, PF_ParamIndex, A_long id, const PF_RenderRequest*, A_long t, A_long, A_u_long, PF_CheckoutResult* r) { g_checkTime[id]=t; r->result_rect={0,0,W,H}; r->max_result_rect={0,0,W,H}; return 0; }
// AE rule (error 25::244): the output may only be checked out after at least
// one input has been checked out in this render.
static int g_inputCheckouts = 0;
static PF_Err checkout_layer_pixels(PF_ProgPtr, A_long id, PF_EffectWorld** w) { ++g_inputCheckouts; g_pix[id] = renderFrame(g_checkTime[id]); *w = &g_pix[id]->w; return 0; }
static PF_Err checkin_layer_pixels(PF_ProgPtr, A_long id) { if (g_gpu) clFinish(g_q); freeWorld(g_pix[id]); g_pix.erase(id); return 0; }
static PF_Err checkout_output(PF_ProgPtr, PF_EffectWorld** w) {
  if (g_inputCheckouts == 0) { std::printf("FAIL: output checked out before any input (AE 25::244)\n"); return PF_Err_BAD_CALLBACK_PARAM; }
  *w = &g_out->w; return 0; }
static PF_Err getfmt(const PF_EffectWorld*, PF_PixelFormat* f) { *f = g_gpu ? (A_long)PF_PixelFormat_GPU_BGRA128 : (A_long)PF_PixelFormat_ARGB128; return 0; }
static PF_Err iterate_generic(A_long n, void* ref, PF_Err (*fn)(void*, A_long, A_long, A_long)) { for (A_long i=0;i<n;++i) if (PF_Err e = fn(ref,0,i,n)) return e; return 0; }
static PF_Err devinfo(PF_ProgPtr, A_u_long, PF_GPUDeviceInfo* i) { i->device_framework = PF_GPU_Framework_OPENCL; i->devicePV = g_dev; i->contextPV = g_ctx; i->command_queuePV = g_q; return 0; }
static PF_Err worlddata(PF_ProgPtr, PF_EffectWorld* w, void** m) { *m = g_worlds.at(w)->mem; return 0; }
static PF_Iterate8Suite2 g_iter{iterate_generic}; static PF_WorldSuite2 g_world{nullptr,nullptr,getfmt}; static PF_GPUDeviceSuite1 g_gs{devinfo, worlddata};
static PF_Err updui(PF_ProgPtr, PF_ParamIndex, const PF_ParamDef*) { return 0; } static PF_ParamUtilsSuite3 g_pu{updui};
static SPErr acq(const char* n, int32_t, const void** s) { std::string sn(n);
  if (sn==kPFIterate8Suite) *s=&g_iter; else if (sn==kPFWorldSuite) *s=&g_world; else if (sn==kPFParamUtilsSuite) *s=&g_pu; else if (sn==kPFGPUDeviceSuite) *s=&g_gs; else return 1; return 0; }
static SPErr rel(const char*, int32_t) { return 0; }

static PF_InData in; static PF_OutData out; static void* g_gpuData;
static std::vector<float> run(bool gpu) {  // returns RGBA straight per pixel
  g_gpu = gpu; g_checkTime.clear();
  PF_PreRenderInput pin{}; pin.what_gpu = gpu ? PF_GPU_Framework_OPENCL : PF_GPU_Framework_NONE; pin.gpu_data = g_gpuData;
  PF_PreRenderOutput pout{}; PF_PreRenderCallbacks pcb{checkout_layer}; PF_PreRenderExtra pex{&pin,&pout,&pcb};
  if (PF_Err e = EffectMain(PF_Cmd_SMART_PRE_RENDER, &in, &out, nullptr, nullptr, &pex)) { printf("prerender %d\n", e); exit(1); }
  if (gpu && !(pout.flags & PF_RenderOutputFlag_GPU_RENDER_POSSIBLE)) { printf("GPU not flagged possible\n"); exit(1); }
  g_out = newWorld(W, H, 0, 0); upload(g_out);
  PF_SmartRenderInput sin{}; sin.pre_render_data = pout.pre_render_data; sin.what_gpu = pin.what_gpu; sin.gpu_data = g_gpuData;
  PF_SmartRenderCallbacks scb{checkout_layer_pixels, checkin_layer_pixels, checkout_output}; PF_SmartRenderExtra sex{&sin,&scb};
  g_inputCheckouts = 0;
  if (PF_Err e = EffectMain(gpu ? PF_Cmd_SMART_RENDER_GPU : PF_Cmd_SMART_RENDER, &in, &out, nullptr, nullptr, &sex)) { printf("render %d\n", e); exit(1); }
  pout.delete_pre_render_data_func(pout.pre_render_data);
  if (gpu) clFinish(g_q);
  download(g_out);
  std::vector<float> res(W*H*4);
  for (int y=0;y<H;++y) for (int x=0;x<W;++x) { float* p = &g_out->host[y*g_out->w.rowbytes/4 + x*4]; float* d = &res[(y*W+x)*4];
    if (gpu) { d[0]=p[2]; d[1]=p[1]; d[2]=p[0]; d[3]=p[3]; } else { d[0]=p[1]; d[1]=p[2]; d[2]=p[3]; d[3]=p[0]; } }
  freeWorld(g_out); return res;
}
int main() {
  cl_platform_id plat; cl_uint np; clGetPlatformIDs(1, &plat, &np); if (!np) { printf("no OpenCL\n"); return 1; }
  clGetDeviceIDs(plat, CL_DEVICE_TYPE_ALL, 1, &g_dev, nullptr); cl_int e; g_ctx = clCreateContext(nullptr, 1, &g_dev, nullptr, nullptr, &e); g_q = clCreateCommandQueue(g_ctx, g_dev, 0, &e);
  char name[256]; clGetDeviceInfo(g_dev, CL_DEVICE_NAME, sizeof name, name, nullptr); printf("OpenCL device: %s\n", name);
  SPBasicSuite basic{acq, rel}; in.inter = {checkout_param, checkin_param, add_param, abort_cb}; in.time_step = 1; in.time_scale = 24; in.current_time = 3; in.pica_basicP = &basic;
  EffectMain(PF_Cmd_GLOBAL_SETUP, &in, &out, nullptr, nullptr, nullptr);
  printf("flags2 %x (gpu bit %d)\n", out.out_flags2, !!(out.out_flags2 & PF_OutFlag2_SUPPORTS_GPU_RENDER_F32));
  EffectMain(PF_Cmd_PARAMS_SETUP, &in, &out, nullptr, nullptr, nullptr);
  PF_GPUDeviceSetupInput gi{PF_GPU_Framework_OPENCL, 0}; PF_GPUDeviceSetupOutput go{}; PF_GPUDeviceSetupExtra gx{&gi, &go}; out.out_flags2 = 0;
  e = EffectMain(PF_Cmd_GPU_DEVICE_SETUP, &in, &out, nullptr, nullptr, &gx);
  printf("gpu setup err %d data %p flags2 %x\n", e, go.gpu_data, out.out_flags2); if (!go.gpu_data) return 1; g_gpuData = go.gpu_data;
  // CUDA must be refused in this build
  PF_GPUDeviceSetupInput gi2{PF_GPU_Framework_CUDA, 0}; PF_GPUDeviceSetupOutput go2{}; PF_GPUDeviceSetupExtra gx2{&gi2, &go2}; out.out_flags2 = PF_OutFlag2_SUPPORTS_GPU_RENDER_F32;
  EffectMain(PF_Cmd_GPU_DEVICE_SETUP, &in, &out, nullptr, nullptr, &gx2); printf("cuda refused: %d\n", !(out.out_flags2 & PF_OutFlag2_SUPPORTS_GPU_RENDER_F32) && !go2.gpu_data);
  struct Case { int blend, tint; float mix, intensity; int colorMode, groupMode; float soft; int shift; bool untinted; };
  Case cases[] = { {1,1,100,100,1,1,0,0,false}, {1,2,100,100,2,1,0,0,false}, {2,1,70,100,1,1,50,0,false}, {3,1,100,35,3,2,0,0,true},
                   {4,2,40,150,1,1,100,0,false}, {5,1,100,200,3,1,29,0,false}, {5,2,70,100,1,2,0,6,true}, {1,1,60,100,1,1,30,7,false}, {3,2,100,50,2,2,0,5,true} };
  int fails = 0;
  for (auto& c : cases) {
    g_params[BT_BLEND_MODE-1].u.pd.value = c.blend; g_params[BT_TINT_MODE-1].u.pd.value = c.tint; g_params[BT_BLEND_ORIGINAL-1].u.fs_d.value = 100 - c.mix;  // c.mix = effect share
    g_params[BT_INTENSITY-1].u.fs_d.value = c.intensity; g_params[BT_COLOR_MODE-1].u.pd.value = c.colorMode; g_params[BT_GROUP_MODE-1].u.pd.value = c.groupMode;
    g_params[BT_GROUP_SOFTNESS-1].u.fs_d.value = c.soft; g_params[BT_CURRENT_UNTINTED-1].u.bd.value = c.untinted; g_srcOriginShift = c.shift;
    auto a = run(false), b = run(true);
    double maxd = 0, sum = 0; for (size_t i=0;i<a.size();++i) { double d = std::fabs(a[i]-b[i]); maxd = std::max(maxd, d); sum += a[i]; }
    bool ok = maxd < 1e-4; fails += !ok;
    printf("blend %d tint %d mix %.0f shift %d : max|cpu-gpu| = %.2e  mean %.4f  %s\n", c.blend, c.tint, c.mix, c.shift, maxd, sum/a.size(), ok ? "OK" : "FAIL");
  }
  // "GPU Acceleration" off: PreRender must not offer the GPU path.
  {
    g_params[BT_USE_GPU-1].u.bd.value = 0;
    PF_PreRenderInput pin{}; pin.what_gpu = PF_GPU_Framework_OPENCL; pin.gpu_data = g_gpuData;
    PF_PreRenderOutput pout{}; PF_PreRenderCallbacks pcb{checkout_layer}; PF_PreRenderExtra pex{&pin,&pout,&pcb};
    EffectMain(PF_Cmd_SMART_PRE_RENDER, &in, &out, nullptr, nullptr, &pex);
    pout.delete_pre_render_data_func(pout.pre_render_data);
    const bool ok = !(pout.flags & PF_RenderOutputFlag_GPU_RENDER_POSSIBLE);
    fails += !ok;
    printf("GPU Acceleration off -> CPU only: %s\n", ok ? "OK" : "FAIL");
    g_params[BT_USE_GPU-1].u.bd.value = 1;
  }
  PF_GPUDeviceSetdownInput di{g_gpuData, PF_GPU_Framework_OPENCL, 0}; PF_GPUDeviceSetdownExtra dx{&di};
  EffectMain(PF_Cmd_GPU_DEVICE_SETDOWN, &in, &out, nullptr, nullptr, &dx);
  printf("%s\n", fails ? "FAILURES" : "ALL OK"); return fails;
}
