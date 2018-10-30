/* Feature macros */

#define GLFW_INCLUDE_VULKAN
#define _POSIX_C_SOURCE (199309L)

#include "features.h"
#include "rutils/file.h"
#include "rutils/math.h"
#include "rutils/string.h"
#include "vk-basic.h"
#include <GLFW/glfw3.h>
#include <limits.h>
#include <time.h>

#define WIDTH 800
#define HEIGHT 600

#define VERT_SHADER_LOC "shaders/basic-shader.vert.spv"
#define FRAG_SHADER_LOC "shaders/basic-shader.frag.spv"
#define MAX_CONCURRENT_FRAMES 10

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

typedef struct Uniform
{
    Mat4f model;
    Mat4f view;
    Mat4f proj;
} Uniform;

local Vertex vertices[] = {
    {{-0.5, -0.5}, {1, 0, 0}},
    {{.5, -.5}, {0, 1, 0}},
    {{.5, .5}, {0, 0, 1}},
    {{-.5, .5}, {1, 1, 1}}};
local u16 indices[] = {0, 1, 2, 2, 3, 0};

local const char *validationLayers[] = {"VK_LAYER_LUNARG_standard_validation"};

local VkCommandBuffer *ApplicationSetupCommandBuffers(VkRenderContext *rc, VkSwapchainData *data,
                                                      VkCommandPool commandPool, VkRenderPass renderpass,
                                                      VkPipeline graphicsPipeline, VkFramebuffer *framebuffers,
                                                      GPUBufferData *vertexBuffer, VkDeviceSize *offsets,
                                                      GPUBufferData *indexBuffer, VkDeviceSize indexOffset,
                                                      VkPipelineLayout pipelineLayout, VkDescriptorSet *descriptorSets)
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

        VkClearValue clearColor = {.1f, .1f, .1f, 1};
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearColor;

        vkCmdBeginRenderPass(ret[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        {
            vkCmdBindPipeline(ret[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
            vkCmdBindVertexBuffers(ret[i], 0, 1, &vertexBuffer->buffer, offsets);
            vkCmdBindIndexBuffer(ret[i], indexBuffer->buffer, indexOffset, VK_INDEX_TYPE_UINT16);
            vkCmdBindDescriptorSets(ret[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                                    0, 1, &descriptorSets[i], 0, NULL);
            vkCmdDrawIndexed(ret[i], countof(indices), 1, 0, 0, 0);
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

local VkDescriptorPool CreateDescriptorPool(VkRenderContext *rc, VkSwapchainData *swapchain, VkDescriptorType type)
{
    ignore rc;
    VkDescriptorPoolSize poolSize = {0};
    poolSize.type = type;
    poolSize.descriptorCount = swapchain->imageCount;

    VkDescriptorPoolCreateInfo poolInfo = {0};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = swapchain->imageCount;
    VkDescriptorPool ret;

    if (vkCreateDescriptorPool(rc->dev, &poolInfo, NULL, &ret) != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
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
                                      Uniform *u,
                                      GPUBufferData *uniformBuffers,
                                      GPUBufferData *uniformStagingBuffer,
                                      VkCommandPool commandPool,
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
    OutputDataToBuffer(rc, uniformStagingBuffer, u, sizeof(*u), 0);

    CopyGPUBuffer(rc, &uniformBuffers[imageIndex], uniformStagingBuffer, sizeof(*u), 0, 0, commandPool);

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
    VkQueueIndices qindices;
    bool properIndices = GetDeviceQueueGraphicsAndPresentationIndices(dev, surf, &qindices);
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

local void ApplicationDestroySwapchainAndRelatedData(VkRenderContext *rc, VkSwapchainData *data,
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
                                        VkPhysicalDevice physdev, VkSurfaceKHR surf,
                                        GPUBufferData *vertexBuffers, VkDeviceSize *offsets,
                                        GPUBufferData *indexBuffer, VkDeviceSize indexOffset,
                                        VkCommandPool cpool,
                                        VkShaderModule vertShader, VkShaderModule fragShader,
                                        VkDescriptorSetLayout *descriptorSetLayouts,
                                        u32 descriptorSetCount,
                                        VkDescriptorSet *descriptorSets,
                                        VkPipelineVertexInputStateCreateInfo *inputInfo,
                                        VkCommandBuffer **cbuffers,
                                        VkFramebuffer **framebuffers,
                                        VkPipeline *pipeline, VkPipelineLayout *layout,
                                        VkRenderPass *renderpass)
{
    if (PROFILING)
    {
        puts("RECREATE SWAPCHAIN");
    }
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
    *pipeline = CreateGraphicsPipeline(rc, data,
                                       vertShader, fragShader,
                                       *renderpass,
                                       descriptorSetLayouts, descriptorSetCount,
                                       inputInfo, layout);
    *framebuffers = CreateFrameBuffers(rc, data, *renderpass);
    *cbuffers = ApplicationSetupCommandBuffers(rc, data, cpool,
                                               *renderpass, *pipeline,
                                               *framebuffers, vertexBuffers, offsets,
                                               indexBuffer, indexOffset, *layout, descriptorSets);
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
    /* glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE); */

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

    VkDescriptorSetLayoutBinding uboLayoutBinding = {0};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {0};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;
    VkDescriptorSetLayout descriptorSetLayout;

    if (vkCreateDescriptorSetLayout(rc.dev, &layoutInfo, NULL, &descriptorSetLayout) != VK_SUCCESS)
    {
        puts("Error. Could not make descriptor set layout");
        return 1;
    }

    VkPipelineLayout layout;
    VkPipeline pipeline = CreateGraphicsPipeline(&rc, &swapchainData,
                                                 vertShader, fragShader, renderpass,
                                                 &descriptorSetLayout, 1,
                                                 &vertexInputInfo,
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

    GPUBufferData stagingBuffer;

    if (CreateGPUBufferData(&rc, physdev, sizeof(vertices),
                            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            &stagingBuffer) != VK_SUCCESS)
    {
        puts("Could not set up staging buffer");
        return 1;
    }

    OutputDataToBuffer(&rc, &stagingBuffer, vertices, sizeof(vertices), 0);

    GPUBufferData vertexBuffer;
    if (CreateGPUBufferData(&rc, physdev, sizeof(vertices),
                            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &vertexBuffer) !=
        VK_SUCCESS)
    {
        puts("Could not set up vertex buffer");
        return 1;
    }

    CopyGPUBuffer(&rc, &vertexBuffer, &stagingBuffer, sizeof(vertices), 0, 0, commandPool);

    DestroyGPUBufferInfo(&rc, &stagingBuffer);

    if (CreateGPUBufferData(&rc, physdev, sizeof(indices),
                            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            &stagingBuffer) != VK_SUCCESS)
    {
        puts("Could not set up staging buffer 2");
        return 1;
    }

    OutputDataToBuffer(&rc, &stagingBuffer, indices, sizeof(indices), 0);

    GPUBufferData indexBuffer;
    if (CreateGPUBufferData(&rc, physdev, sizeof(indices),
                            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &indexBuffer) !=
        VK_SUCCESS)
    {
        puts("Could not set up index buffer");
        return 1;
    }

    CopyGPUBuffer(&rc, &indexBuffer, &stagingBuffer, sizeof(indices), 0, 0, commandPool);
    DestroyGPUBufferInfo(&rc, &stagingBuffer);

    GPUBufferData *uniformBuffers = malloc(sizeof(*uniformBuffers) * swapchainData.imageCount);

    for (u32 i = 0; i < swapchainData.imageCount; i++)
    {
        if (CreateGPUBufferData(&rc, physdev, sizeof(Uniform),
                                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &uniformBuffers[i]) !=
            VK_SUCCESS)
        {
            puts("could not set up uniform buffers");
            return 1;
        }
    }

    GPUBufferData uniformStagingBuffer;
    if (CreateGPUBufferData(&rc, physdev, sizeof(Uniform), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformStagingBuffer) != VK_SUCCESS)
    {
        puts("could not set up uniform buffers");
        return 1;
    }

    VkDescriptorPool descriptorPool = CreateDescriptorPool(&rc, &swapchainData, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    if (!descriptorPool)
    {
        puts("Error could not create descriptor pool");
        return 1;
    }

    VkDescriptorSet descriptorSets[swapchainData.imageCount];
    VkDescriptorSetLayout descriptorSetLayouts[swapchainData.imageCount];
    for (u32 i = 0; i < swapchainData.imageCount; i++)
    {
        descriptorSetLayouts[i] = descriptorSetLayout;
    }
    VkDescriptorSetAllocateInfo allocInfo = {0};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = swapchainData.imageCount;
    allocInfo.pSetLayouts = descriptorSetLayouts;

    if (vkAllocateDescriptorSets(rc.dev, &allocInfo, descriptorSets) != VK_SUCCESS)
    {
        puts("Could not allocate descriptor sets");
        return 1;
    }

    for (u32 i = 0; i < swapchainData.imageCount; i++)
    {
        VkDescriptorBufferInfo bufferInfo = {0};
        bufferInfo.buffer = uniformBuffers[i].buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(Uniform);

        VkWriteDescriptorSet descriptorWrite = {0};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descriptorSets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(rc.dev, 1, &descriptorWrite, 0, NULL);
    }

    VkDeviceSize offsets[1] = {0};

    VkCommandBuffer *commandBuffers =
        ApplicationSetupCommandBuffers(
            &rc, &swapchainData, commandPool,
            renderpass, pipeline,
            framebuffers,
            &vertexBuffer, offsets,
            &indexBuffer, 0, layout, descriptorSets);

    if (commandBuffers == NULL)
    {
        puts("Could not properly set up command buffers");
        return returnValue;
    }

    Semaphores s;
    if (!ApplicationCreateSemaphores(&rc, &s, MAX_CONCURRENT_FRAMES))
    {
        puts("Could not get semaphores");
        return returnValue;
    }
    u32 frameCount = 0;

    double lastFrameTime = glfwGetTime();
    float totalTime = 0;
    while (!glfwWindowShouldClose(win))
    {
        /* Input */
        double frameStartTime = glfwGetTime();
        float dt = (float)frameStartTime - (float)lastFrameTime;
        totalTime += dt;
        lastFrameTime = frameStartTime;

        struct timespec start;
        clock_gettime(CLOCK_REALTIME, &start);

        u32 sindex = frameCount++ % s.count;

        glfwPollEvents();

        /* Update */
        Uniform u = {
            .proj = CreatePerspectiveMat4f(DegToRad(45), swapchainData.e.width / (float)swapchainData.e.height, .1f, 10),
            .view = CalcLookAtMat4f(vec3f(2, 2, 2), vec3f(0, 0, 0), vec3f(0, 0, 1)),
            .model = RotateMat4f(&IdMat4f, totalTime * DegToRad(90), vec3f(0, 0, 1)),
        };
        u.proj.e[1][1] = -1;

        /* render */
        DrawResult result = ApplicationDrawImage(&rc, &swapchainData, &u,
                                                 uniformBuffers, &uniformStagingBuffer, commandPool,
                                                 commandBuffers, s.imageAvailableSemaphores[sindex],
                                                 s.renderFinishedSemaphores[sindex], s.fences[sindex]);
        if (result == SWAP_CHAIN_OUT_OF_DATE || resizeOccurred)
        {
            ApplicationRecreateSwapchain(&rc, &swapchainData, win, physdev, surf,
                                         &vertexBuffer, offsets,
                                         &indexBuffer, 0,
                                         commandPool, vertShader, fragShader,
                                         descriptorSetLayouts, 1,
                                         descriptorSets,
                                         &vertexInputInfo, &commandBuffers,
                                         &framebuffers, &pipeline,
                                         &layout, &renderpass);
            resizeOccurred = false;
        }

        struct timespec end;
        clock_gettime(CLOCK_REALTIME, &end);

        long timePassed;
        if (PROFILING)
        {
            if (end.tv_nsec > start.tv_nsec)
            {
                timePassed = end.tv_nsec - start.tv_nsec;
            }
            else
            {
                timePassed = 1000000000 - start.tv_nsec + end.tv_nsec;
            }
            timePassed /= 1000;
            printf("frame %10" PRIu32 " took %6ld microseconds\n", frameCount, timePassed);
        }
    }

    /* Since drawing is async just because we fall out of the loop doesn't mean
       the device isn't doing work which means deinitialization can fail. Yeah.
       so uhhh... don't let that happen */

    vkDeviceWaitIdle(rc.dev);
    /* Cleanup */
    vkDestroyDescriptorPool(rc.dev, descriptorPool, NULL);

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
    u32 imageCount = swapchainData.imageCount;

    ApplicationDestroySwapchainAndRelatedData(&rc, &swapchainData, commandPool, commandBuffers,
                                              framebuffers, pipeline, layout,
                                              renderpass);
    vkDestroyDescriptorSetLayout(rc.dev, descriptorSetLayout, NULL);
    for (u32 i = 0; i < imageCount; i++)
    {
        DestroyGPUBufferInfo(&rc, &uniformBuffers[i]);
    }
    DestroyGPUBufferInfo(&rc, &uniformStagingBuffer);

    DestroyGPUBufferInfo(&rc, &vertexBuffer);
    DestroyGPUBufferInfo(&rc, &indexBuffer);

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
