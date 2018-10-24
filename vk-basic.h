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
    VkSwapchainKHR swapchain;
    u32 imageCount;
    VkImage *images;
    VkImageView *imageViews;
    VkExtent2D e;
    VkSurfaceFormatKHR format;
    VkQueueIndices indices;
} VkRenderContext;

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
                              u32 width, u32 height,
                              VkRenderContext *outrc);

void DestroyVkRenderContext(VkRenderContext rc);

VkShaderModule CreateVkShaderModule(const VkRenderContext *rc,
                                    const void *shaderSource,
                                    usize shaderLen);

VkPipeline CreateGraphicsPipeline(const VkRenderContext *rc,
                                  VkShaderModule vertShader,
                                  VkShaderModule fragShader,
                                  VkRenderPass renderpass,
                                  VkPipelineLayout *layout);

VkRenderPass CreateRenderPass(const VkRenderContext *rc);

VkFramebuffer *CreateFrameBuffers(VkRenderContext *rc, VkRenderPass renderpass);

VkCommandPool CreateCommandPool(VkRenderContext *rc);

VkCommandBuffer *AllocateCommandBuffers(VkRenderContext *rc, VkCommandPool commandPool,
                                        VkRenderPass renderpass, VkPipeline graphicsPipeline,
                                        VkFramebuffer *framebuffers);
#endif
