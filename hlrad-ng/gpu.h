// =====================================================================================
// hlrad-ng GPU backend (Vulkan compute)
// =====================================================================================
// Optional accelerator for ray jobs that can be batched. The Vulkan loader is
// opened at run time (vulkan-1.dll / libvulkan.so.1, installed by every GPU
// driver), so the executable has no link-time dependency on it; when no usable
// device is found every function here reports failure and the caller keeps
// using the CPU.
//
// A device with VK_KHR_ray_query traces rays against the world as triangles;
// any other Vulkan 1.1 device walks the world BSP in the shader. Every shader
// is embedded as SPIR-V in both builds (see gputrace.glsl).
// =====================================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

/// One BSP node as the shaders read it (std430, 32 bytes).
typedef struct
{
    float plane[4];  // normal xyz, dist
    int children[2]; // tnode index, or negative leaf contents
    int pad[2];
} gpunode_t;

/// Storage buffers a job can bind, at bindings 1 .. GPU_MAX_BUFFERS.
#define GPU_MAX_BUFFERS 9

/// A compute shader with its storage buffers.
typedef struct gpujob_s gpujob_t;

/// Data copied into the buffer at a binding before a dispatch.
typedef struct
{
    int binding;
    const void* data;
    size_t size;
} gpuinput_t;

/// Data copied back from the buffer at a binding after a dispatch. The first
/// clear bytes of the buffer (a multiple of 4) are zeroed before the dispatch.
typedef struct
{
    int binding;
    void* data;
    size_t size;
    size_t clear;
} gpuoutput_t;

/// Opens the Vulkan loader and picks a device. Returns false when the GPU
/// cannot be used; safe to call more than once.
extern bool GpuInit();

/// Releases every Vulkan object, jobs included. Safe to call when GpuInit failed.
extern void GpuShutdown();

/// Name of the device in use, or "" before a successful GpuInit.
extern const char* GpuDeviceName();

/// True when the device traces rays in hardware (VK_KHR_ray_query).
extern bool GpuHasRayQuery();

/// True when the device is a discrete graphics card.
extern bool GpuIsDiscrete();

/// Bytes of memory on the device (its largest device-local heap), and the most one storage
/// buffer can hold.
extern unsigned long long GpuDeviceMemory();
extern unsigned long long GpuMaxBufferBytes();

/// Largest group count of one dispatch.
extern unsigned GpuMaxGroups();

/// Scene for devices without ray queries: the world BSP.
extern bool GpuSetSceneNodes(const gpunode_t* nodes, int numnodes);

/// Scene for devices with ray queries: triangles, 9 floats each.
extern bool GpuSetSceneTriangles(const float* triangles, int numtriangles);

/// Builds the pipeline of a compute shader from the SPIR-V build that fits the
/// device. The shader has the scene at binding 0, numbuffers storage buffers
/// at bindings 1 .. numbuffers and a push-constant block of paramsize bytes.
/// Returns NULL on failure. Needs a scene.
extern gpujob_t* GpuCreateJob(const uint32_t* rayquery, size_t rayquerysize, const uint32_t* bspwalk,
                              size_t bspwalksize, int numbuffers, size_t paramsize);

/// Fills the buffer at a binding with data that stays the same across dispatches.
extern bool GpuJobUpload(gpujob_t* job, int binding, const void* data, size_t size);

/// Gives the buffer at a binding room for size bytes that stay on the device across
/// dispatches; zero sets them to zero.
extern bool GpuJobReserve(gpujob_t* job, int binding, size_t size, bool zero);

/// Makes a binding of a job use the buffer another job has at one of its bindings.
extern bool GpuJobShare(gpujob_t* job, int binding, gpujob_t* from, int frombinding);

/// Runs the shader over groups work groups and waits for it.
extern bool GpuJobRun(gpujob_t* job, const void* params, unsigned groups, const gpuinput_t* inputs, int numinputs,
                      const gpuoutput_t* outputs, int numoutputs);
