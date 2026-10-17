#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

typedef int32_t VkResult;
typedef void *VkInstance, *VkPhysicalDevice, *VkDevice, *VkQueue, *VkCommandBuffer;
typedef uint64_t VkCommandPool, VkFence, VkDebugUtilsMessengerEXT;
typedef void (*PFN)(void);
typedef PFN (*PFN_gipa)(VkInstance, const char *);

typedef struct { char name[256]; uint32_t spec; } VkExtensionProperties;

typedef struct {
    int32_t type; const void *next; const char *app; uint32_t app_ver; const char *engine; uint32_t engine_ver;
    uint32_t api;
} VkApplicationInfo;

typedef struct {
    int32_t type; const void *next; uint32_t flags; const VkApplicationInfo *app; uint32_t layers;
    const char *const *layer_names; uint32_t exts; const char *const *ext_names;
} VkInstanceCreateInfo;

typedef struct {
    int32_t type; const void *next; uint32_t flags; uint32_t severity; uint32_t types; void *callback; void *user;
} VkDebugUtilsMessengerCreateInfoEXT;

typedef struct { uint32_t flags; uint32_t count; uint32_t timestamp_bits; uint32_t granularity[3]; } VkQueueFamilyProperties;

typedef struct {
    int32_t type; const void *next; uint32_t flags; uint32_t family; uint32_t count; const float *priorities;
} VkDeviceQueueCreateInfo;

typedef struct {
    int32_t type; const void *next; uint32_t flags; uint32_t queue_infos; const VkDeviceQueueCreateInfo *queues;
    uint32_t layers; const char *const *layer_names; uint32_t exts; const char *const *ext_names;
    const void *features;
} VkDeviceCreateInfo;

typedef struct { int32_t type; const void *next; uint32_t flags; uint32_t family; } VkCommandPoolCreateInfo;
typedef struct { int32_t type; const void *next; VkCommandPool pool; uint32_t level; uint32_t count; } VkCommandBufferAllocateInfo;
typedef struct { int32_t type; const void *next; uint32_t flags; const void *inherit; } VkCommandBufferBeginInfo;
typedef struct { int32_t type; const void *next; uint32_t flags; } VkFenceCreateInfo;

typedef struct {
    int32_t type; const void *next; uint32_t waits; const void *wait_sems; const uint32_t *wait_stages;
    uint32_t cmds; const VkCommandBuffer *cmd_bufs; uint32_t signals; const void *signal_sems;
} VkSubmitInfo;

typedef struct {
    void *user; void *alloc; void *realloc; void *free; void *internal_alloc; void *internal_free;
} VkAllocationCallbacks;

static int allocator_calls;
static int callback_calls;

static void *__attribute__((ms_abi)) fake_alloc(void *u, size_t s, size_t a, int scope)
{
    allocator_calls++;
    return NULL;
}

static uint32_t __attribute__((ms_abi)) on_debug(uint32_t severity, uint32_t types, const void *data, void *user)
{
    if (user == (void *)0x5C2E)
        callback_calls++;
    return 0;
}

#define LOAD(name) name##_fn name = (name##_fn)gipa(inst, #name)
typedef VkResult (*vkEnumerateInstanceExtensionProperties_fn)(const char *, uint32_t *, VkExtensionProperties *);
typedef VkResult (*vkCreateInstance_fn)(const VkInstanceCreateInfo *, const void *, VkInstance *);
typedef VkResult (*vkEnumeratePhysicalDevices_fn)(VkInstance, uint32_t *, VkPhysicalDevice *);
typedef void (*vkGetPhysicalDeviceProperties_fn)(VkPhysicalDevice, uint8_t *);
typedef void (*vkGetPhysicalDeviceQueueFamilyProperties_fn)(VkPhysicalDevice, uint32_t *, VkQueueFamilyProperties *);
typedef VkResult (*vkCreateDevice_fn)(VkPhysicalDevice, const VkDeviceCreateInfo *, const void *, VkDevice *);
typedef PFN (*vkGetDeviceProcAddr_fn)(VkDevice, const char *);
typedef void (*vkGetDeviceQueue_fn)(VkDevice, uint32_t, uint32_t, VkQueue *);
typedef VkResult (*vkCreateCommandPool_fn)(VkDevice, const VkCommandPoolCreateInfo *, const void *, VkCommandPool *);
typedef VkResult (*vkAllocateCommandBuffers_fn)(VkDevice, const VkCommandBufferAllocateInfo *, VkCommandBuffer *);
typedef VkResult (*vkBeginCommandBuffer_fn)(VkCommandBuffer, const VkCommandBufferBeginInfo *);
typedef void (*vkCmdSetLineWidth_fn)(VkCommandBuffer, float);
typedef VkResult (*vkEndCommandBuffer_fn)(VkCommandBuffer);
typedef VkResult (*vkCreateFence_fn)(VkDevice, const VkFenceCreateInfo *, const void *, VkFence *);
typedef VkResult (*vkQueueSubmit_fn)(VkQueue, uint32_t, const VkSubmitInfo *, VkFence);
typedef VkResult (*vkWaitForFences_fn)(VkDevice, uint32_t, const VkFence *, uint32_t, uint64_t);
typedef void (*vkDestroyFence_fn)(VkDevice, VkFence, const void *);
typedef void (*vkDestroyCommandPool_fn)(VkDevice, VkCommandPool, const void *);
typedef void (*vkDestroyDevice_fn)(VkDevice, const void *);
typedef void (*vkDestroyInstance_fn)(VkInstance, const void *);

