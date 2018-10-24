#include "vk-basic.h"
#include "rutils/math.h"
#include "rutils/string.h"
#include <string.h>

VkPhysicalDevice GetVkPhysicalDevice(VkInstance instance, VkSurfaceKHR surf,
                                     const char **expectedDeviceExtensions,
                                     size_t numExpectedExtensions,
                                     SuitableDeviceCheck checkFun)
{
    u32 dcount = 0;
    vkEnumeratePhysicalDevices(instance, &dcount, NULL);
    VkPhysicalDevice devArr[dcount];
    vkEnumeratePhysicalDevices(instance, &dcount, devArr);

    for (u32 i = 0; i < dcount; i++)
    {
        if (checkFun(devArr[i], surf, expectedDeviceExtensions, numExpectedExtensions))
        {
            VkPhysicalDevice ret = devArr[i];
            return ret;
        }
    }

    return VK_NULL_HANDLE;
}

bool GetDeviceQueueGraphicsAndPresentationIndices(VkPhysicalDevice dev, VkSurfaceKHR surf, VkQueueIndices *indices)
{
    u32 queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, NULL);

    VkQueueFamilyProperties pArr[queueFamilyCount];

    bool presentSupport = false;
    bool graphicsSupport = false;

    vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, pArr);
    for (u32 i = 0; i < queueFamilyCount; i++)
    {
        VkBool32 presentSupportVK = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surf, &presentSupportVK);

        if (pArr[i].queueCount > 0 && presentSupportVK)
        {
            indices->presentIndex = i;
            presentSupport = true;
        }

        if (pArr[i].queueCount > 0 && pArr[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            indices->graphicsIndex = i;
            graphicsSupport = true;
        }
        if (presentSupport && graphicsSupport)
        {
            return true;
        }
    }
    return false;
}

bool CheckDeviceExtensionSupport(VkPhysicalDevice dev,
                                 const char **extensionList,
                                 size_t extensionCount)
{
    u32 devExtensionCount = 0;
    vkEnumerateDeviceExtensionProperties(dev, NULL, &devExtensionCount, NULL);
    VkExtensionProperties extensionArr[devExtensionCount];
    vkEnumerateDeviceExtensionProperties(dev, NULL, &devExtensionCount, extensionArr);
    bool confirmArray[extensionCount];
    memset(confirmArray, 0, sizeof(confirmArray));

    for (u32 i = 0; i < devExtensionCount; i++)
    {
        for (size_t j = 0; j < extensionCount; j++)
        {
            if (!confirmArray[j] && streq(extensionList[j], extensionArr[i].extensionName))
            {
                confirmArray[j] = true;
            }
        }
    }

    bool ret = true;

    for (size_t i = 0; i < extensionCount; i++)
    {
        if (!confirmArray[i])
        {
            ret = false;
            break;
        }
    }

    return ret;
}

SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice dev, VkSurfaceKHR surf)
{
    SwapChainSupportDetails details = {0};

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev, surf, &details.capabilities);

    vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surf, &details.formatCount, NULL);
    if (details.formatCount != 0)
    {
        details.formats = malloc(details.formatCount * sizeof(details.formats[0]));
        vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surf, &details.formatCount, details.formats);
    }

    vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surf, &details.modeCount, NULL);
    if (details.modeCount != 0)
    {
        details.presentModes = malloc(details.modeCount * sizeof(details.presentModes[0]));
        vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surf, &details.modeCount, details.presentModes);
    }

    return details;
}

void DeleteSwapChainSupportDetails(SwapChainSupportDetails details)
{
    free(details.formats);
    free(details.presentModes);
}

