// CPU path test: a mock AE host drives EffectMain through GLOBAL_SETUP, PARAMS_SETUP,
// UPDATE_PARAMS_UI, SMART_PRE_RENDER and SMART_RENDER in 8 / 16 / 32 bpc for every
// blend mode, and checks resource balance and the "static areas stay untouched"
// property of the Optical mode.
#include "ae_stub.h"
#include "BrightTimes.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>
#include <cmath>
#include <string>
static std::vector<PF_ParamDef> g_params;
static A_long g_fmt = PF_PixelFormat_ARGB32;
static int W = 320, H = 180;
struct Frame { std::vector<char> buf; PF_EffectWorld w; };
static std::map<A_long, A_long> g_checkTime;  // id -> time
static std::map<A_long, Frame*> g_out;
static int pixSize() { return g_fmt == PF_PixelFormat_ARGB32 ? 4 : g_fmt == PF_PixelFormat_ARGB64 ? 8 : 16; }
static float maxv() { return g_fmt == PF_PixelFormat_ARGB32 ? 255.f : g_fmt == PF_PixelFormat_ARGB64 ? 32768.f : 1.f; }
static void put(PF_EffectWorld& w, int x, int y, float a, float r, float g, float b) {
  char* p = (char*)w.data + y * w.rowbytes + x * pixSize(); float m = maxv();
  if (g_fmt == PF_PixelFormat_ARGB32) { auto* q=(PF_Pixel8*)p; q->alpha=a*m+.5f; q->red=r*m+.5f; q->green=g*m+.5f; q->blue=b*m+.5f; }
  else if (g_fmt == PF_PixelFormat_ARGB64) { auto* q=(PF_Pixel16*)p; q->alpha=a*m+.5f; q->red=r*m+.5f; q->green=g*m+.5f; q->blue=b*m+.5f; }
  else { auto* q=(PF_PixelFloat*)p; q->alpha=a; q->red=r; q->green=g; q->blue=b; }
}
static void get(const PF_EffectWorld& w, int x, int y, float* o) {
  const char* p = (const char*)w.data + y * w.rowbytes + x * pixSize(); float m = maxv();
  if (g_fmt == PF_PixelFormat_ARGB32) { auto* q=(const PF_Pixel8*)p; o[0]=q->alpha/m; o[1]=q->red/m; o[2]=q->green/m; o[3]=q->blue/m; }
  else if (g_fmt == PF_PixelFormat_ARGB64) { auto* q=(const PF_Pixel16*)p; o[0]=q->alpha/m; o[1]=q->red/m; o[2]=q->green/m; o[3]=q->blue/m; }
  else { auto* q=(const PF_PixelFloat*)p; o[0]=q->alpha; o[1]=q->red; o[2]=q->green; o[3]=q->blue; }
}
static Frame* makeWorld() { auto* f = new Frame; f->w = {}; f->w.width=W; f->w.height=H; f->w.rowbytes=W*pixSize()+16; f->buf.assign(f->w.rowbytes*H, 0); f->w.data=f->buf.data(); return f; }
static Frame* renderFrame(A_long t) {  // t in frames (time_step = 1)
  Frame* f = makeWorld();
  for (int y=0;y<H;++y) for (int x=0;x<W;++x) {
    float v = 0.5f + 0.3f*y/H; if ((x/16+y/16)%2) v -= 0.05f;
    float r=v, g=v*0.9f, b=v*0.75f, a=1.f;
    float cx = 80 + t*10.f; if (std::fabs(x-cx)<30 && std::fabs(y-90.f)<40) { r=0.1f; g=0.3f; b=0.25f; }
    put(f->w, x, y, a, r, g, b);
  }
  return f;
}
static PF_Err add_param(PF_ProgPtr, A_long, PF_ParamDef* d) { g_params.push_back(*d); return 0; }
static PF_Err checkout_param(PF_ProgPtr, PF_ParamIndex i, A_long, A_long, A_u_long, PF_ParamDef* p) { if (i<=0 || i>(int)g_params.size()) return PF_Err_BAD_CALLBACK_PARAM; *p = g_params[i-1]; return 0; }
static PF_Err checkin_param(PF_ProgPtr, PF_ParamDef*) { return 0; }
static PF_Err abort_cb(PF_ProgPtr) { return 0; }
static PF_Err checkout_layer(PF_ProgPtr, PF_ParamIndex idx, A_long id, const PF_RenderRequest*, A_long t, A_long, A_u_long, PF_CheckoutResult* r) {
  if (idx != 0) return PF_Err_BAD_CALLBACK_PARAM;
  g_checkTime[id] = t; r->result_rect = {0,0,W,H}; r->max_result_rect = {0,0,W,H}; return 0; }
