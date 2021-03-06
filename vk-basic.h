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
    VkPhysicalDevice physdev;
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

typedef struct DepthResources
{
    VkImage image;
    VkDeviceMemory mem;
    VkImageView view;
    VkFormat format;
} DepthResources;

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
                                  const RenderContext *data,
                                  VkShaderModule vertShader,
                                  VkShaderModule fragShader,
                                  VkRenderPass renderpass,
                                  VkDescriptorSetLayout *descriptorSetLayouts,
                                  u32 descriptorSetsCount,
                                  VkPipelineVertexInputStateCreateInfo *vertexInputInfo,
                                  DepthResources *dr,
                                  VkPipelineLayout *layout);

VkRenderPass CreateRenderPass(const LogicalDevice *ld, const RenderContext *data, const DepthResources *dr);

VkFramebuffer *CreateFrameBuffers(const LogicalDevice *ld, const RenderContext *data, VkRenderPass renderpass,
                                  const DepthResources *dr);

VkCommandPool CreateCommandPool(LogicalDevice *ld, VkCommandPoolCreateFlags flags);

errcode CreateRenderContext(LogicalDevice *ld,
                            VkSurfaceKHR surf, u32 windowWidth,
                            u32 windowHeight, RenderContext *out);

void DestroySwapChainData(LogicalDevice *ld, RenderContext *rc);

VkResult CreateGPUBufferData(LogicalDevice *ld,
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

VkDescriptorPool CreateDescriptorPool(LogicalDevice *ld, RenderContext *swapchain,
                                      u32 poolSizeCount, VkDescriptorPoolSize *poolSizes);

VkDescriptorSet *AllocateDescriptorSets(LogicalDevice *ld, RenderContext *data,
                                        VkDescriptorPool descriptorPool,
                                        GPUBufferData *buffers, VkDescriptorSetLayout layout,
                                        VkDeviceSize typeSize, VkImageView imageView, VkSampler sampler);

bool FindMemoryType(VkPhysicalDevice physdev, u32 typefilter,
                    VkMemoryPropertyFlags properties, u32 *out);

void DestroyDepthResources(LogicalDevice *ld, DepthResources *dr);

bool CreateImageView(LogicalDevice *ld, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags,
                     VkImageView *out);

bool CreateVkImage(LogicalDevice *ld, u32 x, u32 y, VkFormat format, VkImageUsageFlags usage,
                   VkImage *outImage, VkDeviceMemory *outMem);
void TransitionImageLayout(LogicalDevice *ld, VkCommandPool commandPool, VkImage image, VkFormat format,
                           VkImageLayout oldLayout, VkImageLayout newLayout);

VkCommandBuffer BeginSingleTimeCommandBuffer(LogicalDevice *ld, VkCommandPool commandPool);

void EndSingleTimeCommandBuffer(LogicalDevice *ld, VkCommandPool commandPool, VkCommandBuffer commandBuffer);
bool CreateDepthResources(LogicalDevice *ld, RenderContext *rc, VkCommandPool commandPool, DepthResources *out);
#endif
