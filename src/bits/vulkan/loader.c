#include "vk.h"

#include <screw/bridge.h>
#include <screw/codegen.h>
#include <screw/log.h>

#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_BUCKETS 512

extern const struct bit bit_vulkan;

struct stub {
    void *host;
    void *entry;
    struct stub *next;
};

static void *lib;
static host_gipa gipa;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static struct stub *cache[CACHE_BUCKETS];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static VkDevice last_device;

static void load(void)
{
    const char *name = getenv("SCREW_VULKAN") ? getenv("SCREW_VULKAN") : "libvulkan.so.1";
    lib = dlopen(name, RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        warn("Vulkan is not available (%s); install libvulkan1 and a GPU driver, see screw doctor", dlerror());
        return;
    }
    gipa = (host_gipa)dlsym(lib, "vkGetInstanceProcAddr");
    trace("vulkan: host loader %s", name);
}

bool vk_loaded(void)
{
    pthread_once(&once, load);
    return gipa != NULL;
}

void *vk_host_global(const char *name)
{
    if (!vk_loaded())
        return NULL;
    void *p = dlsym(lib, name);
    return p ? p : (void *)gipa(NULL, name);
}

void *vk_host_instance(VkInstance inst, const char *name)
{
    return vk_loaded() ? (void *)gipa(inst, name) : NULL;
}

void *vk_host_device(VkDevice dev, const char *name)
{
    if (!vk_loaded())
        return NULL;
    host_gdpa gdpa = (host_gdpa)dlsym(lib, "vkGetDeviceProcAddr");
    void *p = gdpa && dev ? (void *)gdpa(dev, name) : NULL;
    return p ? p : vk_host_global(name);
}

void vk_track_device(VkDevice dev) { __atomic_store_n(&last_device, dev, __ATOMIC_RELEASE); }
VkDevice vk_last_device(void) { return __atomic_load_n(&last_device, __ATOMIC_ACQUIRE); }

static bool starts(const char *s, const char *prefix) { return !strncmp(s, prefix, strlen(prefix)); }

static bool ends(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && !strcmp(s + a - b, suffix);
}

static int allocator_index(const char *name)
{
    static const struct {
        const char *name;
        int index;
    } special[] = {
        { "vkCreateInstance", 1 },
        { "vkDestroyInstance", 1 },
        { "vkDestroyDevice", 1 },
        { "vkCreateSharedSwapchainsKHR", 3 },
        { "vkCreateDisplayModeKHR", 3 },
        { "vkRegisterDisplayEventEXT", 3 },
        { "vkRegisterDeviceEventEXT", 2 },
        { "vkAllocateMemory", 2 },
        { "vkFreeMemory", 2 },
        { "vkCreateRayTracingPipelinesKHR", 5 },
        { "vkCreateShadersEXT", 3 },
    };
    for (size_t i = 0; i < sizeof special / sizeof special[0]; i++)
        if (!strcmp(name, special[i].name))
            return special[i].index;
    if (starts(name, "vkCreate") && ends(name, "Pipelines"))
        return 4;
    if (starts(name, "vkCreate") || starts(name, "vkDestroy"))
        return 2;
    return -1;
}

static void *override(const char *name)
{
    for (const struct bit_table *const *t = bit_vulkan.tables; *t; t++)
        for (size_t i = 0; i < (*t)->count; i++)
            if (!strcmp((*t)->exports[i].name, name))
                return (*t)->exports[i].addr;
    return NULL;
}

void *vk_wrap(const char *name, void *host)
{
    if (!host)
        return NULL;
    void *entry = bridge_to_host(host);
    int index = allocator_index(name);
    if (index < 0)
        return entry;

    size_t b = ((uintptr_t)host >> 4) % CACHE_BUCKETS;
    pthread_mutex_lock(&lock);
    struct stub *s = cache[b];
    while (s && s->host != host)
        s = s->next;
    if (!s) {
        s = malloc(sizeof *s);
        s->host = host;
        s->entry = code_null_arg(index, entry);
        s->next = cache[b];
        cache[b] = s;
    }
    pthread_mutex_unlock(&lock);
    return s->entry;
}

static void *resolve(const char *name)
{
    if (strncmp(name, "vk", 2))
        return NULL;
    void *o = override(name);
    return o ? o : vk_wrap(name, vk_host_global(name));
}

static PFN_vkVoidFunction WINAPI vkGetInstanceProcAddr(VkInstance inst, const char *name)
{
    if (!name)
        return NULL;
    void *o = override(name);
    if (o)
        return (PFN_vkVoidFunction)o;
    void *h = vk_host_instance(inst, name);
    if (!h)
        trace("vulkan: instance proc %s is NULL", name);
    return (PFN_vkVoidFunction)vk_wrap(name, h);
}

static PFN_vkVoidFunction WINAPI vkGetDeviceProcAddr(VkDevice dev, const char *name)
{
    if (!name)
        return NULL;
    void *o = override(name);
    if (o)
        return (PFN_vkVoidFunction)o;
    if (!vk_loaded())
        return NULL;
    host_gdpa gdpa = (host_gdpa)dlsym(lib, "vkGetDeviceProcAddr");
    void *h = gdpa ? (void *)gdpa(dev, name) : NULL;
    if (!h)
        trace("vulkan: device proc %s is NULL", name);
    return (PFN_vkVoidFunction)vk_wrap(name, h);
}

BIT_TABLE(vk_loader, BIT_FN(vkGetInstanceProcAddr), BIT_FN(vkGetDeviceProcAddr));

BIT_DEFINE_RESOLVED(bit_vulkan, "vulkan-1.dll", resolve,
                    &vk_loader, &vk_instance, &vk_surface, &vk_debug, &vk_floats);
