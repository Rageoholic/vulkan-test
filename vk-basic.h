#ifndef VK_BASIC_H
#define VK_BASIC_H

#include "rutils/def.h"
#include <vulkan/vulkan.h>

typedef struct VkQueueIndices
{
    u32 graphicsIndex;
    u32 presentIndex;
} VkQueueIndices;

typedef struct VkRenderContext
{
    VkDevice dev;
    VkQueue graphicsQueue;
    VkQueue presentQueue;
    VkQueueIndices indices;
} VkRenderContext;

typedef struct VkSwapchainData
{
    VkSwapchainKHR swapchain;
    u32 imageCount;
    VkImage *images;
    VkImageView *imageViews;
    VkExtent2D e;
    VkSurfaceFormatKHR format;

} VkSwapchainData;

typedef struct SwapChainSupportDetails
{
    VkSurfaceCapabilitiesKHR capabilities;
    u32 formatCount;
    VkSurfaceFormatKHR *formats;
    u32 modeCount;
    VkPresentModeKHR *presentModes;
} SwapChainSupportDetails;

typedef int (*SuitableDeviceCheck)(VkPhysicalDevice dev,
                                   VkSurfaceKHR surf,
                                   const char **expectedDeviceExtensions,
                                   size_t numExpectedExtensions);

VkPhysicalDevice GetVkPhysicalDevice(VkInstance instance, VkSurfaceKHR surf,
                                     const char **expectedDeviceExtensions,
                                     size_t numExpectedExtensions,
                                     SuitableDeviceCheck checkFun);

bool GetDeviceQueueGraphicsAndPresentationIndices(VkPhysicalDevice dev, VkSurfaceKHR surf, VkQueueIndices *indices);

bool CheckDeviceExtensionSupport(VkPhysicalDevice dev,
                                 const char **extensionList,
                                 size_t extensionCount);

SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice dev, VkSurfaceKHR surf);

void DeleteSwapChainSupportDetails(SwapChainSupportDetails details);

errcode CreateVkRenderContext(VkPhysicalDevice physdev,
                              VkPhysicalDeviceFeatures *df,
                              VkSurfaceKHR surf,
                              VkRenderContext *outrc);

void DestroyVkRenderContext(VkRenderContext *rc);

VkShaderModule CreateVkShaderModule(const VkRenderContext *rc,
                                    const void *shaderSource,
                                    usize shaderLen);

VkPipeline CreateGraphicsPipeline(const VkRenderContext *rc,
                                  const VkSwapchainData *data,
                                  VkShaderModule vertShader,
                                  VkShaderModule fragShader,
                                  VkRenderPass renderpass,
                                  VkPipelineVertexInputStateCreateInfo *vertexInputInfo,
                                  VkPipelineLayout *layout);

VkRenderPass CreateRenderPass(const VkRenderContext *rc, const VkSwapchainData *data);

VkFramebuffer *CreateFrameBuffers(const VkRenderContext *rc, const VkSwapchainData *data, VkRenderPass renderpass);

VkCommandPool CreateCommandPool(VkRenderContext *rc);

errcode CreateSwapchain(VkRenderContext *rc, VkPhysicalDevice physdev,
                        VkSurfaceKHR surf, u32 windowWidth,
                        u32 windowHeight, VkSwapchainData *out);

void DestroySwapChainData(VkRenderContext *rc, VkSwapchainData *data);
#endif