int main(void)
{
    HMODULE lib = LoadLibraryA("vulkan-1.dll");
    PFN_gipa gipa = lib ? (PFN_gipa)GetProcAddress(lib, "vkGetInstanceProcAddr") : NULL;
    printf("loader: %s\n", gipa ? "ok" : "missing");
    if (!gipa)
        return 1;

    VkInstance inst = NULL;
    LOAD(vkEnumerateInstanceExtensionProperties);
    LOAD(vkCreateInstance);
    uint32_t n = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &n, NULL);
    VkExtensionProperties exts[256];
    if (n > 256)
        n = 256;
    vkEnumerateInstanceExtensionProperties(NULL, &n, exts);
    int win32 = 0, xlib = 0;
    for (uint32_t i = 0; i < n; i++) {
        win32 |= !strcmp(exts[i].name, "VK_KHR_win32_surface");
        xlib |= !strcmp(exts[i].name, "VK_KHR_xlib_surface");
    }
    printf("win32 surface advertised: %s, xlib hidden: %s\n", win32 ? "yes" : "no", xlib ? "no" : "yes");

    const char *want[] = { "VK_KHR_surface", "VK_KHR_win32_surface", "VK_EXT_debug_utils" };
    VkApplicationInfo app = { 0, NULL, "screw-test", 1, "screw", 1, (1u << 22) | (1u << 12) };
    VkDebugUtilsMessengerCreateInfoEXT dbg = { 1000128004, NULL, 0, 0x1111, 0x7, (void *)on_debug, (void *)0x5C2E };
    VkInstanceCreateInfo ici = { 1, &dbg, 0, &app, 0, NULL, 3, want };
    VkResult r = vkCreateInstance(&ici, NULL, &inst);
    printf("instance: %s\n", r == 0 ? "ok" : "failed");
    if (r)
        return 1;

    LOAD(vkEnumeratePhysicalDevices);
    LOAD(vkGetPhysicalDeviceProperties);
    LOAD(vkGetPhysicalDeviceQueueFamilyProperties);
    LOAD(vkCreateDevice);
    LOAD(vkGetDeviceProcAddr);
    LOAD(vkDestroyInstance);
    VkPhysicalDevice devs[8];
    n = 8;
    vkEnumeratePhysicalDevices(inst, &n, devs);
    printf("devices: %s\n", n ? "yes" : "none");
    if (!n)
        return 1;
    VkPhysicalDevice phys = devs[0];
    static uint8_t props[4096];
    for (uint32_t i = 0; i < n; i++) {
        vkGetPhysicalDeviceProperties(devs[i], props);
        if (*(uint32_t *)(props + 16) != 4) {
            phys = devs[i];
            break;
        }
    }

    VkQueueFamilyProperties families[16];
    uint32_t fc = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &fc, families);
    uint32_t family = 0;
    for (uint32_t i = 0; i < fc; i++)
        if (families[i].flags & 1) {
            family = i;
            break;
        }

    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci = { 2, NULL, 0, family, 1, &priority };
    VkDeviceCreateInfo dci = { 3, NULL, 0, 1, &qci, 0, NULL, 0, NULL, NULL };
    VkDevice dev = NULL;
    r = vkCreateDevice(phys, &dci, NULL, &dev);
    printf("device: %s\n", r == 0 ? "ok" : "failed");
    if (r)
        return 1;

#define DLOAD(name) name##_fn name = (name##_fn)vkGetDeviceProcAddr(dev, #name)
    DLOAD(vkGetDeviceQueue);
    DLOAD(vkCreateCommandPool);
    DLOAD(vkAllocateCommandBuffers);
    DLOAD(vkBeginCommandBuffer);
    DLOAD(vkCmdSetLineWidth);
    DLOAD(vkEndCommandBuffer);
    DLOAD(vkCreateFence);
    DLOAD(vkQueueSubmit);
    DLOAD(vkWaitForFences);
    DLOAD(vkDestroyFence);
    DLOAD(vkDestroyCommandPool);
    DLOAD(vkDestroyDevice);

    VkQueue queue;
    vkGetDeviceQueue(dev, family, 0, &queue);
    VkAllocationCallbacks alloc = { NULL, (void *)fake_alloc, (void *)fake_alloc, (void *)fake_alloc, NULL, NULL };
    VkCommandPoolCreateInfo pci = { 39, NULL, 0, family };
    VkCommandPool pool;
    r = vkCreateCommandPool(dev, &pci, &alloc, &pool);
    VkCommandBufferAllocateInfo cai = { 40, NULL, pool, 0, 1 };
    VkCommandBuffer cb;
    r |= vkAllocateCommandBuffers(dev, &cai, &cb);
    VkCommandBufferBeginInfo bi = { 42, NULL, 1, NULL };
    r |= vkBeginCommandBuffer(cb, &bi);
    vkCmdSetLineWidth(cb, 1.0f);
    r |= vkEndCommandBuffer(cb);
    VkFenceCreateInfo fci = { 8, NULL, 0 };
    VkFence fence;
    r |= vkCreateFence(dev, &fci, NULL, &fence);
    VkSubmitInfo si = { 4, NULL, 0, NULL, NULL, 1, &cb, 0, NULL };
    r |= vkQueueSubmit(queue, 1, &si, fence);
    r |= vkWaitForFences(dev, 1, &fence, 1, UINT64_MAX);
    printf("submit and wait on GPU: %s\n", r == 0 ? "ok" : "failed");

    vkDestroyFence(dev, fence, NULL);
    vkDestroyCommandPool(dev, pool, &alloc);
    vkDestroyDevice(dev, NULL);
    vkDestroyInstance(inst, NULL);
    printf("windows allocator left alone: %s\n", allocator_calls ? "no" : "yes");
    printf("debug callback reached Windows code: %s\n", callback_calls ? "yes" : "no");
    return r != 0;
}
