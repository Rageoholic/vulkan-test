#ifndef VK_BASIC_H
#define VK_BASIC_H

#include "rutils/def.h"
#include <string.h>
#include <vulkan/vulkan.h>

typedef struct QueueIndices
{
    u32 graphicsIndex;
    u32 presentIndex;
} QueueIndices;

typedef struct LogicalDevice
{
    VkDevice dev;
    VkQueue graphicsQueue;
    VkQueue presentQueue;
    QueueIndices indices;
} LogicalDevice;

typedef struct RenderContext
{
    VkSwapchainKHR swapchain;
    u32 imageCount;
    VkImage *images;
    VkImageView *imageViews;
    VkExtent2D e;
    VkSurfaceFormatKHR format;

} RenderContext;

typedef struct SwapChainSupportDetails
{
    VkSurfaceCapabilitiesKHR capabilities;
    u32 formatCount;
    VkSurfaceFormatKHR *formats;
    u32 modeCount;
    VkPresentModeKHR *presentModes;
} SwapChainSupportDetails;

typedef struct GPUBufferData
{
    VkBuffer buffer;
    VkDeviceMemory deviceMemory;
} GPUBufferData;

typedef int (*SuitableDeviceCheck)(VkPhysicalDevice dev,
                                   VkSurfaceKHR surf,
                                   const char **expectedDeviceExtensions,
                                   size_t numExpectedExtensions);

VkPhysicalDevice GetVkPhysicalDevice(VkInstance instance, VkSurfaceKHR surf,
                                     const char **expectedDeviceExtensions,
                                     size_t numExpectedExtensions,
                                     SuitableDeviceCheck checkFun);

bool GetDeviceQueueGraphicsAndPresentationIndices(VkPhysicalDevice dev, VkSurfaceKHR surf, QueueIndices *indices);

bool CheckDeviceExtensionSupport(VkPhysicalDevice dev,
                                 const char **extensionList,
                                 size_t extensionCount);

SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice dev, VkSurfaceKHR surf);

void DeleteSwapChainSupportDetails(SwapChainSupportDetails details);

errcode CreateLogicalDevice(VkPhysicalDevice physdev,
                            VkPhysicalDeviceFeatures *df,
                            VkSurfaceKHR surf,
                            LogicalDevice *outld);

void DestroyLogicalDevice(LogicalDevice *ld);

VkShaderModule CreateVkShaderModule(const LogicalDevice *ld,
                                    const void *shaderSource,
                                    usize shaderLen);

VkPipeline CreateGraphicsPipeline(const LogicalDevice *ld,
                                  const RenderContext *rc,
                                  VkShaderModule vertShader,
                                  VkShaderModule fragShader,
                                  VkRenderPass renderpass,
                                  VkDescriptorSetLayout *descriptorSets,
                                  u32 descriptorSetsCount,
                                  VkPipelineVertexInputStateCreateInfo *vertexInputInfo,
                                  VkPipelineLayout *layout);

VkRenderPass CreateRenderPass(const LogicalDevice *ld, const RenderContext *rc);

VkFramebuffer *CreateFrameBuffers(const LogicalDevice *ld, const RenderContext *rc, VkRenderPass renderpass);

VkCommandPool CreateCommandPool(LogicalDevice *ld, VkCommandPoolCreateFlags flags);

errcode CreateRenderContext(LogicalDevice *ld, VkPhysicalDevice physdev,
                            VkSurfaceKHR surf, u32 windowWidth,
                            u32 windowHeight, RenderContext *out);

void DestroySwapChainData(LogicalDevice *ld, RenderContext *rc);

VkResult CreateGPUBufferData(LogicalDevice *ld, VkPhysicalDevice physdev,
                             size_t vertexBufferSize,
                             VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                             GPUBufferData *buffer);

void DestroyGPUBufferInfo(LogicalDevice *ld, GPUBufferData *buffer);

local void OutputDataToBuffer(LogicalDevice *ld, GPUBufferData *buffer, void *data, size_t dataLen, size_t offset)
{
    void *bufp;
    vkMapMemory(ld->dev, buffer->deviceMemory, offset, dataLen, 0, &bufp);
    memcpy(bufp, data, dataLen);
    vkUnmapMemory(ld->dev, buffer->deviceMemory);
}

void CopyGPUBuffer(LogicalDevice *ld,
                   GPUBufferData *dest, GPUBufferData *src,
                   VkDeviceSize size, VkDeviceSize offsetDest,
                   VkDeviceSize offsetSrc, VkCommandPool commandPool);
VkDescriptorPool CreateDescriptorPool(LogicalDevice *ld, RenderContext *swapchain, VkDescriptorType type);

VkDescriptorSet *AllocateDescriptorSets(LogicalDevice *ld, RenderContext *rc,
                                        VkDescriptorPool descriptorPool,
                                        GPUBufferData *buffers, VkDescriptorSetLayout layout,
                                        VkDeviceSize typeSize);

#endif
