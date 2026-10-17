#include "cli.h"

#include <screw/path.h>

#include <X11/Xlib.h>

#include <asm/prctl.h>
#include <dirent.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <unistd.h>

#define VK_STRUCTURE_TYPE_APPLICATION_INFO 0
#define VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO 1
#define VK_PROPERTIES_BYTES 4096

enum level { OK, WARN, FAIL };

struct vk_app {
    int type;
    const void *next;
    const char *app_name;
    uint32_t app_version;
    const char *engine_name;
    uint32_t engine_version;
    uint32_t api_version;
};

struct vk_instance_info {
    int type;
    const void *next;
    uint32_t flags;
    const struct vk_app *app;
    uint32_t layer_count;
    const char *const *layers;
    uint32_t ext_count;
    const char *const *exts;
};

typedef int (*vk_version_fn)(uint32_t *);
typedef int (*vk_create_fn)(const struct vk_instance_info *, const void *, void **);
typedef void (*vk_destroy_fn)(void *, const void *);
typedef int (*vk_enum_fn)(void *, uint32_t *, void **);
typedef void (*vk_props_fn)(void *, void *);
typedef void *(*vk_proc_fn)(void *, const char *);

static int failures, warnings;
static bool color;

static void report(enum level l, const char *fmt, ...)
{
    static const char *tags[] = { "ok", "warn", "FAIL" };
    static const char *colors[] = { "\033[32m", "\033[33m", "\033[31m" };
    if (l == WARN)
        warnings++;
    if (l == FAIL)
        failures++;
    printf("%s[%-4s]%s ", color ? colors[l] : "", tags[l], color ? "\033[0m" : "");
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}

static void fix(const char *fmt, ...)
{
    printf("       fix: ");
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}

static void section(const char *name) { printf("\n%s%s%s\n", color ? "\033[1m" : "", name, color ? "\033[0m" : ""); }

static bool have_tool(const char *tool)
{
    const char *env = getenv("PATH");
    if (!env)
        return false;
    char *copy = strdup(env), *save, path[PATH_MAX];
    bool found = false;
    for (char *dir = strtok_r(copy, ":", &save); dir && !found; dir = strtok_r(NULL, ":", &save)) {
        snprintf(path, sizeof path, "%s/%s", dir, tool);
        found = !access(path, X_OK);
    }
    free(copy);
    return found;
}

static bool read_line(const char *path, char *out, size_t size)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    bool ok = fgets(out, size, f) != NULL;
    fclose(f);
    if (ok)
        out[strcspn(out, "\n")] = 0;
    return ok;
}

static void check_system(void)
{
    section("System");
    struct utsname u;
    uname(&u);
    if (!strcmp(u.machine, "x86_64"))
        report(OK, "CPU architecture %s, kernel %s", u.machine, u.release);
    else
        report(FAIL, "CPU architecture %s: Screw runs 64-bit x86 Windows programs only on x86_64", u.machine);

    unsigned long gs = 0;
    if (!syscall(SYS_arch_prctl, ARCH_GET_GS, &gs))
        report(OK, "thread block register %%gs is available");
    else
        report(FAIL, "kernel refused arch_prctl(ARCH_GET_GS)");

    void *want = (void *)0x140000000ul;
    void *p = mmap(want, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == want) {
        report(OK, "default Windows image base 0x140000000 can be mapped");
        munmap(p, 4096);
    } else {
        if (p != MAP_FAILED)
            munmap(p, 4096);
        report(WARN, "cannot map the default image base; programs will be relocated, which needs relocation data");
    }

    char buf[64];
    if (read_line("/proc/sys/vm/max_map_count", buf, sizeof buf) && atol(buf) < 262144) {
        report(WARN, "vm.max_map_count is %s; large games may run out of memory mappings", buf);
        fix("echo vm.max_map_count=1048576 | sudo tee /etc/sysctl.d/90-screw.conf && sudo sysctl --system");
    } else {
        report(OK, "vm.max_map_count is %s", buf);
    }
}

