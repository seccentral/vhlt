// =====================================================================================
// hlrad-ng GPU backend (Vulkan compute), see gpu.h
// =====================================================================================
#ifdef SYSTEM_WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "qrad.h"
#include "gpu.h"

#define VK_NO_PROTOTYPES
#include "vulkan/vulkan_core.h"

// Vulkan entry points are looked up at run time, so the tool starts on
// machines without a Vulkan loader.
#define GPU_INSTANCE_FUNCTIONS(X)                                                                                      \
    X(DestroyInstance)                                                                                                 \
    X(EnumeratePhysicalDevices)                                                                                        \
    X(GetPhysicalDeviceProperties)                                                                                     \
    X(GetPhysicalDeviceQueueFamilyProperties)                                                                          \
    X(GetPhysicalDeviceMemoryProperties)                                                                               \
    X(GetPhysicalDeviceFeatures2)                                                                                      \
    X(EnumerateDeviceExtensionProperties)                                                                              \
    X(CreateDevice)                                                                                                    \
    X(GetDeviceProcAddr)

#define GPU_DEVICE_FUNCTIONS(X)                                                                                        \
    X(DestroyDevice)                                                                                                   \
    X(GetDeviceQueue)                                                                                                  \
    X(CreateBuffer)                                                                                                    \
    X(DestroyBuffer)                                                                                                   \
    X(GetBufferMemoryRequirements)                                                                                     \
    X(AllocateMemory)                                                                                                  \
    X(FreeMemory)                                                                                                      \
    X(BindBufferMemory)                                                                                                \
    X(MapMemory)                                                                                                       \
    X(UnmapMemory)                                                                                                     \
    X(CreateShaderModule)                                                                                              \
    X(DestroyShaderModule)                                                                                             \
    X(CreateDescriptorSetLayout)                                                                                       \
    X(DestroyDescriptorSetLayout)                                                                                      \
    X(CreatePipelineLayout)                                                                                            \
    X(DestroyPipelineLayout)                                                                                           \
    X(CreateComputePipelines)                                                                                          \
    X(DestroyPipeline)                                                                                                 \
    X(CreateDescriptorPool)                                                                                            \
    X(DestroyDescriptorPool)                                                                                           \
    X(AllocateDescriptorSets)                                                                                          \
    X(UpdateDescriptorSets)                                                                                            \
    X(CreateCommandPool)                                                                                               \
    X(DestroyCommandPool)                                                                                              \
    X(AllocateCommandBuffers)                                                                                          \
    X(BeginCommandBuffer)                                                                                              \
    X(EndCommandBuffer)                                                                                                \
    X(CmdBindPipeline)                                                                                                 \
    X(CmdBindDescriptorSets)                                                                                           \
    X(CmdPushConstants)                                                                                                \
    X(CmdDispatch)                                                                                                     \
    X(CmdCopyBuffer)                                                                                                   \
    X(CmdFillBuffer)                                                                                                   \
    X(CmdPipelineBarrier)                                                                                              \
    X(QueueSubmit)                                                                                                     \
    X(QueueWaitIdle)

// Only on devices that trace rays in hardware.
#define GPU_RAYQUERY_FUNCTIONS(X)                                                                                      \
    X(GetBufferDeviceAddress)                                                                                          \
    X(CreateAccelerationStructureKHR)                                                                                  \
    X(DestroyAccelerationStructureKHR)                                                                                 \
    X(GetAccelerationStructureBuildSizesKHR)                                                                           \
    X(GetAccelerationStructureDeviceAddressKHR)                                                                        \
    X(CmdBuildAccelerationStructuresKHR)

#define GPU_DECLARE(name) static PFN_vk##name vk##name = NULL;
GPU_INSTANCE_FUNCTIONS(GPU_DECLARE)
GPU_DEVICE_FUNCTIONS(GPU_DECLARE)
GPU_RAYQUERY_FUNCTIONS(GPU_DECLARE)
#undef GPU_DECLARE
static PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = NULL;
static PFN_vkCreateInstance vkCreateInstance = NULL;

typedef struct
{
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceSize size;
    bool borrowed; // another job owns it (GpuJobShare)
} gpubuffer_t;

struct gpujob_s
{
    VkShaderModule shader;
    VkDescriptorSetLayout setlayout;
    VkPipelineLayout layout;
    VkPipeline pipeline;
    VkDescriptorPool pool;
    VkDescriptorSet set;
    int numbuffers;
    size_t paramsize;
    gpubuffer_t buffers[GPU_MAX_BUFFERS + 1]; // by binding; [0] is the scene
    struct gpujob_s* next;
};

// The acceleration-structure scratch offset alignment is at most this (Vulkan limit).
#define GPU_SCRATCH_ALIGNMENT 256

