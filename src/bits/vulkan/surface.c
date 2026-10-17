#include "vk.h"

#include <screw/display.h>
#include <screw/log.h>

#include <X11/Xlib.h>

typedef VkResult (*create_xlib_fn)(VkInstance, const struct vk_xlib_surface_info *, const void *, VkSurfaceKHR *);
typedef VkBool32 (*xlib_support_fn)(VkPhysicalDevice, uint32_t, Display *, VisualID);

static VkResult WINAPI vkCreateWin32SurfaceKHR(VkInstance inst, const struct vk_win32_surface_info *info,
                                               const void *alloc, VkSurfaceKHR *out)
{
    create_xlib_fn create = vk_host_instance(inst, "vkCreateXlibSurfaceKHR");
    Display *dpy = display_x11();
    unsigned long window = display_window(info->hwnd);
    if (!create) {
        warn("vulkan: the driver has no VK_KHR_xlib_surface");
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    if (!dpy || !window) {
        warn("vulkan: window %p has no X11 window behind it", info->hwnd);
        return VK_ERROR_SURFACE_LOST_KHR;
    }
    struct vk_xlib_surface_info x = { VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR, NULL, 0, dpy, window };
    VkResult r = create(inst, &x, NULL, out);
    trace("vulkan: surface for window %p (X11 0x%lx) -> %d", info->hwnd, window, r);
    return r;
}

static VkBool32 WINAPI vkGetPhysicalDeviceWin32PresentationSupportKHR(VkPhysicalDevice phys, uint32_t family)
{
    xlib_support_fn fn = vk_host_global("vkGetPhysicalDeviceXlibPresentationSupportKHR");
    Display *dpy = display_x11();
    if (!fn || !dpy)
        return 0;
    VisualID visual = XVisualIDFromVisual(DefaultVisual(dpy, DefaultScreen(dpy)));
    return fn(phys, family, dpy, visual);
}

BIT_TABLE(vk_surface, BIT_FN(vkCreateWin32SurfaceKHR), BIT_FN(vkGetPhysicalDeviceWin32PresentationSupportKHR));