static void check_integration(void)
{
    section("Integration");
    if (access("/proc/sys/fs/binfmt_misc/register", F_OK)) {
        report(WARN, "binfmt_misc is not mounted; ./program.exe cannot start directly");
        fix("sudo mount -t binfmt_misc binfmt_misc /proc/sys/fs/binfmt_misc");
    } else if (access("/proc/sys/fs/binfmt_misc/screw", F_OK)) {
        report(WARN, ".exe files are not registered; they must be started as: screw program.exe");
        fix("sudo screw register");
    } else {
        char line[PATH_MAX];
        read_line("/proc/sys/fs/binfmt_misc/screw", line, sizeof line);
        report(OK, ".exe files start directly through binfmt (%s)", line);
    }

    path_init();
    const char *c = path_c_root();
    if (!access(c, W_OK))
        report(OK, "drive C: is %s", c);
    else
        report(FAIL, "drive C: folder %s is not writable", c);

    if (have_tool("zenity") || have_tool("kdialog"))
        report(OK, "message boxes will use %s", have_tool("zenity") ? "zenity" : "kdialog");
    else {
        report(WARN, "no zenity or kdialog; MessageBox falls back to the terminal");
        fix("sudo apt install zenity");
    }
}

static void check_display(void)
{
    section("Display");
    const char *x = getenv("DISPLAY"), *wl = getenv("WAYLAND_DISPLAY");
    if (!x && !wl) {
        report(WARN, "no graphical session (DISPLAY and WAYLAND_DISPLAY are empty); only console programs can run");
        return;
    }
    if (!x) {
        report(FAIL, "Wayland session without XWayland; Screw windows need X11");
        fix("enable XWayland in your desktop settings, or log into an X11 session");
        return;
    }
    Display *d = XOpenDisplay(NULL);
    if (!d) {
        report(FAIL, "cannot connect to X11 display %s", x);
        fix("run from a terminal inside your desktop session, or check: xhost +si:localuser:$USER");
        return;
    }
    report(OK, "X11 display %s, %dx%d%s", x, DisplayWidth(d, DefaultScreen(d)), DisplayHeight(d, DefaultScreen(d)),
           wl ? " (XWayland)" : "");
    XCloseDisplay(d);
}

static const char *vendor_name(unsigned id)
{
    switch (id) {
    case 0x8086: return "Intel";
    case 0x1002: return "AMD";
    case 0x10de: return "NVIDIA";
    case 0x1af4: return "virtio (virtual machine)";
    case 0x15ad: return "VMware";
    default: return "unknown vendor";
    }
}

static unsigned check_gpus(void)
{
    section("Graphics hardware");
    DIR *d = opendir("/sys/class/drm");
    unsigned first = 0;
    int count = 0;
    struct dirent *e;
    while (d && (e = readdir(d))) {
        if (strncmp(e->d_name, "card", 4) || strchr(e->d_name, '-'))
            continue;
        char path[PATH_MAX], vendor[32] = "", driver[PATH_MAX] = "";
        snprintf(path, sizeof path, "/sys/class/drm/%s/device/vendor", e->d_name);
        if (!read_line(path, vendor, sizeof vendor))
            continue;
        snprintf(path, sizeof path, "/sys/class/drm/%s/device/driver", e->d_name);
        ssize_t n = readlink(path, driver, sizeof driver - 1);
        driver[n > 0 ? n : 0] = 0;
        const char *drv = strrchr(driver, '/') ? strrchr(driver, '/') + 1 : "none";
        unsigned id = strtoul(vendor, NULL, 16);
        if (!first)
            first = id;
        count++;
        if (id == 0x10de && !strcmp(drv, "nouveau")) {
            report(WARN, "%s: %s GPU on the open nouveau driver; games will be slow", e->d_name, vendor_name(id));
            fix("install the proprietary driver: sudo apt install nvidia-driver nvidia-vulkan-icd, then reboot");
        } else {
            report(OK, "%s: %s GPU, kernel driver %s", e->d_name, vendor_name(id), drv);
        }
    }
    if (d)
        closedir(d);
    if (!count)
        report(WARN, "no GPU found in /sys/class/drm; 3D will use the slow software renderer");
    return first;
}

static const char *icd_for(unsigned vendor)
{
    switch (vendor) {
    case 0x8086: return "intel";
    case 0x1002: return "radeon";
    case 0x10de: return "nvidia";
    default: return NULL;
    }
}

static bool icd_present(const char *needle)
{
    static const char *dirs[] = { "/usr/share/vulkan/icd.d", "/etc/vulkan/icd.d", "/usr/local/share/vulkan/icd.d" };
    for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        DIR *d = opendir(dirs[i]);
        struct dirent *e;
        bool found = false;
        while (d && (e = readdir(d)))
            if (strstr(e->d_name, needle))
                found = true;
        if (d)
            closedir(d);
        if (found)
            return true;
    }
    return false;
}

static void vulkan_fix(unsigned vendor)
{
    if (vendor == 0x10de)
        fix("sudo apt install nvidia-driver nvidia-vulkan-icd libvulkan1, then reboot");
    else
        fix("sudo apt install mesa-vulkan-drivers libvulkan1 vulkan-tools");
}