static bool g_gpu_tried = false;
static bool g_gpu_ready = false;
static bool g_gpu_rayquery = false;
static bool g_gpu_discrete = false;
static bool g_gpu_scene = false;
static void* g_gpu_library = NULL;
static char g_gpu_devicename[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = "";
static uint32_t g_gpu_maxgroups = 0;
static uint32_t g_gpu_maxbuffer = 0; // largest storage buffer range
static uint32_t g_gpu_apiversion = VK_MAKE_API_VERSION(0, 1, 1, 0);

static VkInstance g_instance = VK_NULL_HANDLE;
static VkPhysicalDevice g_physical = VK_NULL_HANDLE;
static VkDevice g_device = VK_NULL_HANDLE;
static VkQueue g_queue = VK_NULL_HANDLE;
static VkPhysicalDeviceMemoryProperties g_memprops;
static VkCommandPool g_cmdpool = VK_NULL_HANDLE;
static VkCommandBuffer g_cmd = VK_NULL_HANDLE;

static gpubuffer_t g_staging; // host visible, for uploads and read-back
static void* g_staging_mapped = NULL;
static gpubuffer_t g_nodes;      // scene of the BSP walk
static gpubuffer_t g_vertices;   // scene of the ray queries
static gpubuffer_t g_blasbuffer; // bottom-level acceleration structure: the triangles
static gpubuffer_t g_tlasbuffer; // top-level acceleration structure: one instance of them
static gpubuffer_t g_instances;
static VkAccelerationStructureKHR g_blas = VK_NULL_HANDLE;
static VkAccelerationStructureKHR g_tlas = VK_NULL_HANDLE;
static gpujob_t* g_jobs = NULL;

// -------------------------------------------------------------------------------------
// loader
// -------------------------------------------------------------------------------------

static void* GpuLoadSymbol(const char* name)
{
#ifdef SYSTEM_WIN32
    return (void*)GetProcAddress((HMODULE)g_gpu_library, name);
#else
    return dlsym(g_gpu_library, name);
#endif
}

static bool GpuOpenLibrary()
{
#ifdef SYSTEM_WIN32
    g_gpu_library = (void*)LoadLibraryA("vulkan-1.dll");
#else
    g_gpu_library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
#endif
    if (!g_gpu_library)
    {
        return false;
    }
    vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)GpuLoadSymbol("vkGetInstanceProcAddr");
    if (!vkGetInstanceProcAddr)
    {
        return false;
    }
    vkCreateInstance = (PFN_vkCreateInstance)vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance");
    return vkCreateInstance != NULL;
}

// -------------------------------------------------------------------------------------
// buffers and commands
// -------------------------------------------------------------------------------------

static void GpuDestroyBuffer(gpubuffer_t* b)
{
    if (b->buffer != VK_NULL_HANDLE && !b->borrowed)
    {
        vkDestroyBuffer(g_device, b->buffer, NULL);
    }
    if (b->memory != VK_NULL_HANDLE && !b->borrowed)
    {
        vkFreeMemory(g_device, b->memory, NULL);
    }
    b->borrowed = false;
    b->buffer = VK_NULL_HANDLE;
    b->memory = VK_NULL_HANDLE;
    b->size = 0;
}

static bool GpuCreateBuffer(gpubuffer_t* b, const VkDeviceSize size, const VkBufferUsageFlags usage,
                            const VkMemoryPropertyFlags properties)
{
    VkBufferCreateInfo info;
    VkMemoryRequirements req;
    VkMemoryAllocateInfo alloc;
    VkMemoryAllocateFlagsInfo allocflags;
    uint32_t type;

    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(g_device, &info, NULL, &b->buffer) != VK_SUCCESS)
    {
        b->buffer = VK_NULL_HANDLE;
        return false;
    }

    vkGetBufferMemoryRequirements(g_device, b->buffer, &req);
    for (type = 0; type < g_memprops.memoryTypeCount; type++)
    {
        if (((req.memoryTypeBits >> type) & 1u) &&
            (g_memprops.memoryTypes[type].propertyFlags & properties) == properties)
        {
            break;
        }
    }
    if (type == g_memprops.memoryTypeCount)
    {
        GpuDestroyBuffer(b);
        return false;
    }

    memset(&alloc, 0, sizeof(alloc));
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = type;
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
    {
        memset(&allocflags, 0, sizeof(allocflags));
        allocflags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        allocflags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        alloc.pNext = &allocflags;
    }
    if (vkAllocateMemory(g_device, &alloc, NULL, &b->memory) != VK_SUCCESS)
    {
        b->memory = VK_NULL_HANDLE;
        GpuDestroyBuffer(b);
        return false;
    }
    if (vkBindBufferMemory(g_device, b->buffer, b->memory, 0) != VK_SUCCESS)
    {
        GpuDestroyBuffer(b);
        return false;
    }
    b->size = size;
    return true;
}

// Makes sure a buffer holds at least size bytes; its contents are lost when
// it grows. Buffers grow by half again at least, so a job whose dispatches
// get larger does not reallocate every time.
static bool GpuEnsureBuffer(gpubuffer_t* b, const VkDeviceSize size, const VkBufferUsageFlags usage,
                            const VkMemoryPropertyFlags properties)
{
    if (b->buffer != VK_NULL_HANDLE && b->size >= size)
    {
        return true;
    }
    const VkDeviceSize grown = b->size + b->size / 2;
    GpuDestroyBuffer(b);
    if (grown > size && GpuCreateBuffer(b, grown, usage, properties))
    {
        return true;
    }
    return GpuCreateBuffer(b, qmax(size, (VkDeviceSize)4), usage, properties);
}

