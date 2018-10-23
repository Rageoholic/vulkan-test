/* Feature macros */

#define GLFW_INCLUDE_VULKAN

#include "rutils/math.h"

#include "rutils/file.h"
#include "rutils/math.h"
#include "rutils/string.h"
#include "vk-basic.h"
#include <GLFW/glfw3.h>

#define WIDTH 800
#define HEIGHT 600

#define VERT_SHADER_LOC "shaders/basic-shader.vert.spv"
#define FRAG_SHADER_LOC "shaders/basic-shader.frag.spv"

local errcode glfwCreateVkInstance(VkInstance *instance, const char *appName, u32 appVer, u32 apiVer)
{
    VkApplicationInfo appInfo = {0};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = appName;
    appInfo.applicationVersion = appVer;
    appInfo.pEngineName = "custom";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 0, 0);
    appInfo.apiVersion = apiVer;

    u32 glfwExtensionCount = 0;
    const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    VkInstanceCreateInfo cinfo = {0};
    cinfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    cinfo.pApplicationInfo = &appInfo;
    cinfo.ppEnabledExtensionNames = glfwExtensions;
    cinfo.enabledExtensionCount = glfwExtensionCount;

    if (vkCreateInstance(&cinfo, NULL, instance) != VK_SUCCESS)
    {

        return ERROR_INITIALIZATION_FAILURE;
    }
    return ERROR_SUCCESS;
}

local int ApplicationCheckDevice(VkPhysicalDevice dev,
                                 VkSurfaceKHR surf,
                                 const char **extensionList,
                                 size_t extensionCount)
{
    VkQueueIndices indices = GetDeviceQueueGraphicsAndPresentationIndices(dev, surf);
    if (indices.graphicsSupport && indices.presentSupport &&
        CheckDeviceExtensionSupport(dev, extensionList, extensionCount))
    {
        SwapChainSupportDetails sd = QuerySwapChainSupport(dev, surf);

        bool validSwapChain = sd.formats && sd.presentModes;

        DeleteSwapChainSupportDetails(sd);
        return validSwapChain;
    }
    return false;
}

int main(int argc, char **argv)
{
    int returnValue = ERROR_SUCCESS;
    ignore argc, ignore argv;

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow *win = glfwCreateWindow(WIDTH, HEIGHT, "vulkan", NULL, NULL);
    if (win == NULL)
    {
        fputs("ERROR! Could not create window\n", stderr);
        returnValue = ERROR_INITIALIZATION_FAILURE;
        goto errorNoWin;
    }

    VkInstance instance;
    if (glfwCreateVkInstance(&instance, "Vulkan tutorial",
                             VK_MAKE_VERSION(0, 0, 0),
                             VK_API_VERSION_1_0))

    {
        fputs("ERROR! could not create instance\n", stderr);
        returnValue = ERROR_INITIALIZATION_FAILURE;
        goto errorNoInstance;
    }

    VkSurfaceKHR surf;
    if (glfwCreateWindowSurface(instance, win, NULL, &surf) != VK_SUCCESS)
    {
        fputs("NOT ABLE TO CREATE SURFACE\n", stdout);
        returnValue = ERROR_INITIALIZATION_FAILURE;
        goto errorNoSurf;
    }

    const char *extensionList[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkPhysicalDevice physdev = GetVkPhysicalDevice(instance, surf, extensionList,
                                                   countof(extensionList),
                                                   ApplicationCheckDevice);
    if (physdev == VK_NULL_HANDLE)
    {
        fputs("NO SUITABLE DEVICE\n", stderr);
        returnValue = ERROR_INITIALIZATION_FAILURE;
        goto errorNoContext;
    }

    VkPhysicalDeviceFeatures features = {0};
    VkRenderContext rc;
    if (CreateVkRenderContext(physdev, &features, surf,
                              WIDTH, HEIGHT, &rc) != ERROR_SUCCESS)
    {
        fputs("NOT ABLE TO CREATE DEVICE\n", stderr);
        returnValue = ERROR_INITIALIZATION_FAILURE;
        goto errorNoContext;
    }

    isize vertShaderSize;
    void *vertShaderCode = MapFileToROBuffer(VERT_SHADER_LOC, NULL, &vertShaderSize);
    if (!vertShaderCode)
    {
        fputs("Could not find vertex shader\n", stderr);
    }

    VkShaderModule vertShader = CreateVkShaderModule(&rc, vertShaderCode, vertShaderSize - 1);
    if (vertShader == VK_NULL_HANDLE)
    {
        fputs("Could not load vertex shader\n", stderr);
    }
    UnmapMappedBuffer(vertShaderCode, vertShaderSize);

    isize fragShaderSize;
    void *fragShaderCode = MapFileToROBuffer(FRAG_SHADER_LOC, NULL, &fragShaderSize);
    if (!fragShaderCode)
    {
        fputs("Could not find fragment shader\n", stderr);
    }

    VkShaderModule fragShader = CreateVkShaderModule(&rc, fragShaderCode, fragShaderSize - 1);
    if (fragShader == VK_NULL_HANDLE)
    {
        fputs("Could not load fragment shader\n", stderr);
    }
    UnmapMappedBuffer(fragShaderCode, fragShaderSize);

    VkRenderPass renderpass = CreateRenderPass(&rc);
    if (renderpass == VK_NULL_HANDLE)
    {
        returnValue = ERROR_INITIALIZATION_FAILURE;
        fputs("Could not create render pass", stderr);
        goto errorRenderPass;
    }
    VkPipelineLayout layout;
    VkPipeline pipeline = CreateGraphicsPipeline(&rc, vertShader,
                                                 fragShader, renderpass,
                                                 &layout);
    if (pipeline == VK_NULL_HANDLE)
    {
        fputs("Could not create graphics pipeline\n", stderr);
        returnValue = ERROR_INITIALIZATION_FAILURE;
        goto errorPipeline;
    }

    vkDestroyShaderModule(rc.dev, vertShader, NULL);
    vkDestroyShaderModule(rc.dev, fragShader, NULL);

    VkFramebuffer *framebuffers = CreateFrameBuffers(&rc, renderpass);
    if (framebuffers == NULL)
    {
        fputs("Could not create framebuffer\n", stderr);
        goto errorFramebuffers;
    }

    while (!glfwWindowShouldClose(win))
    {
        glfwPollEvents();
    }

    for (u32 i = 0; i < rc.imageCount; i++)
    {
        vkDestroyFramebuffer(rc.dev, framebuffers[i], NULL);
    }
    free(framebuffers);

errorFramebuffers:
    vkDestroyPipeline(rc.dev, pipeline, NULL);
    vkDestroyPipelineLayout(rc.dev, layout, NULL);
errorPipeline:
    vkDestroyRenderPass(rc.dev, renderpass, NULL);
errorRenderPass:
    DestroyVkRenderContext(rc);
errorNoContext:
    vkDestroySurfaceKHR(instance, surf, NULL);
errorNoSurf:
    vkDestroyInstance(instance, NULL);
errorNoInstance:
    glfwDestroyWindow(win);
errorNoWin:
    glfwTerminate();

    return returnValue;
}
