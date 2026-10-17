#include "vk.h"

#include <screw/log.h>

#include <stdlib.h>
#include <string.h>

typedef VkResult (*create_instance_fn)(const struct vk_instance_info *, const void *, VkInstance *);
typedef VkResult (*create_device_fn)(VkPhysicalDevice, const struct vk_device_info *, const void *, VkDevice *);
typedef VkResult (*enum_instance_ext_fn)(const char *, uint32_t *, struct vk_extension *);

static const char *const windows_only[] = {
    "VK_KHR_external_memory_win32", "VK_KHR_external_semaphore_win32", "VK_KHR_external_fence_win32",
    "VK_KHR_win32_keyed_mutex", "VK_NV_external_memory_win32", "VK_NV_win32_keyed_mutex",
    "VK_EXT_full_screen_exclusive",
};

static bool is_windows_only(const char *name)
{
    for (size_t i = 0; i < sizeof windows_only / sizeof windows_only[0]; i++)
        if (!strcmp(name, windows_only[i]))
            return true;
    return false;
}

const char **vk_rewrite_names(const char *const *names, uint32_t count, uint32_t *out_count, bool device)
{
    const char **out = calloc(count + 1, sizeof *out);
    uint32_t n = 0;
    for (uint32_t i = 0; i < count; i++) {
        const char *name = names[i];
        if (device && is_windows_only(name)) {
            trace("vulkan: dropping Windows-only device extension %s", name);
            continue;
        }
        if (!device && !strcmp(name, EXT_WIN32_SURFACE))
            name = EXT_XLIB_SURFACE;
        bool dup = false;
        for (uint32_t j = 0; j < n && !dup; j++)
            dup = !strcmp(out[j], name);
        if (!dup)
            out[n++] = name;
    }
    *out_count = n;
    return out;
}

void vk_free_names(const char **names) { free(names); }

static VkResult WINAPI vkCreateInstance(const struct vk_instance_info *info, const void *alloc, VkInstance *out)
{
    create_instance_fn create = vk_host_global("vkCreateInstance");
    if (!create)
        return VK_ERROR_INITIALIZATION_FAILED;
    struct vk_instance_info copy = *info;
    const char **names = vk_rewrite_names(info->exts, info->ext_count, &copy.ext_count, false);
    copy.exts = names;
    for (uint32_t i = 0; i < copy.ext_count; i++)
        trace("vulkan: instance extension %s", names[i]);
    vk_debug_patch(info->next, false);
    VkResult r = create(&copy, NULL, out);
    vk_debug_patch(info->next, true);
    vk_free_names(names);
    trace("vulkan: vkCreateInstance -> %d", r);
    return r;
}

static VkResult WINAPI vkEnumerateInstanceExtensionProperties(const char *layer, uint32_t *count,
                                                              struct vk_extension *props)
{
    enum_instance_ext_fn fn = vk_host_global("vkEnumerateInstanceExtensionProperties");
    if (!fn) {
        *count = 0;
        return VK_SUCCESS;
    }
    VkResult r = fn(layer, count, props);
    if (props && r >= 0)
        for (uint32_t i = 0; i < *count; i++)
            if (!strcmp(props[i].name, EXT_XLIB_SURFACE))
                strcpy(props[i].name, EXT_WIN32_SURFACE);
    return r;
}

static VkResult WINAPI vkCreateDevice(VkPhysicalDevice phys, const struct vk_device_info *info, const void *alloc,
                                      VkDevice *out)
{
    create_device_fn create = vk_host_global("vkCreateDevice");
    if (!create)
        return VK_ERROR_INITIALIZATION_FAILED;
    struct vk_device_info copy = *info;
    const char **names = vk_rewrite_names(info->exts, info->ext_count, &copy.ext_count, true);
    copy.exts = names;
    for (uint32_t i = 0; i < copy.ext_count; i++)
        trace("vulkan: device extension %s", names[i]);
    VkResult r = create(phys, &copy, NULL, out);
    vk_free_names(names);
    if (r == VK_SUCCESS)
        vk_track_device(*out);
    trace("vulkan: vkCreateDevice -> %d", r);
    return r;
}

BIT_TABLE(vk_instance,
          BIT_FN(vkCreateInstance), BIT_FN(vkEnumerateInstanceExtensionProperties),
          BIT_FN(vkCreateDevice));
