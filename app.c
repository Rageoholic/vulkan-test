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

local bool resizeOccurred;

typedef enum DrawResult
{
    NO_ERROR,
    SWAP_CHAIN_OUT_OF_DATE,
    NO_SUBMIT
} DrawResult;

typedef struct Vertex
{
    Vec2f pos;
    Vec3f color;
} Vertex;

local Vertex vertices[] = {

    {{0.0, -0.5}, {1, 0, 0}},
    {{0.5, 0.5}, {0, 1, 0}},
    {{-0.5, 0.5}, {0, 0, 1}}};

local const char *validationLayers[] = {"VK_LAYER_LUNARG_standard_validation"};

local VkCommandBuffer *ApplicationSetupCommandBuffers(VkRenderContext *rc, VkSwapchainData *data,
                                                      VkCommandPool commandPool, VkRenderPass renderpass,
                                                      VkPipeline graphicsPipeline, VkFramebuffer *framebuffers,
                                                      GPUBufferData *vertexBuffer, VkDeviceSize *offsets)
{
    VkCommandBuffer *ret = malloc(sizeof(VkCommandBuffer) * data->imageCount);

    VkCommandBufferAllocateInfo allocInfo = {0};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = data->imageCount;

    if (vkAllocateCommandBuffers(rc->dev, &allocInfo, ret) != VK_SUCCESS)
    {
        free(ret);
        return NULL;
    }

    for (u32 i = 0; i < data->imageCount; i++)
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
        renderPassInfo.renderArea.extent = data->e;

        VkClearValue clearColor = {0, 0, 0, 1};
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearColor;

        vkCmdBeginRenderPass(ret[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        {
            vkCmdBindPipeline(ret[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
            vkCmdBindVertexBuffers(ret[i], 0, 1, &vertexBuffer->buffer, offsets);
            vkCmdBindVertexBuffers(ret[i], 0, 1, &vertexBuffer->buffer, offsets);
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

local DrawResult ApplicationDrawImage(VkRenderContext *rc, VkSwapchainData *data,
                                      VkCommandBuffer *commandBuffers, VkSemaphore imageSemaphore,
                                      VkSemaphore renderSemaphore, VkFence fence)
{

    vkWaitForFences(rc->dev, 1, &fence, VK_TRUE, UINT64_MAX);

    u32 imageIndex;
    VkResult result = vkAcquireNextImageKHR(rc->dev, data->swapchain, UINT64_MAX, imageSemaphore, NULL, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        return SWAP_CHAIN_OUT_OF_DATE;
    }

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
    vkResetFences(rc->dev, 1, &fence);

    if (vkQueueSubmit(rc->graphicsQueue, 1, &submitInfo, fence) != VK_SUCCESS)
    {
        return NO_SUBMIT;
    }

    VkPresentInfoKHR presentInfo = {0};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderSemaphore;

    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &data->swapchain;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(rc->presentQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        return SWAP_CHAIN_OUT_OF_DATE;
    }
    return NO_ERROR;
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

local void
ApplicationDestroySwapchainAndRelatedData(VkRenderContext *rc, VkSwapchainData *data,
                                          VkCommandPool cpool,
                                          VkCommandBuffer *cbuffers,
                                          VkFramebuffer *framebuffers,
                                          VkPipeline pipeline, VkPipelineLayout layout,
                                          VkRenderPass renderpass)
{

    for (u32 i = 0; i < data->imageCount; i++)
    {
        vkDestroyFramebuffer(rc->dev, framebuffers[i], NULL);
    }
    free(framebuffers);

    vkFreeCommandBuffers(rc->dev, cpool, data->imageCount, cbuffers);
    free(cbuffers);

    vkDestroyPipeline(rc->dev, pipeline, NULL);
    vkDestroyPipelineLayout(rc->dev, layout, NULL);

    vkDestroyRenderPass(rc->dev, renderpass, NULL);

    DestroySwapChainData(rc, data);
}
local bool ApplicationRecreateSwapchain(VkRenderContext *rc, VkSwapchainData *data, GLFWwindow *win,
                                        VkPhysicalDevice physdev, VkSurfaceKHR surf, GPUBufferData *vertexBuffers,
                                        VkDeviceSize *offsets, VkCommandPool cpool,
                                        VkShaderModule vertShader, VkShaderModule fragShader,
                                        VkPipelineVertexInputStateCreateInfo *inputInfo,
                                        VkCommandBuffer **cbuffers,
                                        VkFramebuffer **framebuffers,
                                        VkPipeline *pipeline, VkPipelineLayout *layout,
                                        VkRenderPass *renderpass)

{
    vkDeviceWaitIdle(rc->dev);
    ApplicationDestroySwapchainAndRelatedData(rc, data, cpool, *cbuffers,
                                              *framebuffers, *pipeline, *layout,
                                              *renderpass);
    int wwidth, wheight;
    glfwGetWindowSize(win, &wwidth, &wheight);
    if (CreateSwapchain(rc, physdev, surf, wwidth, wheight, data) != ERROR_SUCCESS)
    {
        return false;
    }
    *renderpass = CreateRenderPass(rc, data);
    *pipeline = CreateGraphicsPipeline(rc, data, vertShader, fragShader, *renderpass, inputInfo, layout);
    *framebuffers = CreateFrameBuffers(rc, data, *renderpass);
    *cbuffers = ApplicationSetupCommandBuffers(rc, data, cpool,
                                               *renderpass, *pipeline,
                                               *framebuffers, vertexBuffers, offsets);

    return true;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *callbackData,
    void *userData)
{
    ignore messageSeverity;
    ignore messageType;
    ignore userData;
    if (!streq(callbackData->pMessage, "Added messenger"))
    {
        fprintf(stderr, "validation layer: %s\n", callbackData->pMessage);
    }
    return VK_FALSE;
}
static VkResult CreateDebugUtilsMessenger(VkInstance instance,
                                          const VkDebugUtilsMessengerCreateInfoEXT *createInfo,
                                          VkDebugUtilsMessengerEXT *callback)
{
    PFN_vkCreateDebugUtilsMessengerEXT func = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != NULL)
    {
        return func(instance, createInfo, NULL, callback);
    }
    else
    {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

static VkResult ApplicationSetupDebugCallback(VkInstance instance,
                                              VkDebugUtilsMessengerEXT *callback)
{
    VkDebugUtilsMessengerCreateInfoEXT createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = DebugCallback;

    return CreateDebugUtilsMessenger(instance, &createInfo, callback);
}

static void ResizeCallback(GLFWwindow *win, int width, int height)
{
    ignore win;
    ignore width;
    ignore height;
    resizeOccurred = true;
}

static void DestroyDebugUtilsMessenger(VkInstance instance, VkDebugUtilsMessengerEXT callback)
{
    PFN_vkDestroyDebugUtilsMessengerEXT func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != NULL)
    {
        func(instance, callback, NULL);
    }
}

int main(int argc, char **argv)
{
    int returnValue = ERROR_SUCCESS;
    ignore argc, ignore argv;

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    GLFWwindow *win = glfwCreateWindow(WIDTH, HEIGHT, "vulkan", NULL, NULL);
    if (win == NULL)
    {
        puts("ERROR! Could not create window");
        returnValue = ERROR_INITIALIZATION_FAILURE;
        return returnValue;
    }
    glfwSetFramebufferSizeCallback(win, ResizeCallback);

    VkInstance instance;
    if (glfwCreateVkInstance(&instance, "Vulkan tutorial",
                             VK_MAKE_VERSION(0, 0, 0),
                             VK_API_VERSION_1_0))

    {
        puts("ERROR! could not create instance");
        returnValue = ERROR_INITIALIZATION_FAILURE;
        return returnValue;
    }
    VkDebugUtilsMessengerEXT callback = VK_NULL_HANDLE;

    ApplicationSetupDebugCallback(instance, &callback);
    VkSurfaceKHR surf;
    if (glfwCreateWindowSurface(instance, win, NULL, &surf) != VK_SUCCESS)
    {
        puts("NOT ABLE TO CREATE SURFACE");
        returnValue = ERROR_INITIALIZATION_FAILURE;
        return returnValue;
    }

    const char *extensionList[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkPhysicalDevice physdev = GetVkPhysicalDevice(instance, surf, extensionList,
                                                   countof(extensionList),
                                                   ApplicationCheckDevice);
    if (physdev == VK_NULL_HANDLE)
    {
        puts("NO SUITABLE DEVICE");
        returnValue = ERROR_INITIALIZATION_FAILURE;
        return returnValue;
    }

    VkPhysicalDeviceFeatures features = {0};
    VkRenderContext rc;
    if (CreateVkRenderContext(physdev, &features, surf,
                              &rc) != ERROR_SUCCESS)
    {
        puts("NOT ABLE TO CREATE DEVICE");
        returnValue = ERROR_INITIALIZATION_FAILURE;
        return returnValue;
    }

    int wwidth, wheight;
    glfwGetWindowSize(win, &wwidth, &wheight);

    VkSwapchainData swapchainData = {0};
    if (CreateSwapchain(&rc, physdev, surf, wwidth, wheight, &swapchainData) != ERROR_SUCCESS)
    {
        puts("NOT ABLE TO CREATE SWAPCHAIN");
        returnValue = ERROR_INITIALIZATION_FAILURE;
        return returnValue;
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

    VkRenderPass renderpass = CreateRenderPass(&rc, &swapchainData);
    if (renderpass == VK_NULL_HANDLE)
    {
        returnValue = ERROR_INITIALIZATION_FAILURE;
        puts("Could not create render pa");
        return returnValue;
    }

    VkVertexInputBindingDescription bindingDescription = {0};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attributeDescription[2] = {0};

    attributeDescription[0].binding = 0;
    attributeDescription[0].location = 0;
    attributeDescription[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescription[0].offset = offsetof(Vertex, pos);

    attributeDescription[1].binding = 0;
    attributeDescription[1].location = 1;
    attributeDescription[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescription[1].offset = offsetof(Vertex, color);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {0};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexAttributeDescriptionCount = countof(attributeDescription);
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescription;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;

    VkPipelineLayout layout;
    VkPipeline pipeline = CreateGraphicsPipeline(&rc, &swapchainData,
                                                 vertShader, fragShader,
                                                 renderpass, &vertexInputInfo,
                                                 &layout);
    if (pipeline == VK_NULL_HANDLE)
    {
        puts("Could not create graphics pipeline");
        returnValue = ERROR_INITIALIZATION_FAILURE;
        return returnValue;
    }

    VkFramebuffer *framebuffers = CreateFrameBuffers(&rc, &swapchainData, renderpass);
    if (framebuffers == NULL)
    {
        puts("Could not create framebuffer");
        return returnValue;
    }

    VkCommandPool commandPool = CreateCommandPool(&rc);
    if (commandPool == VK_NULL_HANDLE)
    {
        puts("Could not create command pool");
        return returnValue;
    }

    GPUBufferData vertexBuffer;

    if (CreateGPUBufferData(&rc, physdev, sizeof(vertices), &vertexBuffer) != VK_SUCCESS)
    {
        puts("Could not set up vertex buffer");
        return 1;
    }

    VkDeviceSize offsets[1] = {0};

    VkCommandBuffer *commandBuffers = ApplicationSetupCommandBuffers(&rc, &swapchainData, commandPool,
                                                                     renderpass, pipeline,
                                                                     framebuffers,
                                                                     &vertexBuffer, offsets);

    if (commandBuffers == NULL)
    {
        puts("Could not properly set up command buffers");
        return returnValue;
    }
    OutputDataToBuffer(&rc, &vertexBuffer, vertices, sizeof(vertices), 0);

    Semaphores s;
    if (!ApplicationCreateSemaphores(&rc, &s, MAX_CONCURRENT_FRAMES))
    {
        puts("Could not get semaphores");
        return returnValue;
    }
    u32 frameCount = 0;
    while (!glfwWindowShouldClose(win))
    {
        u32 sindex = frameCount++ % s.count;
        glfwPollEvents();
        DrawResult result = ApplicationDrawImage(&rc, &swapchainData, commandBuffers,
                                                 s.imageAvailableSemaphores[sindex],
                                                 s.renderFinishedSemaphores[sindex],
                                                 s.fences[sindex]);
        if (result == SWAP_CHAIN_OUT_OF_DATE || resizeOccurred)
        {
            ApplicationRecreateSwapchain(&rc, &swapchainData, win, physdev, surf,
                                         &vertexBuffer, offsets,
                                         commandPool, vertShader, fragShader,
                                         &vertexInputInfo, &commandBuffers,
                                         &framebuffers, &pipeline, &layout,
                                         &renderpass);
        }
    }

    /* Since drawing is async just because we fall out of the loop doesn't mean
       the device isn't doing work which means deinitialization can fail. Yeah.
       so uhhh... don't let that happen */

    vkDeviceWaitIdle(rc.dev);
    /* Cleanup */

    vkDestroyShaderModule(rc.dev, vertShader, NULL);
    vkDestroyShaderModule(rc.dev, fragShader, NULL);

    for (u32 i = 0; i < s.count; i++)
    {
        vkDestroySemaphore(rc.dev, s.renderFinishedSemaphores[i], NULL);
        vkDestroySemaphore(rc.dev, s.imageAvailableSemaphores[i], NULL);
        vkDestroyFence(rc.dev, s.fences[i], NULL);
    }
    free(s.imageAvailableSemaphores);
    free(s.renderFinishedSemaphores);
    free(s.fences);

    ApplicationDestroySwapchainAndRelatedData(&rc, &swapchainData, commandPool, commandBuffers,
                                              framebuffers, pipeline, layout,
                                              renderpass);

    DestroyGPUBufferInfo(&rc, &vertexBuffer);

    vkDestroyCommandPool(rc.dev, commandPool, NULL);

    DestroyVkRenderContext(&rc);

    vkDestroySurfaceKHR(instance, surf, NULL);
    if (callback != VK_NULL_HANDLE)
    {
        DestroyDebugUtilsMessenger(instance, callback);
    }

    vkDestroyInstance(instance, NULL);

    glfwDestroyWindow(win);

    glfwTerminate();

    return returnValue;
}
