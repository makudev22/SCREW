#ifndef SCREW_VK_H
#define SCREW_VK_H

#include <screw/bit.h>
#include <screw/win.h>

#include <stdbool.h>
#include <stdint.h>

#define VK_SUCCESS 0
#define VK_ERROR_OUT_OF_HOST_MEMORY (-1)
#define VK_ERROR_INITIALIZATION_FAILED (-3)
#define VK_ERROR_EXTENSION_NOT_PRESENT (-7)
#define VK_ERROR_SURFACE_LOST_KHR (-1000000000)

#define VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR 1000004000
#define VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR 1000009000
#define VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT 1000011000
#define VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT 1000128004

#define VK_MAX_EXTENSION_NAME 256

#define EXT_WIN32_SURFACE "VK_KHR_win32_surface"
#define EXT_XLIB_SURFACE "VK_KHR_xlib_surface"

typedef int32_t VkResult;
typedef uint32_t VkBool32;
typedef void *VkInstance;
typedef void *VkPhysicalDevice;
typedef void *VkDevice;
typedef void *VkCommandBuffer;
typedef uint64_t VkSurfaceKHR;
typedef uint64_t VkHandle;
typedef void (*PFN_vkVoidFunction)(void);

struct vk_base {
    int32_t type;
    const struct vk_base *next;
};

struct vk_instance_info {
    int32_t type;
    const void *next;
    uint32_t flags;
    const void *app;
    uint32_t layer_count;
    const char *const *layers;
    uint32_t ext_count;
    const char *const *exts;
};

struct vk_device_info {
    int32_t type;
    const void *next;
    uint32_t flags;
    uint32_t queue_info_count;
    const void *queue_infos;
    uint32_t layer_count;
    const char *const *layers;
    uint32_t ext_count;
    const char *const *exts;
    const void *features;
};

struct vk_extension {
    char name[VK_MAX_EXTENSION_NAME];
    uint32_t spec_version;
};

struct vk_win32_surface_info {
    int32_t type;
    const void *next;
    uint32_t flags;
    void *instance;
    HWND hwnd;
};

struct vk_xlib_surface_info {
    int32_t type;
    const void *next;
    uint32_t flags;
    void *display;
    unsigned long window;
};

struct vk_debug_utils_info {
    int32_t type;
    const void *next;
    uint32_t flags;
    uint32_t severity;
    uint32_t types;
    void *callback;
    void *user;
};

struct vk_debug_report_info {
    int32_t type;
    const void *next;
    uint32_t flags;
    void *callback;
    void *user;
};

_Static_assert(sizeof(struct vk_instance_info) == 64, "VkInstanceCreateInfo layout");
_Static_assert(sizeof(struct vk_device_info) == 72, "VkDeviceCreateInfo layout");
_Static_assert(sizeof(struct vk_extension) == 260, "VkExtensionProperties layout");
_Static_assert(sizeof(struct vk_win32_surface_info) == 40, "VkWin32SurfaceCreateInfoKHR layout");
_Static_assert(sizeof(struct vk_debug_utils_info) == 48, "VkDebugUtilsMessengerCreateInfoEXT layout");
_Static_assert(sizeof(struct vk_debug_report_info) == 40, "VkDebugReportCallbackCreateInfoEXT layout");

typedef PFN_vkVoidFunction (*host_gipa)(VkInstance, const char *);
typedef PFN_vkVoidFunction (*host_gdpa)(VkDevice, const char *);

bool vk_loaded(void);
void *vk_host_global(const char *name);
void *vk_host_instance(VkInstance inst, const char *name);
void *vk_host_device(VkDevice dev, const char *name);
void *vk_wrap(const char *name, void *host);
void vk_track_device(VkDevice dev);
VkDevice vk_last_device(void);

const char **vk_rewrite_names(const char *const *names, uint32_t count, uint32_t *out_count, bool device);
void vk_free_names(const char **names);
void vk_debug_patch(const void *chain, bool restore);

extern const struct bit_table vk_loader;
extern const struct bit_table vk_instance;
extern const struct bit_table vk_surface;
extern const struct bit_table vk_debug;
extern const struct bit_table vk_floats;

#endif