static std::map<A_long, Frame*> g_pix;
static PF_Err checkout_layer_pixels(PF_ProgPtr, A_long id, PF_EffectWorld** w) { if (!g_checkTime.count(id)) return PF_Err_BAD_CALLBACK_PARAM; g_pix[id] = renderFrame(g_checkTime[id]); *w = &g_pix[id]->w; return 0; }
static PF_Err checkin_layer_pixels(PF_ProgPtr, A_long id) { delete g_pix[id]; g_pix.erase(id); return 0; }
static Frame* g_output;
static PF_Err checkout_output(PF_ProgPtr, PF_EffectWorld** w) { *w = &g_output->w; return 0; }
static PF_Err getfmt(const PF_EffectWorld*, PF_PixelFormat* f) { *f = g_fmt; return 0; }
static std::vector<std::string> g_threads;
static PF_Err iterate_generic(A_long n, void* ref, PF_Err (*fn)(void*, A_long, A_long, A_long)) { for (A_long i=0;i<n;++i) { PF_Err e = fn(ref, 0, i, n); if (e) return e; } return 0; }
static PF_Iterate8Suite2 g_iter{iterate_generic}; static PF_WorldSuite2 g_world{nullptr,nullptr,getfmt};
static PF_Err updui(PF_ProgPtr, PF_ParamIndex i, const PF_ParamDef* d) { std::printf("  UI param %d %s\n", i, (d->ui_flags & PF_PUI_DISABLED) ? "disabled" : "enabled"); return 0; }
static PF_ParamUtilsSuite3 g_pu{updui};
static int g_acquired = 0;
static SPErr acq(const char* n, int32_t, const void** s) { ++g_acquired; std::string sn(n);
  if (sn == kPFIterate8Suite) *s = &g_iter; else if (sn == kPFWorldSuite) *s = &g_world; else if (sn == kPFParamUtilsSuite) *s = &g_pu; else return 1; return 0; }
static SPErr rel(const char*, int32_t) { --g_acquired; return 0; }

