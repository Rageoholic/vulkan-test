/* Feature macros */

#define GLFW_INCLUDE_VULKAN
#define _POSIX_C_SOURCE (199309L)

#include "features.h"
#include "rutils/debug.h"
#include "rutils/file.h"
#include "rutils/math.h"
#include "rutils/string.h"
#include "stb_image.h"
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
    Vec3f pos;
    Vec3f color;
    Vec2f uv;
} Vertex;

typedef struct Uniform
{
    Mat4f model;
    Mat4f view;
    Mat4f proj;
} Uniform;

typedef struct Semaphores
{
    VkSemaphore *imageAvailableSemaphores;
    VkSemaphore *renderFinishedSemaphores;
    VkFence *fences;
    u32 count;
} Semaphores;

typedef struct Texture
{
    VkImage image;
    VkDeviceMemory texMem;
    VkImageView imageView;
    VkSampler sampler;
    u32 x;
    u32 y;
    int bytesPerPixel;
} Texture;

local Vertex vertices[] = {
    {{-0.5, -0.5, 0}, {1, 0, 0}, {1, 0}},
    {{.5, -.5, 0}, {0, 1, 0}, {0, 0}},
    {{.5, .5, 0}, {0, 0, 1}, {0, 1}},
    {{-.5, .5, 0}, {1, 1, 1}, {1, 1}},
    {{-0.5, -0.5, -.5}, {1, 0, 0}, {1, 0}},
    {{.5, -.5, -.5}, {0, 1, 0}, {0, 0}},
    {{.5, .5, -.5}, {0, 0, 1}, {0, 1}},
    {{-.5, .5, -.5}, {1, 1, 1}, {1, 1}}};

local u16 indices[] = {0, 1, 2, 2, 3, 0,
                       4, 5, 6, 6, 7, 4};

local const char *validationLayers[] = {"VK_LAYER_LUNARG_standard_validation"};

