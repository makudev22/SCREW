#include "vk.h"

#include <screw/bridge.h>

#define PATCH_MAX 8

typedef VkResult (*create_messenger_fn)(VkInstance, const struct vk_debug_utils_info *, const void *, VkHandle *);
typedef VkResult (*create_report_fn)(VkInstance, const struct vk_debug_report_info *, const void *, VkHandle *);

struct patch {
    void **slot;
    void *original;
};

static __thread struct patch patches[PATCH_MAX];
static __thread int patch_count;

static void **callback_slot(const struct vk_base *node)
{
    if (node->type == VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT)
        return &((struct vk_debug_utils_info *)node)->callback;
    if (node->type == VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT)
        return &((struct vk_debug_report_info *)node)->callback;
    return NULL;
}

void vk_debug_patch(const void *chain, bool restore)
{
    if (restore) {
        while (patch_count) {
            struct patch *p = &patches[--patch_count];
            *p->slot = p->original;
        }
        return;
    }
    for (const struct vk_base *n = chain; n && patch_count < PATCH_MAX; n = n->next) {
        void **slot = callback_slot(n);
        if (!slot || !*slot)
            continue;
        patches[patch_count++] = (struct patch){ slot, *slot };
        *slot = bridge_to_windows(*slot);
    }
}

static VkResult WINAPI vkCreateDebugUtilsMessengerEXT(VkInstance inst, const struct vk_debug_utils_info *info,
                                                      const void *alloc, VkHandle *out)
{
    create_messenger_fn create = vk_host_instance(inst, "vkCreateDebugUtilsMessengerEXT");
    if (!create)
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    struct vk_debug_utils_info copy = *info;
    copy.callback = bridge_to_windows(info->callback);
    vk_debug_patch(info->next, false);
    VkResult r = create(inst, &copy, NULL, out);
    vk_debug_patch(info->next, true);
    return r;
}

static VkResult WINAPI vkCreateDebugReportCallbackEXT(VkInstance inst, const struct vk_debug_report_info *info,
                                                      const void *alloc, VkHandle *out)
{
    create_report_fn create = vk_host_instance(inst, "vkCreateDebugReportCallbackEXT");
    if (!create)
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    struct vk_debug_report_info copy = *info;
    copy.callback = bridge_to_windows(info->callback);
    return create(inst, &copy, NULL, out);
}

BIT_TABLE(vk_debug, BIT_FN(vkCreateDebugUtilsMessengerEXT), BIT_FN(vkCreateDebugReportCallbackEXT));
