#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <vulkan/vulkan.h>

#include "cube.vert.h"
#include "cube.frag.h"

#define WIDTH 800
#define HEIGHT 600
#define CHECK(x) do { VkResult _r = (x); if (_r != VK_SUCCESS) { printf("%s failed: %d\n", #x, _r); return 2; } } while (0)

static PFN_vkGetInstanceProcAddr p_vkGetInstanceProcAddr;

#define INSTANCE_PROCS \
    X(vkCreateInstance) X(vkDestroyInstance) X(vkCreateWin32SurfaceKHR) X(vkDestroySurfaceKHR) \
    X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceQueueFamilyProperties) \
    X(vkGetPhysicalDeviceSurfaceSupportKHR) X(vkGetPhysicalDeviceSurfaceFormatsKHR) \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) X(vkGetPhysicalDeviceSurfacePresentModesKHR) \
    X(vkGetPhysicalDeviceMemoryProperties) X(vkGetPhysicalDeviceProperties) X(vkCreateDevice) X(vkGetDeviceProcAddr)

#define DEVICE_PROCS \
    X(vkGetDeviceQueue) X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) X(vkGetSwapchainImagesKHR) \
    X(vkCreateImageView) X(vkCreateImage) X(vkGetImageMemoryRequirements) X(vkBindImageMemory) \
    X(vkAllocateMemory) X(vkFreeMemory) X(vkCreateRenderPass) X(vkCreateFramebuffer) \
    X(vkCreateShaderModule) X(vkCreateBuffer) X(vkGetBufferMemoryRequirements) X(vkBindBufferMemory) \
    X(vkMapMemory) X(vkCreateDescriptorSetLayout) X(vkCreateDescriptorPool) X(vkAllocateDescriptorSets) \
    X(vkUpdateDescriptorSets) X(vkCreatePipelineLayout) X(vkCreateGraphicsPipelines) X(vkCreateCommandPool) \
    X(vkAllocateCommandBuffers) X(vkCreateSemaphore) X(vkCreateFence) X(vkWaitForFences) X(vkResetFences) \
    X(vkAcquireNextImageKHR) X(vkResetCommandBuffer) X(vkBeginCommandBuffer) X(vkCmdBeginRenderPass) \
    X(vkCmdBindPipeline) X(vkCmdBindVertexBuffers) X(vkCmdBindIndexBuffer) X(vkCmdBindDescriptorSets) \
    X(vkCmdSetViewport) X(vkCmdSetScissor) X(vkCmdDrawIndexed) X(vkCmdEndRenderPass) X(vkEndCommandBuffer) \
    X(vkQueueSubmit) X(vkQueuePresentKHR) X(vkDeviceWaitIdle)

#define X(n) static PFN_##n n;
INSTANCE_PROCS
DEVICE_PROCS
#undef X

typedef struct { float m[16]; } mat4;

static mat4 identity(void)
{
    mat4 r = { { 0 } };
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

static mat4 multiply(mat4 a, mat4 b)
{
    mat4 r = { { 0 } };
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++)
            for (int k = 0; k < 4; k++)
                r.m[c * 4 + row] += a.m[k * 4 + row] * b.m[c * 4 + k];
    return r;
}

static mat4 perspective(float fovy, float aspect, float znear, float zfar)
{
    float f = 1.0f / tanf(fovy / 2.0f);
    mat4 r = { { 0 } };
    r.m[0] = f / aspect;
    r.m[5] = -f;
    r.m[10] = zfar / (znear - zfar);
    r.m[11] = -1.0f;
    r.m[14] = (znear * zfar) / (znear - zfar);
    return r;
}

static mat4 translate(float x, float y, float z)
{
    mat4 r = identity();
    r.m[12] = x;
    r.m[13] = y;
    r.m[14] = z;
    return r;
}

static mat4 rotate_xy(float ax, float ay)
{
    float cx = cosf(ax), sx = sinf(ax), cy = cosf(ay), sy = sinf(ay);
    mat4 rx = identity();
    rx.m[5] = cx; rx.m[6] = sx; rx.m[9] = -sx; rx.m[10] = cx;
    mat4 ry = identity();
    ry.m[0] = cy; ry.m[2] = -sy; ry.m[8] = sy; ry.m[10] = cy;
    return multiply(ry, rx);
}