int main(int argc, char** argv) {
  SPBasicSuite basic{acq, rel};
  PF_InData in{}; in.inter = {checkout_param, checkin_param, add_param, abort_cb}; in.time_step = 1; in.time_scale = 24; in.current_time = 5; in.pica_basicP = &basic;
  PF_OutData out{};
  PF_Err e = EffectMain(PF_Cmd_GLOBAL_SETUP, &in, &out, nullptr, nullptr, nullptr); std::printf("global %d flags %x %x ver %u\n", e, out.out_flags, out.out_flags2, out.my_version);
  e = EffectMain(PF_Cmd_PARAMS_SETUP, &in, &out, nullptr, nullptr, nullptr); std::printf("params %d num %d added %zu\n", e, out.num_params, g_params.size());
  if ((int)g_params.size() + 1 != out.num_params) { std::printf("PARAM COUNT MISMATCH\n"); return 1; }
  // UI update
  std::vector<PF_ParamDef*> pp(out.num_params); PF_ParamDef dummy{}; pp[0] = &dummy; for (int i=1;i<out.num_params;++i) pp[i] = &g_params[i-1];
  g_params[BT_COLOR_MODE-1].u.pd.value = 3;
  e = EffectMain(PF_Cmd_UPDATE_PARAMS_UI, &in, &out, pp.data(), nullptr, nullptr); std::printf("ui %d\n", e);
  g_params[BT_COLOR_MODE-1].u.pd.value = 1;
  const char* outdir = argc > 1 ? argv[1] : ".";
  A_long fmts[3] = {PF_PixelFormat_ARGB32, PF_PixelFormat_ARGB64, PF_PixelFormat_ARGB128};
  int blendModes[4] = {1,2,3,4};
  for (int bm : blendModes) for (A_long fmt : fmts) {
    g_fmt = fmt; g_checkTime.clear();
    g_params[BT_BLEND_MODE-1].u.pd.value = bm;
    g_params[BT_INTENSITY-1].u.fs_d.value = bm == 3 ? 35 : 100;
    PF_PreRenderInput pin{}; PF_PreRenderOutput pout{}; PF_PreRenderCallbacks pcb{checkout_layer};
    PF_PreRenderExtra pex{&pin, &pout, &pcb};
    e = EffectMain(PF_Cmd_SMART_PRE_RENDER, &in, &out, nullptr, nullptr, &pex);
    if (e) { std::printf("prerender err %d\n", e); return 1; }
    g_output = makeWorld();
    PF_SmartRenderInput sin{}; sin.pre_render_data = pout.pre_render_data; PF_SmartRenderCallbacks scb{checkout_layer_pixels, checkin_layer_pixels, checkout_output};
    PF_SmartRenderExtra sex{&sin, &scb};
    e = EffectMain(PF_Cmd_SMART_RENDER, &in, &out, nullptr, nullptr, &sex);
    if (e) { std::printf("render err %d\n", e); return 1; }
    pout.delete_pre_render_data_func(pout.pre_render_data);
    // checks: static bg pixel (300,10) must equal original in optical mode
    float o[4], src[4]; get(g_output->w, 300, 10, o); Frame* f0 = renderFrame(5); get(f0->w, 300, 10, src);
    std::printf("blend %d fmt %d checkouts %zu leaked_pix %zu suites %d | bg out %.4f %.4f %.4f %.4f src %.4f %.4f %.4f\n", bm, pixSize()*8/4, g_checkTime.size(), g_pix.size(), g_acquired, o[0],o[1],o[2],o[3], src[1],src[2],src[3]);
    if (fmt == PF_PixelFormat_ARGB32) {
      std::string path = std::string(outdir) + "/host_blend" + std::to_string(bm) + ".ppm"; FILE* fp = fopen(path.c_str(), "wb"); fprintf(fp, "P6\n%d %d\n255\n", W, H);
      for (int y=0;y<H;++y) for (int x=0;x<W;++x) { float q[4]; get(g_output->w,x,y,q); for (int c=1;c<4;++c) fputc((int)(q[c]*255+.5f), fp); }
      fclose(fp);
    }
    if (g_pix.size() || g_acquired) { std::printf("FAIL: leaked checkouts/suites\n"); return 1; }
    if (g_checkTime.size() != 10) { std::printf("FAIL: expected 9 samples + current\n"); return 1; }
    if (bm == 1 && (std::fabs(o[1]-src[1]) > 1.5f/255 || std::fabs(o[2]-src[2]) > 1.5f/255 || std::fabs(o[3]-src[3]) > 1.5f/255)) { std::printf("FAIL: optical changed a static pixel\n"); return 1; }
    delete f0; delete g_output;
  }
  // Backward / forward counts are independent: 2 back, 6 forward (fractional
  // keyframe values round to whole frames).
  {
    g_params[BT_FRAMES_BACKWARD-1].u.fs_d.value = 2.2;
    g_params[BT_FRAMES_FORWARD-1].u.fs_d.value  = 5.6;
    g_checkTime.clear();
    PF_PreRenderInput pin{}; PF_PreRenderOutput pout{}; PF_PreRenderCallbacks pcb{checkout_layer};
    PF_PreRenderExtra pex{&pin, &pout, &pcb};
    if (EffectMain(PF_Cmd_SMART_PRE_RENDER, &in, &out, nullptr, nullptr, &pex)) { std::printf("FAIL: prerender\n"); return 1; }
    pout.delete_pre_render_data_func(pout.pre_render_data);
    std::vector<A_long> times;
    for (auto& kv : g_checkTime) if (kv.first != 10000) times.push_back(kv.second);
    const std::vector<A_long> expected = {3, 4, 5, 6, 7, 8, 9, 10, 11};  // current_time 5: -2..+6
    if (times != expected) { std::printf("FAIL: asymmetric backward/forward\n"); return 1; }
    std::printf("asymmetric 2 back / 6 forward: OK\n");
    g_params[BT_FRAMES_BACKWARD-1].u.fs_d.value = 4;
    g_params[BT_FRAMES_FORWARD-1].u.fs_d.value  = 4;
  }
  std::printf("checkout times:"); for (auto& kv : g_checkTime) std::printf(" %d@%d", kv.first, kv.second); std::printf("\n");
  std::printf("ALL OK\n");
  return 0;
}
