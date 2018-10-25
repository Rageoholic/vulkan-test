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
#define MAX_CONCURRENT_FRAMES 2

local const char *validationLayers[] = {"VK_LAYER_LUNARG_standard_validation"};

local VkCommandBuffer *ApplicationSetupCommandBuffers(VkRenderContext *rc, VkCommandPool commandPool,
                                                      VkRenderPass renderpass, VkPipeline graphicsPipeline,
                                                      VkFramebuffer *framebuffers)
{
    VkCommandBuffer *ret = malloc(sizeof(VkCommandBuffer) * rc->imageCount);

    VkCommandBufferAllocateInfo allocInfo = {0};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = rc->imageCount;

    if (vkAllocateCommandBuffers(rc->dev, &allocInfo, ret) != VK_SUCCESS)
    {
        free(ret);
        return NULL;
    }

    for (u32 i = 0; i < rc->imageCount; i++)
    {
        VkCommandBufferBeginInfo beginInfo = {0};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

        if (vkBeginCommandBuffer(ret[i], &beginInfo) != VK_SUCCESS)
        {
            free(ret);
            return NULL;
        }

        VkRenderPassBeginInfo renderPassInfo = {0};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderpass;
        renderPassInfo.framebuffer = framebuffers[i];
        renderPassInfo.renderArea.offset = (VkOffset2D){0, 0};
        renderPassInfo.renderArea.extent = rc->e;

        VkClearValue clearColor = {0, 0, 0, 1};
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearColor;

        vkCmdBeginRenderPass(ret[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        {
            vkCmdBindPipeline(ret[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
            vkCmdDraw(ret[i], 3, 1, 0, 0);
        }
        vkCmdEndRenderPass(ret[i]);

        if (vkEndCommandBuffer(ret[i]) != VK_SUCCESS)
        {
            free(ret);
            return NULL;
        }
    }

    return ret;
}

typedef struct Semaphores
{
    VkSemaphore *imageAvailableSemaphores;
    VkSemaphore *renderFinishedSemaphores;
    VkFence *fences;
    u32 count;
} Semaphores;

local bool ApplicationDrawImage(VkRenderContext *rc, VkCommandBuffer *commandBuffers,
                                VkSemaphore imageSemaphore, VkSemaphore renderSemaphore,
                                VkFence fence)
{

    vkWaitForFences(rc->dev, 1, &fence, VK_TRUE, UINT64_MAX);
    vkResetFences(rc->dev, 1, &fence);
    u32 imageIndex;
    vkAcquireNextImageKHR(rc->dev, rc->swapchain, UINT64_MAX, imageSemaphore, NULL, &imageIndex);

    VkSubmitInfo submitInfo = {0};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &imageSemaphore;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[imageIndex];

    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderSemaphore;

    if (vkQueueSubmit(rc->graphicsQueue, 1, &submitInfo, fence) != VK_SUCCESS)
    {
        return false;
    }

    VkPresentInfoKHR presentInfo = {0};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderSemaphore;

    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &rc->swapchain;
    presentInfo.pImageIndices = &imageIndex;

    vkQueuePresentKHR(rc->presentQueue, &presentInfo);

    return true;
}

local bool ApplicationCreateSemaphores(VkRenderContext *rc, Semaphores *out, u32 semaphoreCount)
{

    out->count = semaphoreCount;
    out->imageAvailableSemaphores = malloc(sizeof(out->imageAvailableSemaphores[0]) * semaphoreCount);
    out->renderFinishedSemaphores = malloc(sizeof(out->renderFinishedSemaphores[0]) * semaphoreCount);
    out->fences = malloc(sizeof(out->fences[0]) * semaphoreCount);

    VkSemaphoreCreateInfo semaphoreInfo = {0};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo = {0};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (u32 i = 0; i < semaphoreCount; i++)
    {
        if (vkCreateSemaphore(rc->dev, &semaphoreInfo, NULL,
                              &out->imageAvailableSemaphores[i]) != VK_SUCCESS)
        {
            return false;
        }

        if (vkCreateSemaphore(rc->dev, &semaphoreInfo, NULL,
                              &out->renderFinishedSemaphores[i]) != VK_SUCCESS)
        {
            return false;
        }

        if (vkCreateFence(rc->dev, &fenceInfo, NULL, &out->fences[i]) != VK_SUCCESS)
        {
            return false;
        }
    }
    return true;
}

local bool CheckValidationLayerSupport()
{
    u32 layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, NULL);

    VkLayerProperties *availableLayers = malloc(sizeof(*availableLayers) * layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers);
    for (u32 i = 0; i < countof(validationLayers); i++)
    {
        bool layerFound = false;
        for (u32 j = 0; j < layerCount; j++)
        {
            if (streq(availableLayers[j].layerName, validationLayers[i]))
            {
                layerFound = true;
                break;
            }
        }
        if (!layerFound)
        {
            free(availableLayers);
            return false;
        }
    }
    free(availableLayers);

    return true;
}

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
#ifdef DEBUG
    if (!CheckValidationLayerSupport())
    {
        puts("No validation layers");
    }
    else
    {
        cinfo.enabledLayerCount = countof(validationLayers);
        cinfo.ppEnabledLayerNames = validationLayers;
    }

    const char **extensions = malloc(sizeof(*extensions) * glfwExtensionCount + 1);
    memcpy(extensions, glfwExtensions, glfwExtensionCount * sizeof(*glfwExtensions));
    extensions[glfwExtensionCount] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    u32 extensionCount = glfwExtensionCount + 1;
#else
    const char **extensions = malloc(sizeof(*extensions) * glfwExtensionCount);
    memcpy(extensions, glfwExtensions, glfwExtensionCount * sizeof(*glfwExtensions));
    u32 extensionCount = glfwExtensionCount;
#endif
    cinfo.ppEnabledExtensionNames = extensions;
    cinfo.enabledExtensionCount = extensionCount;

    if (vkCreateInstance(&cinfo, NULL, instance) != VK_SUCCESS)
    {
        free(extensions);

        return ERROR_INITIALIZATION_FAILURE;
    }
    free(extensions);
    return ERROR_SUCCESS;
}

local int ApplicationCheckDevice(VkPhysicalDevice dev,
                                 VkSurfaceKHR surf,
                                 const char **extensionList,
                                 size_t extensionCount)
{
    VkQueueIndices indices;
    bool properIndices = GetDeviceQueueGraphicsAndPresentationIndices(dev, surf, &indices);
    if (properIndices &&
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
        puts("ERROR! Could not create window");
        returnValue = ERROR_INITIALIZATION_FAILURE;
        goto errorNoWin;
    }

    VkInstance instance;
    if (glfwCreateVkInstance(&instance, "Vulkan tutorial",
                             VK_MAKE_VERSION(0, 0, 0),
                             VK_API_VERSION_1_0))

    {
        puts("ERROR! could not create instance");
        returnValue = ERROR_INITIALIZATION_FAILURE;
        goto errorNoInstance;
    }

    VkSurfaceKHR surf;
    if (glfwCreateWindowSurface(instance, win, NULL, &surf) != VK_SUCCESS)
    {
        puts("NOT ABLE TO CREATE SURFACE");
        returnValue = ERROR_INITIALIZATION_FAILURE;
        goto errorNoSurf;
    }

    const char *extensionList[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkPhysicalDevice physdev = GetVkPhysicalDevice(instance, surf, extensionList,
                                                   countof(extensionList),
                                                   ApplicationCheckDevice);
    if (physdev == VK_NULL_HANDLE)
    {
        puts("NO SUITABLE DEVICE");
        returnValue = ERROR_INITIALIZATION_FAILURE;
        goto errorNoContext;
    }

    VkPhysicalDeviceFeatures features = {0};
    VkRenderContext rc;
    if (CreateVkRenderContext(physdev, &features, surf,
                              WIDTH, HEIGHT, &rc) != ERROR_SUCCESS)
    {
        puts("NOT ABLE TO CREATE DEVICE");
        returnValue = ERROR_INITIALIZATION_FAILURE;
        goto errorNoContext;
    }

    isize vertShaderSize;
    void *vertShaderCode = MapFileToROBuffer(VERT_SHADER_LOC, NULL, &vertShaderSize);
    if (!vertShaderCode)
    {
        puts("Could not find vertex shader");
    }

    VkShaderModule vertShader = CreateVkShaderModule(&rc, vertShaderCode, vertShaderSize - 1);
    if (vertShader == VK_NULL_HANDLE)
    {
        puts("Could not load vertex shader");
    }
    UnmapMappedBuffer(vertShaderCode, vertShaderSize);

    isize fragShaderSize;
    void *fragShaderCode = MapFileToROBuffer(FRAG_SHADER_LOC, NULL, &fragShaderSize);
    if (!fragShaderCode)
    {
        puts("Could not find fragment shader");
    }

    VkShaderModule fragShader = CreateVkShaderModule(&rc, fragShaderCode, fragShaderSize - 1);
    if (fragShader == VK_NULL_HANDLE)
    {
        puts("Could not load fragment shader");
    }
    UnmapMappedBuffer(fragShaderCode, fragShaderSize);

    VkRenderPass renderpass = CreateRenderPass(&rc);
    if (renderpass == VK_NULL_HANDLE)
    {
        returnValue = ERROR_INITIALIZATION_FAILURE;
        puts("Could not create render pa");
        goto errorRenderPass;
    }
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {0};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.vertexBindingDescriptionCount = 0;

    VkPipelineLayout layout;
    VkPipeline pipeline = CreateGraphicsPipeline(&rc, vertShader,
                                                 fragShader, renderpass,
                                                 &vertexInputInfo,
                                                 &layout);
    if (pipeline == VK_NULL_HANDLE)
    {
        puts("Could not create graphics pipeline");
        returnValue = ERROR_INITIALIZATION_FAILURE;
        goto errorPipeline;
    }

    vkDestroyShaderModule(rc.dev, vertShader, NULL);
    vkDestroyShaderModule(rc.dev, fragShader, NULL);

    VkFramebuffer *framebuffers = CreateFrameBuffers(&rc, renderpass);
    if (framebuffers == NULL)
    {
        puts("Could not create framebuffer");
        goto errorFramebuffers;
    }

    VkCommandPool commandPool = CreateCommandPool(&rc);
    if (framebuffers == VK_NULL_HANDLE)
    {
        puts("Could not create command pool");
        goto errorCommandPool;
    }

    VkCommandBuffer *commandBuffers = ApplicationSetupCommandBuffers(&rc, commandPool,
                                                                     renderpass, pipeline,
                                                                     framebuffers);

    if (commandBuffers == NULL)
    {
        puts("Could not properly set up command buffers");
        goto errorCommandBuffers;
    }

    Semaphores s;
    if (!ApplicationCreateSemaphores(&rc, &s, MAX_CONCURRENT_FRAMES))
    {
        puts("Could not get semaphores");
        goto errorSemaphores;
    }
    u32 frameCount = 0;
    while (!glfwWindowShouldClose(win))
    {
        u32 sindex = frameCount++ % s.count;
        glfwPollEvents();
        if (!ApplicationDrawImage(&rc, commandBuffers,
                                  s.imageAvailableSemaphores[sindex],
                                  s.renderFinishedSemaphores[sindex],
                                  s.fences[sindex]))
        {
            break;
        }
    }

    /* Since drawing is async just because we fall out of the loop doesn't mean
       the device isn't doing work which means deinitialization can fail. Yeah.
       so uhhh... don't let that happen */

    vkDeviceWaitIdle(rc.dev);
    /* Cleanup */
    for (u32 i = 0; i < s.count; i++)
    {
        vkDestroySemaphore(rc.dev, s.renderFinishedSemaphores[i], NULL);
        vkDestroySemaphore(rc.dev, s.imageAvailableSemaphores[i], NULL);
        vkDestroyFence(rc.dev, s.fences[i], NULL);
    }
    free(s.imageAvailableSemaphores);
    free(s.renderFinishedSemaphores);
    free(s.fences);
errorSemaphores:
    free(commandBuffers);
errorCommandBuffers:
    vkDestroyCommandPool(rc.dev, commandPool, NULL);

errorCommandPool:
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
