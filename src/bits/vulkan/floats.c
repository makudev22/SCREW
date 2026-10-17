#include "vk.h"

typedef void (*f1_fn)(VkCommandBuffer, float);
typedef void (*f2_fn)(VkCommandBuffer, float, float);
typedef void (*f3_fn)(VkCommandBuffer, float, float, float);

static void *host(void **slot, const char *name, bool device)
{
    void *p = __atomic_load_n(slot, __ATOMIC_ACQUIRE);
    if (!p) {
        p = device ? vk_host_device(vk_last_device(), name) : vk_host_global(name);
        __atomic_store_n(slot, p, __ATOMIC_RELEASE);
    }
    return p;
}

static void WINAPI vkCmdSetLineWidth(VkCommandBuffer cb, float width)
{
    static void *fn;
    ((f1_fn)host(&fn, "vkCmdSetLineWidth", false))(cb, width);
}

static void WINAPI vkCmdSetDepthBias(VkCommandBuffer cb, float constant, float clamp, float slope)
{
    static void *fn;
    ((f3_fn)host(&fn, "vkCmdSetDepthBias", false))(cb, constant, clamp, slope);
}

static void WINAPI vkCmdSetDepthBounds(VkCommandBuffer cb, float min, float max)
{
    static void *fn;
    ((f2_fn)host(&fn, "vkCmdSetDepthBounds", false))(cb, min, max);
}

static void WINAPI vkCmdSetExtraPrimitiveOverestimationSizeEXT(VkCommandBuffer cb, float size)
{
    static void *fn;
    f1_fn f = host(&fn, "vkCmdSetExtraPrimitiveOverestimationSizeEXT", true);
    if (f)
        f(cb, size);
}

BIT_TABLE(vk_floats,
          BIT_FN(vkCmdSetLineWidth), BIT_FN(vkCmdSetDepthBias), BIT_FN(vkCmdSetDepthBounds),
          BIT_FN(vkCmdSetExtraPrimitiveOverestimationSizeEXT));