struct vertex {
    float pos[3];
    float color[3];
};

static const struct vertex cube_verts[8] = {
    { { -1, -1, -1 }, { 0, 0, 0 } }, { { 1, -1, -1 }, { 1, 0, 0 } },
    { { 1, 1, -1 }, { 1, 1, 0 } },   { { -1, 1, -1 }, { 0, 1, 0 } },
    { { -1, -1, 1 }, { 0, 0, 1 } },  { { 1, -1, 1 }, { 1, 0, 1 } },
    { { 1, 1, 1 }, { 1, 1, 1 } },    { { -1, 1, 1 }, { 0, 1, 1 } },
};

static const uint16_t cube_indices[36] = {
    0, 1, 2, 2, 3, 0, 4, 6, 5, 6, 4, 7, 0, 3, 7, 7, 4, 0,
    1, 5, 6, 6, 2, 1, 3, 2, 6, 6, 7, 3, 0, 4, 5, 5, 1, 0,
};

static VkPhysicalDeviceMemoryProperties mem_props;

static uint32_t find_memory(uint32_t bits, VkMemoryPropertyFlags want)
{
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mem_props.memoryTypes[i].propertyFlags & want) == want)
            return i;
    return 0;
}

static VkDevice g_dev;

static void make_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer *buf, VkDeviceMemory *mem,
                        void **mapped)
{
    VkBufferCreateInfo bi = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(g_dev, &bi, NULL, buf);
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(g_dev, *buf, &req);
    VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = find_memory(req.memoryTypeBits,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(g_dev, &ai, NULL, mem);
    vkBindBufferMemory(g_dev, *buf, *mem, 0);
    if (mapped)
        vkMapMemory(g_dev, *mem, 0, size, 0, mapped);
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

static double now_ms(void)
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return 1000.0 * (double)c.QuadPart / (double)f.QuadPart;
}

int main(int argc, char **argv)
{
    int frames = argc > 1 ? atoi(argv[1]) : 1200;

    HMODULE vk = LoadLibraryA("vulkan-1.dll");
    p_vkGetInstanceProcAddr = vk ? (PFN_vkGetInstanceProcAddr)GetProcAddress(vk, "vkGetInstanceProcAddr") : NULL;
    if (!p_vkGetInstanceProcAddr) {
        puts("vulkan-1.dll not available");
        return 1;
    }
#define X(n) n = (PFN_##n)p_vkGetInstanceProcAddr(NULL, #n);
    INSTANCE_PROCS
#undef X

    const char *inst_ext[] = { "VK_KHR_surface", "VK_KHR_win32_surface" };
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "screw-cube";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = 2;
    ici.ppEnabledExtensionNames = inst_ext;
    VkInstance instance;
    CHECK(vkCreateInstance(&ici, NULL, &instance));
#define X(n) n = (PFN_##n)p_vkGetInstanceProcAddr(instance, #n);
    INSTANCE_PROCS
#undef X

    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "ScrewCube";
    RegisterClassA(&wc);
    HWND win = CreateWindowExA(0, "ScrewCube", "Screw Vulkan cube", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              CW_USEDEFAULT, CW_USEDEFAULT, WIDTH, HEIGHT, NULL, NULL, wc.hInstance, NULL);

    VkWin32SurfaceCreateInfoKHR si = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
    si.hinstance = wc.hInstance;
    si.hwnd = win;
    VkSurfaceKHR surface;
    CHECK(vkCreateWin32SurfaceKHR(instance, &si, NULL, &surface));

    uint32_t ndev = 1;
    VkPhysicalDevice phys;
    vkEnumeratePhysicalDevices(instance, &ndev, &phys);
    if (!ndev) {
        puts("no Vulkan device");
        return 1;
    }
    VkPhysicalDeviceProperties pprops;
    vkGetPhysicalDeviceProperties(phys, &pprops);
    printf("GPU: %s\n", pprops.deviceName);
    vkGetPhysicalDeviceMemoryProperties(phys, &mem_props);

    uint32_t nq = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &nq, NULL);
    VkQueueFamilyProperties qf[16];
    if (nq > 16)
        nq = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &nq, qf);
    uint32_t family = 0;
    for (uint32_t i = 0; i < nq; i++) {
        VkBool32 present = 0;
        vkGetPhysicalDeviceSurfaceSupportKHR(phys, i, surface, &present);
        if ((qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
            family = i;
            break;
        }
    }

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qci.queueFamilyIndex = family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    const char *dev_ext[] = { "VK_KHR_swapchain" };
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = dev_ext;
    CHECK(vkCreateDevice(phys, &dci, NULL, &g_dev));
#define X(n) n = (PFN_##n)vkGetDeviceProcAddr(g_dev, #n);
    DEVICE_PROCS
#undef X

    VkQueue queue;
    vkGetDeviceQueue(g_dev, family, 0, &queue);

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &caps);
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFFu) {
        extent.width = WIDTH;
        extent.height = HEIGHT;
    }
    VkFormat format = VK_FORMAT_B8G8R8A8_UNORM;
    uint32_t image_count = caps.minImageCount < 2 ? 2 : caps.minImageCount;

    VkSwapchainCreateInfoKHR sci = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    sci.surface = surface;
    sci.minImageCount = image_count;
    sci.imageFormat = format;
    sci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    sci.imageExtent = extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sci.clipped = VK_TRUE;
    VkSwapchainKHR swapchain;
    CHECK(vkCreateSwapchainKHR(g_dev, &sci, NULL, &swapchain));

    VkImage images[8];
    vkGetSwapchainImagesKHR(g_dev, swapchain, &image_count, NULL);
    if (image_count > 8)
        image_count = 8;
    vkGetSwapchainImagesKHR(g_dev, swapchain, &image_count, images);

    VkImageView color_views[8];
    for (uint32_t i = 0; i < image_count; i++) {
        VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vi.image = images[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = format;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        vkCreateImageView(g_dev, &vi, NULL, &color_views[i]);
    }

    VkImageCreateInfo dii = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    dii.imageType = VK_IMAGE_TYPE_2D;
    dii.format = VK_FORMAT_D32_SFLOAT;
    dii.extent.width = extent.width;
    dii.extent.height = extent.height;
    dii.extent.depth = 1;
    dii.mipLevels = 1;
    dii.arrayLayers = 1;
    dii.samples = VK_SAMPLE_COUNT_1_BIT;
    dii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    VkImage depth_image;
    vkCreateImage(g_dev, &dii, NULL, &depth_image);
    VkMemoryRequirements dreq;
    vkGetImageMemoryRequirements(g_dev, depth_image, &dreq);
    VkMemoryAllocateInfo dai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    dai.allocationSize = dreq.size;
    dai.memoryTypeIndex = find_memory(dreq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkDeviceMemory depth_mem;
    vkAllocateMemory(g_dev, &dai, NULL, &depth_mem);
    vkBindImageMemory(g_dev, depth_image, depth_mem, 0);
    VkImageViewCreateInfo dvi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    dvi.image = depth_image;
    dvi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    dvi.format = VK_FORMAT_D32_SFLOAT;
    dvi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    dvi.subresourceRange.levelCount = 1;
    dvi.subresourceRange.layerCount = 1;
    VkImageView depth_view;
    vkCreateImageView(g_dev, &dvi, NULL, &depth_view);

    VkAttachmentDescription att[2] = { 0 };
    att[0].format = format;
    att[0].samples = VK_SAMPLE_COUNT_1_BIT;
    att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    att[1].format = VK_FORMAT_D32_SFLOAT;
    att[1].samples = VK_SAMPLE_COUNT_1_BIT;
    att[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference color_ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depth_ref = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass = { 0 };
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;
    subpass.pDepthStencilAttachment = &depth_ref;
    VkRenderPassCreateInfo rpi = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpi.attachmentCount = 2;
    rpi.pAttachments = att;
    rpi.subpassCount = 1;
    rpi.pSubpasses = &subpass;
    VkRenderPass render_pass;
    CHECK(vkCreateRenderPass(g_dev, &rpi, NULL, &render_pass));

    VkFramebuffer framebuffers[8];
    for (uint32_t i = 0; i < image_count; i++) {
        VkImageView fb_att[2] = { color_views[i], depth_view };
        VkFramebufferCreateInfo fi = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fi.renderPass = render_pass;
        fi.attachmentCount = 2;
        fi.pAttachments = fb_att;
        fi.width = extent.width;
        fi.height = extent.height;
        fi.layers = 1;
        vkCreateFramebuffer(g_dev, &fi, NULL, &framebuffers[i]);
    }

    VkShaderModuleCreateInfo vmi = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    vmi.codeSize = sizeof cube_vert_spv;
    vmi.pCode = cube_vert_spv;
    VkShaderModule vshader;
    vkCreateShaderModule(g_dev, &vmi, NULL, &vshader);
    VkShaderModuleCreateInfo fmi = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    fmi.codeSize = sizeof cube_frag_spv;
    fmi.pCode = cube_frag_spv;
    VkShaderModule fshader;
    vkCreateShaderModule(g_dev, &fmi, NULL, &fshader);

    VkDescriptorSetLayoutBinding dslb = { 0 };
    dslb.binding = 0;
    dslb.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    dslb.descriptorCount = 1;
    dslb.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutCreateInfo dsli = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    dsli.bindingCount = 1;
    dsli.pBindings = &dslb;
    VkDescriptorSetLayout set_layout;
    vkCreateDescriptorSetLayout(g_dev, &dsli, NULL, &set_layout);

    VkBuffer vbuf, ibuf, ubuf;
    VkDeviceMemory vmem, imem, umem;
    void *umap;
    make_buffer(sizeof cube_verts, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &vbuf, &vmem, &umap);
    memcpy(umap, cube_verts, sizeof cube_verts);
    make_buffer(sizeof cube_indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &ibuf, &imem, &umap);
    memcpy(umap, cube_indices, sizeof cube_indices);
    make_buffer(sizeof(mat4), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &ubuf, &umem, &umap);

    VkDescriptorPoolSize dps = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 };
    VkDescriptorPoolCreateInfo dpi = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    dpi.maxSets = 1;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &dps;
    VkDescriptorPool pool;
    vkCreateDescriptorPool(g_dev, &dpi, NULL, &pool);
    VkDescriptorSetAllocateInfo dsai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    dsai.descriptorPool = pool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &set_layout;
    VkDescriptorSet set;
    vkAllocateDescriptorSets(g_dev, &dsai, &set);
    VkDescriptorBufferInfo dbi = { ubuf, 0, sizeof(mat4) };
    VkWriteDescriptorSet wds = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    wds.dstSet = set;
    wds.descriptorCount = 1;
    wds.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    wds.pBufferInfo = &dbi;
    vkUpdateDescriptorSets(g_dev, 1, &wds, 0, NULL);

    VkPipelineLayoutCreateInfo pli = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &set_layout;
    VkPipelineLayout layout;
    vkCreatePipelineLayout(g_dev, &pli, NULL, &layout);

    VkPipelineShaderStageCreateInfo stages[2] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, vshader, "main", NULL },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fshader, "main", NULL },
    };
    VkVertexInputBindingDescription vib = { 0, sizeof(struct vertex), VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription via[2] = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3 },
    };
    VkPipelineVertexInputStateCreateInfo vis = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vis.vertexBindingDescriptionCount = 1;
    vis.pVertexBindingDescriptions = &vib;
    vis.vertexAttributeDescriptionCount = 2;
    vis.pVertexAttributeDescriptions = via;
    VkPipelineInputAssemblyStateCreateInfo ias = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ias.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vps = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vps.viewportCount = 1;
    vps.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState cba = { 0 };
    cba.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dns = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dns.dynamicStateCount = 2;
    dns.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo gpi = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    gpi.stageCount = 2;
    gpi.pStages = stages;
    gpi.pVertexInputState = &vis;
    gpi.pInputAssemblyState = &ias;
    gpi.pViewportState = &vps;
    gpi.pRasterizationState = &rs;
    gpi.pMultisampleState = &ms;
    gpi.pDepthStencilState = &ds;
    gpi.pColorBlendState = &cb;
    gpi.pDynamicState = &dns;
    gpi.layout = layout;
    gpi.renderPass = render_pass;
    VkPipeline pipeline;
    CHECK(vkCreateGraphicsPipelines(g_dev, VK_NULL_HANDLE, 1, &gpi, NULL, &pipeline));

    VkCommandPoolCreateInfo cpi = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpi.queueFamilyIndex = family;
    VkCommandPool cmd_pool;
    vkCreateCommandPool(g_dev, &cpi, NULL, &cmd_pool);
    VkCommandBufferAllocateInfo cbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cbi.commandPool = cmd_pool;
    cbi.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbi.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(g_dev, &cbi, &cmd);

    VkSemaphoreCreateInfo spi = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkSemaphore acquired, rendered;
    vkCreateSemaphore(g_dev, &spi, NULL, &acquired);
    vkCreateSemaphore(g_dev, &spi, NULL, &rendered);
    VkFenceCreateInfo fci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkFence fence;
    vkCreateFence(g_dev, &fci, NULL, &fence);

    mat4 proj = perspective(1.0f, (float)extent.width / (float)extent.height, 0.1f, 100.0f);
    double start = now_ms();
    int done = 0;
    MSG m;
    for (int i = 0; i < frames; i++) {
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageA(&m);
            if (m.message == WM_QUIT)
                goto finish;
        }
        vkWaitForFences(g_dev, 1, &fence, VK_TRUE, UINT64_MAX);
        vkResetFences(g_dev, 1, &fence);
        uint32_t idx = 0;
        if (vkAcquireNextImageKHR(g_dev, swapchain, UINT64_MAX, acquired, VK_NULL_HANDLE, &idx) != VK_SUCCESS)
            break;

        float t = i / 60.0f;
        mat4 model = rotate_xy(t * 0.7f, t);
        mat4 view = translate(0, 0, -6.0f);
        mat4 mvp = multiply(proj, multiply(view, model));
        memcpy(umap, &mvp, sizeof mvp);

        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);
        VkClearValue clears[2];
        clears[0].color.float32[0] = 0.05f;
        clears[0].color.float32[1] = 0.05f;
        clears[0].color.float32[2] = 0.1f;
        clears[0].color.float32[3] = 1.0f;
        clears[1].depthStencil.depth = 1.0f;
        clears[1].depthStencil.stencil = 0;
        VkRenderPassBeginInfo rbi = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        rbi.renderPass = render_pass;
        rbi.framebuffer = framebuffers[idx];
        rbi.renderArea.extent = extent;
        rbi.clearValueCount = 2;
        rbi.pClearValues = clears;
        vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport vp = { 0, 0, (float)extent.width, (float)extent.height, 0, 1 };
        VkRect2D sc = { { 0, 0 }, extent };
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, NULL);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf, &offset);
        vkCmdBindIndexBuffer(cmd, ibuf, 0, VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(cmd, 36, 1, 0, 0, 0);
        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);

        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &acquired;
        submit.pWaitDstStageMask = &wait_stage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &rendered;
        vkQueueSubmit(queue, 1, &submit, fence);

        VkPresentInfoKHR present = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &rendered;
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &idx;
        if (vkQueuePresentKHR(queue, &present) != VK_SUCCESS)
            break;
        done++;
    }
finish:;
    vkDeviceWaitIdle(g_dev);
    double elapsed = now_ms() - start;
    printf("rendered %d frames in %.1f ms\n", done, elapsed);
    printf("average FPS: %.1f\n", done > 0 && elapsed > 0 ? done * 1000.0 / elapsed : 0.0);
    return done > 0 ? 0 : 1;
}