local void CopyBufferToImage(LogicalDevice *ld, VkCommandPool bufferCommandPool, GPUBufferData *texBuf, Texture *tex)
{
    VkCommandBuffer commandBuffer = BeginSingleTimeCommandBuffer(ld, bufferCommandPool);
    VkBufferImageCopy region = {0};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = (VkOffset3D){0, 0, 0};
    region.imageExtent = (VkExtent3D){tex->x,
                                      tex->y,
                                      1};

    vkCmdCopyBufferToImage(commandBuffer, texBuf->buffer, tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    EndSingleTimeCommandBuffer(ld, bufferCommandPool, commandBuffer);
}

local errcode LoadTexture(LogicalDevice *ld,
                          const char *path, VkCommandPool bufferCommandPool,
                          Texture *tex)
{
    int x, y;
    void *image = stbi_load(path, &x, &y, &tex->bytesPerPixel, STBI_rgb_alpha);

    tex->x = x;
    tex->y = y;
    size_t imageSize = tex->x * tex->y * 4;

    if (!image)
    {
        return ERROR_NO_MEMORY;
    }

    GPUBufferData texBuf;

    if (CreateGPUBufferData(ld, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                            &texBuf) != VK_SUCCESS)
    {
        return ERROR_NO_MEMORY;
    }

    OutputDataToBuffer(ld, &texBuf, image, imageSize, 0);

    if (!CreateVkImage(ld, tex->x, tex->y, VK_FORMAT_R8G8B8A8_UNORM,
                       VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                       &tex->image, &tex->texMem))
    {
        return ERROR_EXTERNAL_LIB;
    }

    TransitionImageLayout(ld, bufferCommandPool, tex->image,
                          VK_FORMAT_R8G8B8A8_UNORM,
                          VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    CopyBufferToImage(ld, bufferCommandPool, &texBuf, tex);

    TransitionImageLayout(ld, bufferCommandPool, tex->image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    DestroyGPUBufferInfo(ld, &texBuf);

    if (!CreateImageView(ld, tex->image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, &tex->imageView))
    {
        return ERROR_EXTERNAL_LIB;
    }

    VkSamplerCreateInfo samplerInfo = {0};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(ld->dev, &samplerInfo, NULL, &tex->sampler) != VK_SUCCESS)
    {
        return ERROR_EXTERNAL_LIB;
    }

    return ERROR_SUCCESS;
}

local VkCommandBuffer *ApplicationSetupCommandBuffers(LogicalDevice *ld, RenderContext *rc,
                                                      VkCommandPool commandPool, VkRenderPass renderpass,
                                                      VkPipeline graphicsPipeline, VkFramebuffer *framebuffers,
                                                      GPUBufferData *vertexBuffer, VkDeviceSize *offsets,
                                                      GPUBufferData *indexBuffer, VkDeviceSize indexOffset,
                                                      VkPipelineLayout pipelineLayout, VkDescriptorSet *descriptorSets)
{
    VkCommandBuffer *ret = malloc(sizeof(VkCommandBuffer) * rc->imageCount);

    VkCommandBufferAllocateInfo allocInfo = {0};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = rc->imageCount;

    if (vkAllocateCommandBuffers(ld->dev, &allocInfo, ret) != VK_SUCCESS)
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

        VkClearValue clearValues[2] = {0};
        clearValues[0].color = (VkClearColorValue){.1, .1, .1, 1};
        clearValues[1].depthStencil = (VkClearDepthStencilValue){1, 0};
        renderPassInfo.clearValueCount = countof(clearValues);
        renderPassInfo.pClearValues = clearValues;

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

local DrawResult ApplicationDrawImage(LogicalDevice *ld, RenderContext *rc,
                                      Uniform *u,
                                      GPUBufferData *uniformBuffers,
                                      GPUBufferData *uniformStagingBuffer,
                                      VkCommandPool bufferCommandPool,
                                      VkCommandBuffer *commandBuffers, VkSemaphore imageSemaphore,
                                      VkSemaphore renderSemaphore, VkFence fence)
{
    vkWaitForFences(ld->dev, 1, &fence, VK_TRUE, UINT64_MAX);

    u32 imageIndex;
    VkResult result = vkAcquireNextImageKHR(ld->dev, rc->swapchain, UINT64_MAX, imageSemaphore, NULL, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        return SWAP_CHAIN_OUT_OF_DATE;
    }
    OutputDataToBuffer(ld, uniformStagingBuffer, u, sizeof(*u), 0);

    CopyGPUBuffer(ld, &uniformBuffers[imageIndex], uniformStagingBuffer, sizeof(*u), 0, 0, bufferCommandPool);

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
    vkResetFences(ld->dev, 1, &fence);

    if (vkQueueSubmit(ld->graphicsQueue, 1, &submitInfo, fence) != VK_SUCCESS)
    {
        return NO_SUBMIT;
    }

    VkPresentInfoKHR presentInfo = {0};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderSemaphore;

    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &rc->swapchain;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(ld->presentQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        return SWAP_CHAIN_OUT_OF_DATE;
    }
    return NO_ERROR;
}

local bool ApplicationCreateSemaphores(LogicalDevice *ld, Semaphores *out, u32 semaphoreCount)
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
        if (vkCreateSemaphore(ld->dev, &semaphoreInfo, NULL,
                              &out->imageAvailableSemaphores[i]) != VK_SUCCESS)
        {
            return false;
        }

        if (vkCreateSemaphore(ld->dev, &semaphoreInfo, NULL,
                              &out->renderFinishedSemaphores[i]) != VK_SUCCESS)
        {
            return false;
        }

        if (vkCreateFence(ld->dev, &fenceInfo, NULL, &out->fences[i]) != VK_SUCCESS)
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
    QueueIndices qindices;
    bool properIndices = GetDeviceQueueGraphicsAndPresentationIndices(dev, surf, &qindices);

    VkPhysicalDeviceProperties devProps;

    vkGetPhysicalDeviceProperties(dev, &devProps);
    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceFeatures(dev, &features);

    if (!(devProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ||
          devProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU))
    {
        return false;
    }

    if (properIndices &&
        CheckDeviceExtensionSupport(dev, extensionList, extensionCount) &&
        features.samplerAnisotropy)
    {
        SwapChainSupportDetails sd = QuerySwapChainSupport(dev, surf);

        bool validSwapChain = sd.formats && sd.presentModes;

        DeleteSwapChainSupportDetails(sd);
        return validSwapChain;
    }
    return false;
}

local void ApplicationDestroyRenderContextAndRelatedData(LogicalDevice *ld, RenderContext *rc,
                                                         VkCommandPool cpool,
                                                         VkCommandBuffer *cbuffers,
                                                         VkFramebuffer *framebuffers,
                                                         VkPipeline pipeline, VkPipelineLayout layout,
                                                         VkRenderPass renderpass,
                                                         DepthResources *dr)
{
    for (u32 i = 0; i < rc->imageCount; i++)
    {
        vkDestroyFramebuffer(ld->dev, framebuffers[i], NULL);
    }
    free(framebuffers);
    vkFreeCommandBuffers(ld->dev, cpool, rc->imageCount, cbuffers);
    free(cbuffers);
    vkDestroyPipeline(ld->dev, pipeline, NULL);
    vkDestroyPipelineLayout(ld->dev, layout, NULL);
    vkDestroyRenderPass(ld->dev, renderpass, NULL);
    DestroyDepthResources(ld, dr);
    DestroySwapChainData(ld, rc);
}

local bool ApplicationRecreateRenderContextData(LogicalDevice *ld, RenderContext *rc, GLFWwindow *win,
                                                VkSurfaceKHR surf, GPUBufferData *vertexBuffers, VkDeviceSize *offsets,
                                                GPUBufferData *indexBuffer, VkDeviceSize indexOffset,
                                                VkCommandPool cpool,
                                                VkCommandPool tempCommandPool,
                                                VkShaderModule vertShader, VkShaderModule fragShader,
                                                VkDescriptorSetLayout descriptorSetLayouts,
                                                VkDescriptorSet *descriptorSets,
                                                VkPipelineVertexInputStateCreateInfo *inputInfo,
                                                VkCommandBuffer **cbuffers,
                                                VkFramebuffer **framebuffers,
                                                DepthResources *dr,
                                                VkPipeline *pipeline, VkPipelineLayout *layout,
                                                VkRenderPass *renderpass)
{
    if (PROFILING)
    {
        puts("RECREATE SWAPCHAIN");
    }
    vkDeviceWaitIdle(ld->dev);
    ApplicationDestroyRenderContextAndRelatedData(ld, rc, cpool, *cbuffers,
                                                  *framebuffers, *pipeline, *layout,
                                                  *renderpass, dr);
    int wwidth, wheight;
    glfwGetWindowSize(win, &wwidth, &wheight);
    if (CreateRenderContext(ld, surf, wwidth, wheight, rc) != ERROR_SUCCESS)
    {
        return false;
    }

    if (!CreateDepthResources(ld, rc, tempCommandPool, dr))
    {
        return false;
    }
    *renderpass = CreateRenderPass(ld, rc, dr);
    *pipeline = CreateGraphicsPipeline(ld, rc,
                                       vertShader, fragShader,
                                       *renderpass,
                                       &descriptorSetLayouts, 1,
                                       inputInfo, dr, layout);

    *framebuffers = CreateFrameBuffers(ld, rc, *renderpass, dr);
    *cbuffers = ApplicationSetupCommandBuffers(ld, rc, cpool,
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
        fprintf(stderr, "validation layer: %s\n\n", callbackData->pMessage);
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
    features.samplerAnisotropy = VK_TRUE;
    LogicalDevice ld;
    if (CreateLogicalDevice(physdev, &features, surf,
                            &ld) != ERROR_SUCCESS)
    {
        puts("NOT ABLE TO CREATE DEVICE");
        returnValue = ERROR_INITIALIZATION_FAILURE;
        return returnValue;
    }

    int wwidth, wheight;
    glfwGetWindowSize(win, &wwidth, &wheight);

    RenderContext rc = {0};
    if (CreateRenderContext(&ld, surf, wwidth, wheight, &rc) != ERROR_SUCCESS)
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

    VkShaderModule vertShader = CreateVkShaderModule(&ld, vertShaderCode, vertShaderSize - 1);
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

    VkShaderModule fragShader = CreateVkShaderModule(&ld, fragShaderCode, fragShaderSize - 1);
    if (fragShader == VK_NULL_HANDLE)
    {
        puts("Could not load fragment shader");
    }
    UnmapMappedBuffer(fragShaderCode, fragShaderSize);

    VkCommandPool commandPool = CreateCommandPool(&ld, 0);
    if (commandPool == VK_NULL_HANDLE)
    {
        puts("Could not create command pool");
        return returnValue;
    }

    VkCommandPool tempCommandPool = CreateCommandPool(&ld, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
    if (commandPool == VK_NULL_HANDLE)
    {
        puts("Could not create command pool");
        return returnValue;
    }

    DepthResources depthResources;
    if (!CreateDepthResources(&ld, &rc, tempCommandPool, &depthResources))
    {
        puts("Could not create depth resources");
    }

    VkRenderPass renderpass = CreateRenderPass(&ld, &rc, &depthResources);
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
    VkVertexInputAttributeDescription attributeDescription[3] = {0};

    attributeDescription[0].binding = 0;
    attributeDescription[0].location = 0;
    attributeDescription[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescription[0].offset = offsetof(Vertex, pos);

    attributeDescription[1].binding = 0;
    attributeDescription[1].location = 1;
    attributeDescription[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescription[1].offset = offsetof(Vertex, color);

    attributeDescription[2].binding = 0;
    attributeDescription[2].location = 2;
    attributeDescription[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescription[2].offset = offsetof(Vertex, uv);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {0};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexAttributeDescriptionCount = countof(attributeDescription);
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescription;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;

    VkDescriptorSetLayoutBinding layoutBindings[2] = {0};
    layoutBindings[0].binding = 0;
    layoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layoutBindings[0].descriptorCount = 1;
    layoutBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    layoutBindings[1].binding = 1;
    layoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutBindings[1].descriptorCount = 1;
    layoutBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {0};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = countof(layoutBindings);
    layoutInfo.pBindings = layoutBindings;
    VkDescriptorSetLayout descriptorSetLayout;

    if (vkCreateDescriptorSetLayout(ld.dev, &layoutInfo, NULL, &descriptorSetLayout) != VK_SUCCESS)
    {
        puts("Error. Could not make descriptor set layout");
        return 1;
    }

    VkPipelineLayout layout;
    VkPipeline pipeline = CreateGraphicsPipeline(&ld, &rc,
                                                 vertShader, fragShader, renderpass,
                                                 &descriptorSetLayout, 1,
                                                 &vertexInputInfo, &depthResources, &layout);
    if (pipeline == VK_NULL_HANDLE)
    {
        puts("Could not create graphics pipeline");
        returnValue = ERROR_INITIALIZATION_FAILURE;
        return returnValue;
    }

    VkFramebuffer *framebuffers = CreateFrameBuffers(&ld, &rc, renderpass, &depthResources);
    if (framebuffers == NULL)
    {
        puts("Could not create framebuffer");
        return returnValue;
    }

    GPUBufferData stagingBuffer;

    if (CreateGPUBufferData(&ld, sizeof(vertices),
                            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            &stagingBuffer) != VK_SUCCESS)
    {
        puts("Could not set up staging buffer");
        return 1;
    }

    OutputDataToBuffer(&ld, &stagingBuffer, vertices, sizeof(vertices), 0);

    GPUBufferData vertexBuffer;
    if (CreateGPUBufferData(&ld, sizeof(vertices),
                            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &vertexBuffer) !=
        VK_SUCCESS)
    {
        puts("Could not set up vertex buffer");
        return 1;
    }

    CopyGPUBuffer(&ld, &vertexBuffer, &stagingBuffer, sizeof(vertices), 0, 0, tempCommandPool);

    DestroyGPUBufferInfo(&ld, &stagingBuffer);

    if (CreateGPUBufferData(&ld, sizeof(indices),
                            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            &stagingBuffer) != VK_SUCCESS)
    {
        puts("Could not set up staging buffer 2");
        return 1;
    }

    OutputDataToBuffer(&ld, &stagingBuffer, indices, sizeof(indices), 0);

    GPUBufferData indexBuffer;
    if (CreateGPUBufferData(&ld, sizeof(indices),
                            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &indexBuffer) !=
        VK_SUCCESS)
    {
        puts("Could not set up index buffer");
        return 1;
    }

    CopyGPUBuffer(&ld, &indexBuffer, &stagingBuffer, sizeof(indices), 0, 0, tempCommandPool);
    DestroyGPUBufferInfo(&ld, &stagingBuffer);

    GPUBufferData *uniformBuffers = malloc(sizeof(*uniformBuffers) * rc.imageCount);

    for (u32 i = 0; i < rc.imageCount; i++)
    {
        if (CreateGPUBufferData(&ld, sizeof(Uniform),
                                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &uniformBuffers[i]) !=
            VK_SUCCESS)
        {
            puts("could not set up uniform buffers");
            return 1;
        }
    }

    GPUBufferData uniformStagingBuffer;
    if (CreateGPUBufferData(&ld, sizeof(Uniform), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformStagingBuffer) != VK_SUCCESS)
    {
        puts("could not set up uniform buffers");
        return 1;
    }

    VkDescriptorPoolSize poolSizes[2] = {0};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = rc.imageCount;

    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = rc.imageCount;

    VkDescriptorPool descriptorPool = CreateDescriptorPool(&ld, &rc, countof(poolSizes), poolSizes);
    if (!descriptorPool)
    {
        puts("Error could not create descriptor pool");
        return 1;
    }

    Texture tex = {0};
    if (LoadTexture(&ld, "textures/container.jpg", tempCommandPool, &tex) != ERROR_SUCCESS)
    {
        puts("Couldn't load texture");
        return 1;
    }

    VkDescriptorSet *descriptorSets = AllocateDescriptorSets(&ld, &rc,
                                                             descriptorPool, uniformBuffers,
                                                             descriptorSetLayout, sizeof(Uniform),
                                                             tex.imageView, tex.sampler);

    if (descriptorSets == NULL)
    {
        puts("error. Couldn't allocate descriptor sets");
        return 1;
    }
    VkDeviceSize offsets[1] = {0};

    VkCommandBuffer *commandBuffers =
        ApplicationSetupCommandBuffers(
            &ld, &rc, commandPool,
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
    if (!ApplicationCreateSemaphores(&ld, &s, MAX_CONCURRENT_FRAMES))
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
            .proj = CreatePerspectiveMat4f(DegToRad(45), rc.e.width / (float)rc.e.height, .1f, 10),
            .view = CalcLookAtMat4f(vec3f(2, 2, 2), vec3f(0, 0, 0), vec3f(0, 0, 1)),
            .model = RotateMat4f(&IdMat4f, totalTime * DegToRad(90), vec3f(0, 0, 1)),
        };
        u.proj.e[1][1] = -1;

        /* render */
        DrawResult result = ApplicationDrawImage(&ld, &rc, &u,
                                                 uniformBuffers, &uniformStagingBuffer, commandPool,
                                                 commandBuffers, s.imageAvailableSemaphores[sindex],
                                                 s.renderFinishedSemaphores[sindex], s.fences[sindex]);

        if (result == SWAP_CHAIN_OUT_OF_DATE || resizeOccurred)
        {
            ApplicationRecreateRenderContextData(&ld, &rc, win, surf,
                                                 &vertexBuffer, offsets,
                                                 &indexBuffer, 0,
                                                 commandPool, tempCommandPool,
                                                 vertShader, fragShader,
                                                 descriptorSetLayout,
                                                 descriptorSets,
                                                 &vertexInputInfo, &commandBuffers,
                                                 &framebuffers, &depthResources, &pipeline,
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
            double frameEndTime = glfwGetTime();
            printf("frame %10" PRIu32 " took %f milliseconds\n",
                   frameCount, (frameEndTime - frameStartTime) * 1000);
        }
    }

    /* Since drawing is async just because we fall out of the loop doesn't mean
       the device isn't doing work which means deinitialization can fail. Yeah.
       so uhhh... don't let that happen */

    vkDeviceWaitIdle(ld.dev);
    /* Cleanup */

    vkDestroyDescriptorPool(ld.dev, descriptorPool, NULL);

    vkDestroyShaderModule(ld.dev, vertShader, NULL);
    vkDestroyShaderModule(ld.dev, fragShader, NULL);

    for (u32 i = 0; i < s.count; i++)
    {
        vkDestroySemaphore(ld.dev, s.renderFinishedSemaphores[i], NULL);
        vkDestroySemaphore(ld.dev, s.imageAvailableSemaphores[i], NULL);
        vkDestroyFence(ld.dev, s.fences[i], NULL);
    }
    free(s.imageAvailableSemaphores);
    free(s.renderFinishedSemaphores);
    free(s.fences);
    u32 imageCount = rc.imageCount;

    ApplicationDestroyRenderContextAndRelatedData(&ld, &rc, commandPool, commandBuffers,
                                                  framebuffers, pipeline, layout,
                                                  renderpass, &depthResources);

    vkDestroyDescriptorSetLayout(ld.dev, descriptorSetLayout, NULL);
    for (u32 i = 0; i < imageCount; i++)
    {
        DestroyGPUBufferInfo(&ld, &uniformBuffers[i]);
    }
    DestroyGPUBufferInfo(&ld, &uniformStagingBuffer);

    DestroyGPUBufferInfo(&ld, &vertexBuffer);
    DestroyGPUBufferInfo(&ld, &indexBuffer);

    vkDestroySampler(ld.dev, tex.sampler, NULL);
    vkDestroyImageView(ld.dev, tex.imageView, NULL);
    vkFreeMemory(ld.dev, tex.texMem, NULL);
    vkDestroyImage(ld.dev, tex.image, NULL);

    vkDestroyCommandPool(ld.dev, commandPool, NULL);
    vkDestroyCommandPool(ld.dev, tempCommandPool, NULL);

    DestroyLogicalDevice(&ld);

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