static bool GpuEnsureStaging(const VkDeviceSize size)
{
    if (g_staging.buffer != VK_NULL_HANDLE && g_staging.size >= size)
    {
        return true;
    }
    if (g_staging_mapped)
    {
        vkUnmapMemory(g_device, g_staging.memory);
        g_staging_mapped = NULL;
    }
    // cached memory reads back faster; not every device has it
    if (!GpuEnsureBuffer(&g_staging, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                             VK_MEMORY_PROPERTY_HOST_CACHED_BIT) &&
        !GpuEnsureBuffer(&g_staging, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
    {
        return false;
    }
    if (vkMapMemory(g_device, g_staging.memory, 0, VK_WHOLE_SIZE, 0, &g_staging_mapped) != VK_SUCCESS)
    {
        g_staging_mapped = NULL;
        return false;
    }
    return true;
}

static bool GpuEnsureStorage(gpubuffer_t* b, const VkDeviceSize size)
{
    return GpuEnsureBuffer(b, size,
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                               VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
}

static bool GpuBegin()
{
    VkCommandBufferBeginInfo begin;
    memset(&begin, 0, sizeof(begin));
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    return vkBeginCommandBuffer(g_cmd, &begin) == VK_SUCCESS;
}

// Runs the recorded commands and waits for them.
static bool GpuSubmit()
{
    VkSubmitInfo submit;
    if (vkEndCommandBuffer(g_cmd) != VK_SUCCESS)
    {
        return false;
    }
    memset(&submit, 0, sizeof(submit));
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &g_cmd;
    if (vkQueueSubmit(g_queue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS)
    {
        return false;
    }
    return vkQueueWaitIdle(g_queue) == VK_SUCCESS;
}

// Copies data into a device buffer that already has the size for it.
static bool GpuFill(const gpubuffer_t* b, const void* data, const size_t size)
{
    VkBufferCopy region;
    if (size == 0)
    {
        return true;
    }
    if (!GpuEnsureStaging(size) || !GpuBegin())
    {
        return false;
    }
    memcpy(g_staging_mapped, data, size);
    memset(&region, 0, sizeof(region));
    region.size = size;
    vkCmdCopyBuffer(g_cmd, g_staging.buffer, b->buffer, 1, &region);
    return GpuSubmit();
}

// Makes the writes of one pipeline stage visible to the next.
static void GpuBarrier(const VkPipelineStageFlags srcstage, const VkAccessFlags srcaccess,
                       const VkPipelineStageFlags dststage, const VkAccessFlags dstaccess)
{
    VkMemoryBarrier barrier;
    memset(&barrier, 0, sizeof(barrier));
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = srcaccess;
    barrier.dstAccessMask = dstaccess;
    vkCmdPipelineBarrier(g_cmd, srcstage, dststage, 0, 1, &barrier, 0, NULL, 0, NULL);
}

// -------------------------------------------------------------------------------------
// setup
// -------------------------------------------------------------------------------------

static bool GpuCreateInstance()
{
    static const char* const validation = "VK_LAYER_KHRONOS_validation";
    VkApplicationInfo app;
    VkInstanceCreateInfo info;

    // Vulkan 1.2 when the loader has it: ray queries need it
    const PFN_vkEnumerateInstanceVersion enumerate =
        (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion");
    uint32_t loaderversion = VK_MAKE_API_VERSION(0, 1, 0, 0);
    if (enumerate)
    {
        enumerate(&loaderversion);
    }
    if (loaderversion < VK_MAKE_API_VERSION(0, 1, 1, 0))
    {
        return false;
    }
    g_gpu_apiversion = loaderversion >= VK_MAKE_API_VERSION(0, 1, 2, 0) ? VK_MAKE_API_VERSION(0, 1, 2, 0)
                                                                        : VK_MAKE_API_VERSION(0, 1, 1, 0);

    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "hlrad-ng";
    app.apiVersion = g_gpu_apiversion;
    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;

    // development aid: HLRAD_NG_GPU_VALIDATE=1 turns the validation layer on when it is installed
    if (getenv("HLRAD_NG_GPU_VALIDATE"))
    {
        info.enabledLayerCount = 1;
        info.ppEnabledLayerNames = &validation;
        if (vkCreateInstance(&info, NULL, &g_instance) == VK_SUCCESS)
        {
            return true;
        }
        info.enabledLayerCount = 0;
        info.ppEnabledLayerNames = NULL;
    }
    if (vkCreateInstance(&info, NULL, &g_instance) != VK_SUCCESS)
    {
        g_instance = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// Whether a device can trace rays from a compute shader.
static bool GpuDeviceHasRayQuery(const VkPhysicalDevice device, const VkPhysicalDeviceProperties* props)
{
    static const char* const needed[3] = {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME,
                                          VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME};
    VkExtensionProperties extensions[512];
    uint32_t count = 512;
    int found = 0;
    VkPhysicalDeviceFeatures2 features;
    VkPhysicalDeviceVulkan12Features features12;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accel;
    VkPhysicalDeviceRayQueryFeaturesKHR rayquery;

    // HLRAD_NG_GPU_RAYQUERY=0 keeps the BSP walk, for comparing the two
    const char* mode = getenv("HLRAD_NG_GPU_RAYQUERY");
    if ((mode && mode[0] == '0') || g_gpu_apiversion < VK_MAKE_API_VERSION(0, 1, 2, 0) ||
        props->apiVersion < VK_MAKE_API_VERSION(0, 1, 2, 0))
    {
        return false;
    }

    const VkResult result = vkEnumerateDeviceExtensionProperties(device, NULL, &count, extensions);
    if (result != VK_SUCCESS && result != VK_INCOMPLETE)
    {
        return false;
    }
    for (uint32_t i = 0; i < count; i++)
    {
        for (int k = 0; k < 3; k++)
        {
            if (!strcmp(extensions[i].extensionName, needed[k]))
            {
                found++;
            }
        }
    }
    if (found < 3)
    {
        return false;
    }

    memset(&features, 0, sizeof(features));
    memset(&features12, 0, sizeof(features12));
    memset(&accel, 0, sizeof(accel));
    memset(&rayquery, 0, sizeof(rayquery));
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &features12;
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.pNext = &accel;
    accel.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    accel.pNext = &rayquery;
    rayquery.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    vkGetPhysicalDeviceFeatures2(device, &features);
    return features12.bufferDeviceAddress && accel.accelerationStructure && rayquery.rayQuery;
}

// Picks a Vulkan 1.1 device with a compute queue: a discrete GPU when there
// is one, else an integrated one; between equals, one that traces rays in
// hardware. Software devices are not worth using.
static bool GpuPickDevice(uint32_t* queuefamily)
{
    VkPhysicalDevice devices[16];
    uint32_t count = 16;
    int bestrank = 0;
    uint32_t i;

    const VkResult result = vkEnumeratePhysicalDevices(g_instance, &count, devices);
    if (result != VK_SUCCESS && result != VK_INCOMPLETE)
    {
        return false;
    }
    for (i = 0; i < count; i++)
    {
        VkPhysicalDeviceProperties props;
        VkQueueFamilyProperties families[32];
        uint32_t familycount = 32;
        uint32_t family;
        int rank;

        vkGetPhysicalDeviceProperties(devices[i], &props);
        if (props.apiVersion < VK_MAKE_API_VERSION(0, 1, 1, 0))
        {
            continue;
        }
        switch (props.deviceType)
        {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            rank = 6;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            rank = 4;
            break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            rank = 2;
            break;
        default:
            rank = 0;
            break;
        }
        if (rank == 0)
        {
            continue;
        }
        const bool rayquery = GpuDeviceHasRayQuery(devices[i], &props);
        if (rayquery)
        {
            rank++;
        }
        if (rank <= bestrank)
        {
            continue;
        }
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &familycount, families);
        for (family = 0; family < familycount; family++)
        {
            if (families[family].queueFlags & VK_QUEUE_COMPUTE_BIT)
            {
                break;
            }
        }
        if (family == familycount)
        {
            continue;
        }
        bestrank = rank;
        g_physical = devices[i];
        g_gpu_rayquery = rayquery;
        g_gpu_discrete = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        *queuefamily = family;
        g_gpu_maxgroups = props.limits.maxComputeWorkGroupCount[0];
        g_gpu_maxbuffer = props.limits.maxStorageBufferRange;
        safe_strncpy(g_gpu_devicename, props.deviceName, sizeof(g_gpu_devicename));
    }
    return bestrank > 0;
}

static bool GpuCreateDevice(const uint32_t queuefamily)
{
    static const char* const extensions[3] = {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
                                              VK_KHR_RAY_QUERY_EXTENSION_NAME,
                                              VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME};
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue;
    VkDeviceCreateInfo info;
    VkCommandPoolCreateInfo pool;
    VkCommandBufferAllocateInfo cmd;
    VkPhysicalDeviceFeatures2 features;
    VkPhysicalDeviceVulkan12Features features12;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accel;
    VkPhysicalDeviceRayQueryFeaturesKHR rayquery;

    memset(&queue, 0, sizeof(queue));
    queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue.queueFamilyIndex = queuefamily;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queue;
    if (g_gpu_rayquery)
    {
        memset(&features, 0, sizeof(features));
        memset(&features12, 0, sizeof(features12));
        memset(&accel, 0, sizeof(accel));
        memset(&rayquery, 0, sizeof(rayquery));
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = &features12;
        features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        features12.bufferDeviceAddress = VK_TRUE;
        features12.pNext = &accel;
        accel.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        accel.accelerationStructure = VK_TRUE;
        accel.pNext = &rayquery;
        rayquery.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
        rayquery.rayQuery = VK_TRUE;
        info.pNext = &features;
        info.enabledExtensionCount = 3;
        info.ppEnabledExtensionNames = extensions;
    }
    if (vkCreateDevice(g_physical, &info, NULL, &g_device) != VK_SUCCESS)
    {
        g_device = VK_NULL_HANDLE;
        return false;
    }

#define GPU_LOAD(name)                                                                                                 \
    vk##name = (PFN_vk##name)vkGetDeviceProcAddr(g_device, "vk" #name);                                                \
    if (!vk##name)                                                                                                     \
    {                                                                                                                  \
        return false;                                                                                                  \
    }
    GPU_DEVICE_FUNCTIONS(GPU_LOAD)
    if (g_gpu_rayquery)
    {
        GPU_RAYQUERY_FUNCTIONS(GPU_LOAD)
    }
#undef GPU_LOAD

    vkGetDeviceQueue(g_device, queuefamily, 0, &g_queue);
    vkGetPhysicalDeviceMemoryProperties(g_physical, &g_memprops);

    memset(&pool, 0, sizeof(pool));
    pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = queuefamily;
    if (vkCreateCommandPool(g_device, &pool, NULL, &g_cmdpool) != VK_SUCCESS)
    {
        g_cmdpool = VK_NULL_HANDLE;
        return false;
    }
    memset(&cmd, 0, sizeof(cmd));
    cmd.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd.commandPool = g_cmdpool;
    cmd.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd.commandBufferCount = 1;
    return vkAllocateCommandBuffers(g_device, &cmd, &g_cmd) == VK_SUCCESS;
}

bool GpuInit()
{
    uint32_t queuefamily = 0;

    if (g_gpu_tried)
    {
        return g_gpu_ready;
    }
    g_gpu_tried = true;

    if (!GpuOpenLibrary() || !GpuCreateInstance())
    {
        return false;
    }
#define GPU_LOAD(name)                                                                                                 \
    vk##name = (PFN_vk##name)vkGetInstanceProcAddr(g_instance, "vk" #name);                                            \
    if (!vk##name)                                                                                                     \
    {                                                                                                                  \
        return false;                                                                                                  \
    }
    GPU_INSTANCE_FUNCTIONS(GPU_LOAD)
#undef GPU_LOAD

    if (!GpuPickDevice(&queuefamily) || !GpuCreateDevice(queuefamily))
    {
        GpuShutdown();
        g_gpu_tried = true;
        return false;
    }
    g_gpu_ready = true;
    return true;
}

static void GpuDestroyJob(gpujob_t* job)
{
    for (int i = 0; i <= GPU_MAX_BUFFERS; i++)
    {
        GpuDestroyBuffer(&job->buffers[i]);
    }
    if (job->pool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(g_device, job->pool, NULL);
    }
    if (job->pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(g_device, job->pipeline, NULL);
    }
    if (job->layout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(g_device, job->layout, NULL);
    }
    if (job->setlayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(g_device, job->setlayout, NULL);
    }
    if (job->shader != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(g_device, job->shader, NULL);
    }
    free(job);
}

void GpuShutdown()
{
    if (g_device != VK_NULL_HANDLE && vkDestroyDevice)
    {
        vkQueueWaitIdle(g_queue);
        while (g_jobs)
        {
            gpujob_t* job = g_jobs;
            g_jobs = job->next;
            GpuDestroyJob(job);
        }
        if (g_tlas != VK_NULL_HANDLE)
        {
            vkDestroyAccelerationStructureKHR(g_device, g_tlas, NULL);
        }
        if (g_blas != VK_NULL_HANDLE)
        {
            vkDestroyAccelerationStructureKHR(g_device, g_blas, NULL);
        }
        if (g_staging_mapped)
        {
            vkUnmapMemory(g_device, g_staging.memory);
        }
        GpuDestroyBuffer(&g_staging);
        GpuDestroyBuffer(&g_nodes);
        GpuDestroyBuffer(&g_vertices);
        GpuDestroyBuffer(&g_blasbuffer);
        GpuDestroyBuffer(&g_tlasbuffer);
        GpuDestroyBuffer(&g_instances);
        if (g_cmdpool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(g_device, g_cmdpool, NULL);
        }
        vkDestroyDevice(g_device, NULL);
    }
    if (g_instance != VK_NULL_HANDLE && vkDestroyInstance)
    {
        vkDestroyInstance(g_instance, NULL);
    }
    g_device = VK_NULL_HANDLE;
    g_instance = VK_NULL_HANDLE;
    g_cmdpool = VK_NULL_HANDLE;
    g_tlas = VK_NULL_HANDLE;
    g_blas = VK_NULL_HANDLE;
    g_staging_mapped = NULL;
    g_jobs = NULL;
    g_gpu_ready = false;
    g_gpu_scene = false;
}

const char* GpuDeviceName()
{
    return g_gpu_ready ? g_gpu_devicename : "";
}

bool GpuHasRayQuery()
{
    return g_gpu_ready && g_gpu_rayquery;
}

bool GpuIsDiscrete()
{
    return g_gpu_ready && g_gpu_discrete;
}

unsigned GpuMaxGroups()
{
    return g_gpu_ready ? g_gpu_maxgroups : 0;
}

unsigned long long GpuDeviceMemory()
{
    unsigned long long largest = 0;

    if (!g_gpu_ready)
    {
        return 0;
    }
    for (uint32_t i = 0; i < g_memprops.memoryHeapCount; i++)
    {
        if ((g_memprops.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) &&
            g_memprops.memoryHeaps[i].size > largest)
        {
            largest = g_memprops.memoryHeaps[i].size;
        }
    }
    return largest;
}

unsigned long long GpuMaxBufferBytes()
{
    return g_gpu_ready ? g_gpu_maxbuffer : 0;
}

// -------------------------------------------------------------------------------------
// scene
// -------------------------------------------------------------------------------------

bool GpuSetSceneNodes(const gpunode_t* nodes, const int numnodes)
{
    const size_t size = (size_t)qmax(numnodes, 0) * sizeof(gpunode_t);

    if (!g_gpu_ready || g_gpu_rayquery || numnodes <= 0)
    {
        return false;
    }
    if (!GpuEnsureStorage(&g_nodes, size) || !GpuFill(&g_nodes, nodes, size))
    {
        return false;
    }
    g_gpu_scene = true;
    return true;
}

// Builds one acceleration structure on the device and waits for it.
static bool GpuBuildAccelerationStructure(const VkAccelerationStructureTypeKHR type,
                                          const VkAccelerationStructureGeometryKHR* geometry, const uint32_t primitives,
                                          gpubuffer_t* storage, VkAccelerationStructureKHR* structure)
{
    VkAccelerationStructureBuildGeometryInfoKHR build;
    VkAccelerationStructureBuildSizesInfoKHR sizes;
    VkAccelerationStructureCreateInfoKHR create;
    VkAccelerationStructureBuildRangeInfoKHR range;
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
    VkBufferDeviceAddressInfo addressinfo;
    gpubuffer_t scratch;

    memset(&build, 0, sizeof(build));
    build.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build.type = type;
    build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.geometryCount = 1;
    build.pGeometries = geometry;

    memset(&sizes, 0, sizeof(sizes));
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(g_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build,
                                            &primitives, &sizes);

    if (!GpuCreateBuffer(storage, sizes.accelerationStructureSize,
                         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
    {
        return false;
    }
    memset(&create, 0, sizeof(create));
    create.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    create.buffer = storage->buffer;
    create.size = sizes.accelerationStructureSize;
    create.type = type;
    if (vkCreateAccelerationStructureKHR(g_device, &create, NULL, structure) != VK_SUCCESS)
    {
        *structure = VK_NULL_HANDLE;
        return false;
    }

    memset(&scratch, 0, sizeof(scratch));
    if (!GpuCreateBuffer(&scratch, sizes.buildScratchSize + GPU_SCRATCH_ALIGNMENT,
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
    {
        return false;
    }
    memset(&addressinfo, 0, sizeof(addressinfo));
    addressinfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressinfo.buffer = scratch.buffer;
    const VkDeviceAddress scratchaddress = vkGetBufferDeviceAddress(g_device, &addressinfo);

    build.dstAccelerationStructure = *structure;
    build.scratchData.deviceAddress =
        (scratchaddress + GPU_SCRATCH_ALIGNMENT - 1) & ~(VkDeviceAddress)(GPU_SCRATCH_ALIGNMENT - 1);
    memset(&range, 0, sizeof(range));
    range.primitiveCount = primitives;

    bool ok = GpuBegin();
    if (ok)
    {
        vkCmdBuildAccelerationStructuresKHR(g_cmd, 1, &build, &ranges);
        ok = GpuSubmit();
    }
    GpuDestroyBuffer(&scratch);
    return ok;
}

bool GpuSetSceneTriangles(const float* triangles, const int numtriangles)
{
    const size_t vertexsize = (size_t)qmax(numtriangles, 0) * 9 * sizeof(float);
    VkAccelerationStructureGeometryKHR geometry;
    VkAccelerationStructureInstanceKHR instance;
    VkAccelerationStructureDeviceAddressInfoKHR blasinfo;
    VkBufferDeviceAddressInfo addressinfo;

    if (!g_gpu_ready || !g_gpu_rayquery || numtriangles <= 0 || g_blas != VK_NULL_HANDLE)
    {
        return false;
    }

    // the triangles
    if (!GpuCreateBuffer(&g_vertices, vertexsize,
                         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ||
        !GpuFill(&g_vertices, triangles, vertexsize))
    {
        return false;
    }
    memset(&addressinfo, 0, sizeof(addressinfo));
    addressinfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressinfo.buffer = g_vertices.buffer;
    memset(&geometry, 0, sizeof(geometry));
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    geometry.geometry.triangles.vertexData.deviceAddress = vkGetBufferDeviceAddress(g_device, &addressinfo);
    geometry.geometry.triangles.vertexStride = 3 * sizeof(float);
    geometry.geometry.triangles.maxVertex = (uint32_t)numtriangles * 3 - 1;
    geometry.geometry.triangles.indexType = VK_INDEX_TYPE_NONE_KHR;
    if (!GpuBuildAccelerationStructure(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, &geometry,
                                       (uint32_t)numtriangles, &g_blasbuffer, &g_blas))
    {
        return false;
    }

    // one instance of them, where they are
    memset(&blasinfo, 0, sizeof(blasinfo));
    blasinfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    blasinfo.accelerationStructure = g_blas;
    memset(&instance, 0, sizeof(instance));
    instance.transform.matrix[0][0] = 1.0f;
    instance.transform.matrix[1][1] = 1.0f;
    instance.transform.matrix[2][2] = 1.0f;
    instance.mask = 0xFF;
    instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    instance.accelerationStructureReference = vkGetAccelerationStructureDeviceAddressKHR(g_device, &blasinfo);
    if (!GpuCreateBuffer(&g_instances, sizeof(instance),
                         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ||
        !GpuFill(&g_instances, &instance, sizeof(instance)))
    {
        return false;
    }
    addressinfo.buffer = g_instances.buffer;
    memset(&geometry, 0, sizeof(geometry));
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.data.deviceAddress = vkGetBufferDeviceAddress(g_device, &addressinfo);
    if (!GpuBuildAccelerationStructure(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, &geometry, 1, &g_tlasbuffer,
                                       &g_tlas))
    {
        return false;
    }
    g_gpu_scene = true;
    return true;
}

// -------------------------------------------------------------------------------------
// jobs
// -------------------------------------------------------------------------------------

gpujob_t* GpuCreateJob(const uint32_t* rayquery, const size_t rayquerysize, const uint32_t* bspwalk,
                       const size_t bspwalksize, const int numbuffers, const size_t paramsize)
{
    VkShaderModuleCreateInfo shader;
    VkDescriptorSetLayoutBinding bindings[GPU_MAX_BUFFERS + 1];
    VkDescriptorSetLayoutCreateInfo setlayout;
    VkPushConstantRange range;
    VkPipelineLayoutCreateInfo layout;
    VkComputePipelineCreateInfo pipeline;
    VkDescriptorPoolSize poolsizes[2];
    VkDescriptorPoolCreateInfo pool;
    VkDescriptorSetAllocateInfo set;
    int i;

    if (!g_gpu_ready || !g_gpu_scene || numbuffers < 1 || numbuffers > GPU_MAX_BUFFERS)
    {
        return NULL;
    }
    gpujob_t* job = (gpujob_t*)calloc(1, sizeof(gpujob_t));
    hlassume(job != NULL, assume_NoMemory);
    job->numbuffers = numbuffers;
    job->paramsize = paramsize;
    // owned by the backend from here on: GpuShutdown frees it, complete or not
    job->next = g_jobs;
    g_jobs = job;

    memset(&shader, 0, sizeof(shader));
    shader.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader.codeSize = g_gpu_rayquery ? rayquerysize : bspwalksize;
    shader.pCode = g_gpu_rayquery ? rayquery : bspwalk;
    if (vkCreateShaderModule(g_device, &shader, NULL, &job->shader) != VK_SUCCESS)
    {
        job->shader = VK_NULL_HANDLE;
        return NULL;
    }

    memset(bindings, 0, sizeof(bindings));
    for (i = 0; i <= numbuffers; i++)
    {
        bindings[i].binding = (uint32_t)i;
        bindings[i].descriptorType = (i == 0 && g_gpu_rayquery) ? VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR
                                                                : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    memset(&setlayout, 0, sizeof(setlayout));
    setlayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setlayout.bindingCount = (uint32_t)numbuffers + 1;
    setlayout.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(g_device, &setlayout, NULL, &job->setlayout) != VK_SUCCESS)
    {
        job->setlayout = VK_NULL_HANDLE;
        return NULL;
    }

    memset(&range, 0, sizeof(range));
    range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    range.size = (uint32_t)paramsize;
    memset(&layout, 0, sizeof(layout));
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &job->setlayout;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &range;
    if (vkCreatePipelineLayout(g_device, &layout, NULL, &job->layout) != VK_SUCCESS)
    {
        job->layout = VK_NULL_HANDLE;
        return NULL;
    }

    memset(&pipeline, 0, sizeof(pipeline));
    pipeline.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline.stage.module = job->shader;
    pipeline.stage.pName = "main";
    pipeline.layout = job->layout;
    if (vkCreateComputePipelines(g_device, VK_NULL_HANDLE, 1, &pipeline, NULL, &job->pipeline) != VK_SUCCESS)
    {
        job->pipeline = VK_NULL_HANDLE;
        return NULL;
    }

    memset(poolsizes, 0, sizeof(poolsizes));
    poolsizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolsizes[0].descriptorCount = (uint32_t)numbuffers + 1;
    poolsizes[1].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    poolsizes[1].descriptorCount = 1;
    memset(&pool, 0, sizeof(pool));
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = 1;
    pool.poolSizeCount = g_gpu_rayquery ? 2 : 1;
    pool.pPoolSizes = poolsizes;
    if (vkCreateDescriptorPool(g_device, &pool, NULL, &job->pool) != VK_SUCCESS)
    {
        job->pool = VK_NULL_HANDLE;
        return NULL;
    }
    memset(&set, 0, sizeof(set));
    set.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set.descriptorPool = job->pool;
    set.descriptorSetCount = 1;
    set.pSetLayouts = &job->setlayout;
    if (vkAllocateDescriptorSets(g_device, &set, &job->set) != VK_SUCCESS)
    {
        return NULL;
    }
    return job;
}

bool GpuJobUpload(gpujob_t* job, const int binding, const void* data, const size_t size)
{
    if (!job || binding < 1 || binding > job->numbuffers)
    {
        return false;
    }
    return GpuEnsureStorage(&job->buffers[binding], size) && GpuFill(&job->buffers[binding], data, size);
}

bool GpuJobReserve(gpujob_t* job, const int binding, const size_t size, const bool zero)
{
    if (!job || binding < 1 || binding > job->numbuffers || size == 0 || size > g_gpu_maxbuffer)
    {
        return false;
    }
    gpubuffer_t* b = &job->buffers[binding];
    if (b->borrowed || !GpuEnsureStorage(b, size))
    {
        return false;
    }
    if (!zero)
    {
        return true;
    }
    if (!GpuBegin())
    {
        return false;
    }
    vkCmdFillBuffer(g_cmd, b->buffer, 0, VK_WHOLE_SIZE, 0);
    return GpuSubmit();
}

bool GpuJobShare(gpujob_t* job, const int binding, gpujob_t* from, const int frombinding)
{
    if (!job || !from || binding < 1 || binding > job->numbuffers || frombinding < 1 ||
        frombinding > from->numbuffers || from->buffers[frombinding].buffer == VK_NULL_HANDLE)
    {
        return false;
    }
    GpuDestroyBuffer(&job->buffers[binding]);
    job->buffers[binding] = from->buffers[frombinding];
    job->buffers[binding].borrowed = true;
    return true;
}

// Points the descriptor set of a job at the scene and at its buffers.
static bool GpuBindJob(gpujob_t* job)
{
    VkDescriptorBufferInfo infos[GPU_MAX_BUFFERS + 1];
    VkWriteDescriptorSet writes[GPU_MAX_BUFFERS + 1];
    VkWriteDescriptorSetAccelerationStructureKHR scene;
    int i;

    memset(infos, 0, sizeof(infos));
    memset(writes, 0, sizeof(writes));
    memset(&scene, 0, sizeof(scene));
    for (i = 0; i <= job->numbuffers; i++)
    {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = job->set;
        writes[i].dstBinding = (uint32_t)i;
        writes[i].descriptorCount = 1;
        if (i == 0 && g_gpu_rayquery)
        {
            scene.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
            scene.accelerationStructureCount = 1;
            scene.pAccelerationStructures = &g_tlas;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            writes[i].pNext = &scene;
            continue;
        }
        infos[i].buffer = i == 0 ? g_nodes.buffer : job->buffers[i].buffer;
        if (infos[i].buffer == VK_NULL_HANDLE)
        {
            return false; // a binding nobody filled
        }
        infos[i].range = VK_WHOLE_SIZE;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &infos[i];
    }
    vkUpdateDescriptorSets(g_device, (uint32_t)job->numbuffers + 1, writes, 0, NULL);
    return true;
}

bool GpuJobRun(gpujob_t* job, const void* params, const unsigned groups, const gpuinput_t* inputs, const int numinputs,
               const gpuoutput_t* outputs, const int numoutputs)
{
    VkBufferCopy region;
    size_t total = 0;
    size_t offset = 0;
    int i;

    if (!job || !g_gpu_ready || groups == 0 || groups > g_gpu_maxgroups)
    {
        return false;
    }

    // the staging buffer carries the inputs in and the outputs out
    for (i = 0; i < numinputs; i++)
    {
        if (inputs[i].binding < 1 || inputs[i].binding > job->numbuffers || job->buffers[inputs[i].binding].borrowed ||
            !GpuEnsureStorage(&job->buffers[inputs[i].binding], inputs[i].size))
        {
            return false;
        }
        total += inputs[i].size;
    }
    for (i = 0; i < numoutputs; i++)
    {
        if (outputs[i].binding < 1 || outputs[i].binding > job->numbuffers || outputs[i].clear > outputs[i].size ||
            !GpuEnsureStorage(&job->buffers[outputs[i].binding], outputs[i].size))
        {
            return false;
        }
        total += outputs[i].size;
    }
    if (!GpuEnsureStaging(qmax(total, (size_t)4)) || !GpuBindJob(job) || !GpuBegin())
    {
        return false;
    }

    memset(&region, 0, sizeof(region));
    for (i = 0; i < numinputs; i++)
    {
        if (inputs[i].size == 0)
        {
            continue;
        }
        memcpy((byte*)g_staging_mapped + offset, inputs[i].data, inputs[i].size);
        region.srcOffset = offset;
        region.size = inputs[i].size;
        vkCmdCopyBuffer(g_cmd, g_staging.buffer, job->buffers[inputs[i].binding].buffer, 1, &region);
        offset += inputs[i].size;
    }
    for (i = 0; i < numoutputs; i++)
    {
        if (outputs[i].clear > 0)
        {
            vkCmdFillBuffer(g_cmd, job->buffers[outputs[i].binding].buffer, 0, outputs[i].clear, 0);
        }
    }
    GpuBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    vkCmdBindPipeline(g_cmd, VK_PIPELINE_BIND_POINT_COMPUTE, job->pipeline);
    vkCmdBindDescriptorSets(g_cmd, VK_PIPELINE_BIND_POINT_COMPUTE, job->layout, 0, 1, &job->set, 0, NULL);
    vkCmdPushConstants(g_cmd, job->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, (uint32_t)job->paramsize, params);
    vkCmdDispatch(g_cmd, groups, 1, 1);
    GpuBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
               VK_ACCESS_TRANSFER_READ_BIT);
    region.srcOffset = 0;
    const size_t outbase = offset;
    for (i = 0; i < numoutputs; i++)
    {
        if (outputs[i].size == 0)
        {
            continue;
        }
        region.dstOffset = offset;
        region.size = outputs[i].size;
        vkCmdCopyBuffer(g_cmd, job->buffers[outputs[i].binding].buffer, g_staging.buffer, 1, &region);
        offset += outputs[i].size;
    }
    if (!GpuSubmit())
    {
        return false;
    }

    offset = outbase;
    for (i = 0; i < numoutputs; i++)
    {
        if (outputs[i].size == 0)
        {
            continue;
        }
        memcpy(outputs[i].data, (const byte*)g_staging_mapped + offset, outputs[i].size);
        offset += outputs[i].size;
    }
    return true;
}
