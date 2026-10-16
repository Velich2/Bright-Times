// Bright Times - minimal OpenCL 1.2 API, loaded at runtime.
//
// The plug-in does not link against OpenCL and needs no OpenCL SDK to build:
// OpenCL.dll (installed by every GPU driver) is loaded only when After Effects
// asks for an OpenCL device. Declarations follow the Khronos cl.h ABI.

#pragma once

#include <cstddef>
#include <cstdint>

namespace btcl {

typedef int32_t  cl_int;
typedef uint32_t cl_uint;
typedef struct _cl_context*       cl_context;
typedef struct _cl_device_id*     cl_device_id;
typedef struct _cl_program*       cl_program;
typedef struct _cl_kernel*        cl_kernel;
typedef struct _cl_mem*           cl_mem;
typedef struct _cl_command_queue* cl_command_queue;
typedef struct _cl_event*         cl_event;

constexpr cl_int CL_SUCCESS = 0;

#if defined(_WIN32)
#define BTCL_CALL __stdcall
#else
#define BTCL_CALL
#endif

struct Api {
    cl_program (BTCL_CALL* CreateProgramWithSource)(cl_context, cl_uint, const char**, const size_t*, cl_int*);
    cl_int     (BTCL_CALL* BuildProgram)(cl_program, cl_uint, const cl_device_id*, const char*,
                                         void (BTCL_CALL*)(cl_program, void*), void*);
    cl_kernel  (BTCL_CALL* CreateKernel)(cl_program, const char*, cl_int*);
    cl_int     (BTCL_CALL* ReleaseKernel)(cl_kernel);
    cl_int     (BTCL_CALL* ReleaseProgram)(cl_program);
    cl_int     (BTCL_CALL* SetKernelArg)(cl_kernel, cl_uint, size_t, const void*);
    cl_int     (BTCL_CALL* EnqueueNDRangeKernel)(cl_command_queue, cl_kernel, cl_uint, const size_t*,
                                                 const size_t*, const size_t*, cl_uint, const cl_event*,
                                                 cl_event*);
};

// Loads the OpenCL runtime once; nullptr if it is not installed.
const Api* Load();

}  // namespace btcl