errcode CreateVkRenderContext(VkPhysicalDevice physdev,
                              VkPhysicalDeviceFeatures *df,
                              VkSurfaceKHR surf,
                              u32 width, u32 height,
                              VkRenderContext *outrc)
{
    VkRenderContext rc = {0};

    VkQueueIndices qi;
    bool support = GetDeviceQueueGraphicsAndPresentationIndices(physdev, surf, &qi);
    if (!support)
    {
        return ERROR_INVAL_PARAMETER;
    }

    f32 queuePriority = 1.0f;

    VkDeviceQueueCreateInfo qci[2] = {0};
    qci[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci[0].queueFamilyIndex = qi.graphicsIndex;
    qci[0].queueCount = 1;
    qci[0].pQueuePriorities = &queuePriority;

    qci[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci[1].queueFamilyIndex = qi.presentIndex;
    qci[1].queueCount = 1;
    qci[1].pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo dci = {0};
    const char *extensionList[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    dci.enabledExtensionCount = countof(extensionList);
    dci.ppEnabledExtensionNames = extensionList;
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = qi.graphicsIndex == qi.presentIndex ? 1 : 2;
    dci.pQueueCreateInfos = qci;
    dci.pEnabledFeatures = df;

    /* TODO: validation */
    if (vkCreateDevice(physdev, &dci, NULL, &rc.dev) != VK_SUCCESS)
    {
        return ERROR_INITIALIZATION_FAILURE;
    }
    vkGetDeviceQueue(rc.dev, qi.graphicsIndex, 0, &rc.graphicsQueue);
    if (qi.graphicsIndex != qi.presentIndex)
    {
        vkGetDeviceQueue(rc.dev, qi.presentIndex, 0, &rc.presentQueue);
    }
    else
    {
        rc.presentQueue = rc.graphicsQueue;
    }
    SwapChainSupportDetails d = QuerySwapChainSupport(physdev, surf);
    VkSurfaceFormatKHR form = {0};
    if (d.formatCount == 1 && d.formats[0].format == VK_FORMAT_UNDEFINED)
    {
        form = (VkSurfaceFormatKHR){VK_FORMAT_B8G8R8A8_UNORM,
                                    VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    }
    else
    {
        bool found = false;
        for (u32 i = 0; i < d.formatCount; i++)
        {
            if (d.formats[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
                d.formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                form = d.formats[i];
                found = true;
                break;
            }
        }
        if (!found)
        {
            form = d.formats[0];
        }
    }
    VkPresentModeKHR pmode = VK_PRESENT_MODE_FIFO_KHR;
    for (u32 i = 0; i < d.modeCount; i++)
    {
        if (d.presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            pmode = VK_PRESENT_MODE_MAILBOX_KHR;
            break;
        }
        else if (d.presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)
        {
            pmode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        }
    }

    VkExtent2D e = {0};
    if (d.capabilities.currentExtent.width != UINT32_MAX)
    {
        e = d.capabilities.currentExtent;
    }
    else
    {
        e = (VkExtent2D){MAX_VAL(MIN_VAL(width, d.capabilities.minImageExtent.width),
                                 d.capabilities.maxImageExtent.width),
                         MAX_VAL(MIN_VAL(height, d.capabilities.minImageExtent.width),
                                 d.capabilities.currentExtent.height)};
    }

    u32 imageCount = d.capabilities.minImageCount + 1;
    if (d.capabilities.maxImageCount > 0 && imageCount > d.capabilities.maxImageCount)
    {
        imageCount = d.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR ci = {0};
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = surf;
    ci.minImageCount = imageCount;
    ci.imageFormat = form.format;
    ci.imageColorSpace = form.colorSpace;
    ci.imageExtent = e;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    u32 queueFamilyIndices[2] = {qi.graphicsIndex, qi.presentIndex};

    if (qi.graphicsIndex == qi.presentIndex)
    {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ci.queueFamilyIndexCount = 0;
        ci.pQueueFamilyIndices = NULL;
    }
    else
    {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices = queueFamilyIndices;
    }

    ci.preTransform = d.capabilities.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = pmode;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(rc.dev, &ci, NULL, &rc.swapchain) != VK_SUCCESS)
    {
        vkDestroyDevice(rc.dev, NULL);
        return ERROR_EXTERNAL_LIB;
    }

    vkGetSwapchainImagesKHR(rc.dev, rc.swapchain, &imageCount, NULL);
    rc.images = malloc(sizeof(rc.images) * imageCount);
    vkGetSwapchainImagesKHR(rc.dev, rc.swapchain, &imageCount, rc.images);
    rc.imageCount = imageCount;

    rc.imageViews = malloc(sizeof(rc.imageViews[0]) * rc.imageCount);
    for (u32 i = 0; i < rc.imageCount; i++)
    {
        VkImageViewCreateInfo ivci = {0};
        ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image = rc.images[i];
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format = ci.imageFormat;
        ivci.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.baseMipLevel = 0;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.baseArrayLayer = 0;
        ivci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(rc.dev, &ivci, NULL, &rc.imageViews[i]) != VK_SUCCESS)
        {
            vkDestroyDevice(rc.dev, NULL);
            free(rc.images);
            free(rc.imageViews);
            return ERROR_EXTERNAL_LIB;
        }
    }
    rc.e = e;
    rc.format = form;
    rc.indices = qi;

    DeleteSwapChainSupportDetails(d);

    *outrc = rc;

    return ERROR_SUCCESS;
}

void DestroyVkRenderContext(VkRenderContext rc)
{
    for (u32 i = 0; i < rc.imageCount; i++)
    {
        vkDestroyImageView(rc.dev, rc.imageViews[i], NULL);
    }
    free(rc.imageViews);
    free(rc.images);
    vkDestroySwapchainKHR(rc.dev, rc.swapchain, NULL);
    vkDestroyDevice(rc.dev, NULL);
}

VkShaderModule CreateVkShaderModule(const VkRenderContext *rc,
                                    const void *shaderSource,
                                    usize shaderLen)
{
    VkShaderModuleCreateInfo ci = {0};
    ci.codeSize = shaderLen;
    ci.pCode = shaderSource;
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    VkShaderModule mod;

    if (vkCreateShaderModule(rc->dev, &ci, NULL, &mod) != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }
    return mod;
}

VkPipeline CreateGraphicsPipeline(const VkRenderContext *rc,
                                  VkShaderModule vertShader,
                                  VkShaderModule fragShader,
                                  VkRenderPass renderpass,
                                  VkPipelineLayout *layout)
{
    VkPipelineShaderStageCreateInfo vssci = {0};
    vssci.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vssci.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vssci.module = vertShader;
    vssci.pName = "main";

    VkPipelineShaderStageCreateInfo fssci = {0};
    fssci.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fssci.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fssci.module = fragShader;
    fssci.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vssci, fssci};

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {0};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.vertexBindingDescriptionCount = 0;

    VkPipelineInputAssemblyStateCreateInfo piasci = {0};
    piasci.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    piasci.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    piasci.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport = {0};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = (float)rc->e.width;
    viewport.height = (float)rc->e.height;
    viewport.minDepth = 0;
    viewport.maxDepth = 1;

    VkRect2D scissor = {0};
    scissor.offset = (VkOffset2D){0, 0};
    scissor.extent = rc->e;

    VkPipelineViewportStateCreateInfo vps = {0};
    vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = 1;
    vps.pViewports = &viewport;
    vps.scissorCount = 1;
    vps.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer = {0};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling = {0};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment = {0};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                          VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT |
                                          VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending = {0};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0;
    colorBlending.blendConstants[1] = 0;
    colorBlending.blendConstants[2] = 0;
    colorBlending.blendConstants[3] = 0;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                      VK_DYNAMIC_STATE_LINE_WIDTH};

    VkPipelineDynamicStateCreateInfo dynamicState = {0};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = countof(dynamicStates);
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineLayoutCreateInfo pci = {0};
    pci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    if (vkCreatePipelineLayout(rc->dev, &pci, NULL, layout))
    {
        return VK_NULL_HANDLE;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo = {0};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &piasci;
    pipelineInfo.pViewportState = &vps;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = *layout;
    pipelineInfo.renderPass = renderpass;
    pipelineInfo.subpass = 0;

    VkPipeline graphicsPipeline;
    if (vkCreateGraphicsPipelines(rc->dev, VK_NULL_HANDLE, 1,
                                  &pipelineInfo, NULL, &graphicsPipeline) !=
        VK_SUCCESS)
    {
        vkDestroyPipelineLayout(rc->dev, *layout, NULL);
        return VK_NULL_HANDLE;
    }

    return graphicsPipeline;
}

VkRenderPass CreateRenderPass(const VkRenderContext *rc)
{
    VkAttachmentDescription colorAttachment = {0};
    colorAttachment.format = rc->format.format;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachRef = {0};
    colorAttachRef.attachment = 0;
    colorAttachRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {0};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachRef;

    VkSubpassDependency dependency = {0};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;

    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo = {0};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    VkRenderPass renderPass;

    if (vkCreateRenderPass(rc->dev, &renderPassInfo, NULL, &renderPass) != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }
    return renderPass;
}

VkFramebuffer *CreateFrameBuffers(VkRenderContext *rc, VkRenderPass renderpass)
{
    VkFramebuffer *ret = malloc(sizeof(ret[0]) * rc->imageCount);
    for (u32 i = 0; i < rc->imageCount; i++)
    {
        VkImageView attachment = rc->imageViews[i];

        VkFramebufferCreateInfo fbci = {0};
        fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass = renderpass;
        fbci.attachmentCount = 1;
        fbci.pAttachments = &attachment;
        fbci.width = rc->e.width;
        fbci.height = rc->e.height;
        fbci.layers = 1;

        if (vkCreateFramebuffer(rc->dev, &fbci, NULL, &ret[i]) != VK_SUCCESS)
        {
            free(ret);
            return NULL;
        }
    }

    return ret;
}

VkCommandPool CreateCommandPool(VkRenderContext *rc)
{

    VkCommandPoolCreateInfo poolInfo = {0};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = rc->indices.graphicsIndex;
    VkCommandPool ret;
    if (vkCreateCommandPool(rc->dev, &poolInfo, NULL, &ret) != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }
    return ret;
}

VkCommandBuffer *AllocateCommandBuffers(VkRenderContext *rc, VkCommandPool commandPool,
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