static void check_vulkan(unsigned vendor)
{
    section("Vulkan (needed for DXVK and games)");
    void *lib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        report(FAIL, "Vulkan loader libvulkan.so.1 is missing");
        vulkan_fix(vendor);
        return;
    }
    vk_proc_fn get = (vk_proc_fn)dlsym(lib, "vkGetInstanceProcAddr");
    vk_version_fn version = get ? (vk_version_fn)get(NULL, "vkEnumerateInstanceVersion") : NULL;
    uint32_t v = 0;
    if (version && !version(&v))
        report(OK, "Vulkan loader %u.%u.%u", v >> 22, (v >> 12) & 0x3FF, v & 0xFFF);
    else
        report(OK, "Vulkan loader 1.0");

    const char *icd = icd_for(vendor);
    if (icd && !icd_present(icd)) {
        report(FAIL, "no Vulkan driver for your %s GPU", vendor_name(vendor));
        vulkan_fix(vendor);
    }

    struct vk_app app = { VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "screw-doctor", 1, "screw", 1, (1u << 22) };
    struct vk_instance_info info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &app, 0, NULL, 0, NULL };
    vk_create_fn create = (vk_create_fn)get(NULL, "vkCreateInstance");
    void *inst = NULL;
    int r = create ? create(&info, NULL, &inst) : -1;
    if (r || !inst) {
        report(FAIL, "cannot create a Vulkan instance (error %d)", r);
        vulkan_fix(vendor);
        dlclose(lib);
        return;
    }
    vk_enum_fn enumerate = (vk_enum_fn)get(inst, "vkEnumeratePhysicalDevices");
    vk_props_fn props = (vk_props_fn)get(inst, "vkGetPhysicalDeviceProperties");
    vk_destroy_fn destroy = (vk_destroy_fn)get(inst, "vkDestroyInstance");
    uint32_t n = 0;
    enumerate(inst, &n, NULL);
    void *devs[16];
    if (n > 16)
        n = 16;
    enumerate(inst, &n, devs);

    static const char *types[] = { "other", "integrated GPU", "discrete GPU", "virtual GPU", "CPU (software)" };
    uint8_t *all[16];
    bool hardware = false;
    for (uint32_t i = 0; i < n; i++) {
        all[i] = calloc(1, VK_PROPERTIES_BYTES);
        props(devs[i], all[i]);
        hardware |= *(uint32_t *)(all[i] + 16) != 4;
    }
    for (uint32_t i = 0; i < n; i++) {
        uint8_t *p = all[i];
        uint32_t api = *(uint32_t *)p, type = *(uint32_t *)(p + 16);
        const char *name = (const char *)(p + 20);
        bool software = type == 4;
        bool old = !software && (api >> 22) == 1 && ((api >> 12) & 0x3FF) < 3;
        report(old || (software && !hardware) ? WARN : OK, "device %u: %s, %s%s, Vulkan %u.%u.%u", i, name,
               type < 5 ? types[type] : "unknown", software && hardware ? " fallback" : "", api >> 22,
               (api >> 12) & 0x3FF, api & 0xFFF);
        if (old)
            fix("DXVK 2.x needs Vulkan 1.3; update Mesa or your GPU driver");
        free(p);
    }
    if (!n)
        report(FAIL, "Vulkan found no devices");
    if (n && !hardware) {
        report(WARN, "only a software renderer is available; games will be very slow");
        vulkan_fix(vendor);
    }
    destroy(inst, NULL);
    dlclose(lib);
}

static void check_tools(void)
{
    section("Optional tools");
    if (have_tool("x86_64-w64-mingw32-gcc"))
        report(OK, "MinGW compiler found: make test can build Windows test programs");
    else {
        report(WARN, "MinGW compiler missing: make test cannot build its Windows samples");
        fix("sudo apt install gcc-mingw-w64-x86-64");
    }
    if (have_tool("vulkaninfo"))
        report(OK, "vulkaninfo found for deeper GPU checks");
    else {
        report(WARN, "vulkaninfo missing (optional)");
        fix("sudo apt install vulkan-tools");
    }
}

int cli_doctor(void)
{
    color = isatty(1);
    printf("Screw " SCREW_VERSION " doctor\n");
    check_system();
    check_integration();
    check_display();
    unsigned vendor = check_gpus();
    check_vulkan(vendor);
    check_tools();
    printf("\n%d problem%s, %d warning%s\n", failures, failures == 1 ? "" : "s", warnings, warnings == 1 ? "" : "s");
    return failures ? 1 : 0;
}
