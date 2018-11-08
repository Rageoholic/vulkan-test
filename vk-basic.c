#include "vk-basic.h"
#include "features.h"
#include "rutils/math.h"
#include "rutils/string.h"

local bool HasStencilComponent(VkFormat format)
{
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

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

bool GetDeviceQueueGraphicsAndPresentationIndices(VkPhysicalDevice dev, VkSurfaceKHR surf, QueueIndices *indices)
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

local VkExtent2D SelectSwapExtent(SwapChainSupportDetails *details, u32 windowWidth, u32 windowHeight)
{
    VkExtent2D ret;
    if (details->capabilities.currentExtent.width != UINT32_MAX)
    {
        ret = details->capabilities.currentExtent;
    }
    else
    {
        ret.width = windowWidth;
        ret.height = windowHeight;
    }
    return ret;
}

errcode CreateLogicalDevice(VkPhysicalDevice physdev,
                            VkPhysicalDeviceFeatures *df,
                            VkSurfaceKHR surf,
                            LogicalDevice *outld)
{
    LogicalDevice ld = {0};

    QueueIndices qi;
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
    if (vkCreateDevice(physdev, &dci, NULL, &ld.dev) != VK_SUCCESS)
    {
        return ERROR_INITIALIZATION_FAILURE;
    }
    vkGetDeviceQueue(ld.dev, qi.graphicsIndex, 0, &ld.graphicsQueue);
    if (qi.graphicsIndex != qi.presentIndex)
    {
        vkGetDeviceQueue(ld.dev, qi.presentIndex, 0, &ld.presentQueue);
    }
    else
    {
        ld.presentQueue = ld.graphicsQueue;
    }

    ld.indices = qi;
    ld.physdev = physdev;

    *outld = ld;

    return ERROR_SUCCESS;
}

errcode CreateRenderContext(LogicalDevice *ld,
                            VkSurfaceKHR surf, u32 windowWidth,
                            u32 windowHeight, RenderContext *out)

{
    SwapChainSupportDetails d = QuerySwapChainSupport(ld->physdev, surf);
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

    if (USE_MAILBOX_RENDERER)
    {
        for (u32 i = 0; i < d.modeCount; i++)
        {
            if (d.presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                pmode = VK_PRESENT_MODE_MAILBOX_KHR;
                break;
            }
        }
    }

    VkExtent2D e = SelectSwapExtent(&d, windowWidth, windowHeight);

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
    u32 queueFamilyIndices[2] = {ld->indices.graphicsIndex, ld->indices.presentIndex};

    if (ld->indices.graphicsIndex == ld->indices.presentIndex)
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

    if (vkCreateSwapchainKHR(ld->dev, &ci, NULL, &out->swapchain) != VK_SUCCESS)
    {
        return ERROR_EXTERNAL_LIB;
    }

    vkGetSwapchainImagesKHR(ld->dev, out->swapchain, &imageCount, NULL);
    out->images = malloc(sizeof(out->images[0]) * imageCount);
    vkGetSwapchainImagesKHR(ld->dev, out->swapchain, &imageCount, out->images);
    out->imageCount = imageCount;

    out->imageViews = malloc(sizeof(out->imageViews[0]) * out->imageCount);
    for (u32 i = 0; i < out->imageCount; i++)
    {
        VkImageViewCreateInfo ivci = {0};
        ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image = out->images[i];
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
        if (vkCreateImageView(ld->dev, &ivci, NULL, &out->imageViews[i]) != VK_SUCCESS)
        {
            free(out->images);
            free(out->imageViews);
            return ERROR_EXTERNAL_LIB;
        }
    }
    out->e = e;
    out->format = form;

    DeleteSwapChainSupportDetails(d);
    return ERROR_SUCCESS;
}

void DestroySwapChainData(LogicalDevice *ld, RenderContext *data)
{
    for (u32 i = 0; i < data->imageCount; i++)
    {
        vkDestroyImageView(ld->dev, data->imageViews[i], NULL);
    }
    free(data->imageViews);
    free(data->images);
    vkDestroySwapchainKHR(ld->dev, data->swapchain, NULL);
}

void DestroyLogicalDevice(LogicalDevice *ld)
{
    vkDestroyDevice(ld->dev, NULL);
}

VkShaderModule CreateVkShaderModule(const LogicalDevice *ld,
                                    const void *shaderSource,
                                    usize shaderLen)
{
    VkShaderModuleCreateInfo ci = {0};
    ci.codeSize = shaderLen;
    ci.pCode = shaderSource;
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    VkShaderModule mod;

    if (vkCreateShaderModule(ld->dev, &ci, NULL, &mod) != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }
    return mod;
}

VkPipeline CreateGraphicsPipeline(const LogicalDevice *ld,
                                  const RenderContext *data,
                                  VkShaderModule vertShader,
                                  VkShaderModule fragShader,
                                  VkRenderPass renderpass,
                                  VkDescriptorSetLayout *descriptorSetLayouts,
                                  u32 descriptorSetsCount,
                                  VkPipelineVertexInputStateCreateInfo *vertexInputInfo,
                                  DepthResources *dr,
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

    VkPipelineInputAssemblyStateCreateInfo piasci = {0};
    piasci.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    piasci.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    piasci.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport = {0};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = (float)data->e.width;
    viewport.height = (float)data->e.height;
    viewport.minDepth = 0;
    viewport.maxDepth = 1;

    VkRect2D scissor = {0};
    scissor.offset = (VkOffset2D){0, 0};
    scissor.extent = data->e;

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
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
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
    pci.setLayoutCount = descriptorSetsCount;
    pci.pSetLayouts = descriptorSetLayouts;

    if (vkCreatePipelineLayout(ld->dev, &pci, NULL, layout))
    {
        return VK_NULL_HANDLE;
    }
    VkPipelineDepthStencilStateCreateInfo depthStencil = {0};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkGraphicsPipelineCreateInfo pipelineInfo = {0};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &piasci;
    pipelineInfo.pViewportState = &vps;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = *layout;
    pipelineInfo.renderPass = renderpass;
    pipelineInfo.subpass = 0;
    pipelineInfo.pDepthStencilState = &depthStencil;

    VkPipeline graphicsPipeline;
    if (vkCreateGraphicsPipelines(ld->dev, VK_NULL_HANDLE, 1,
                                  &pipelineInfo, NULL, &graphicsPipeline) !=
        VK_SUCCESS)
    {
        vkDestroyPipelineLayout(ld->dev, *layout, NULL);
        return VK_NULL_HANDLE;
    }

    return graphicsPipeline;
}

VkRenderPass CreateRenderPass(const LogicalDevice *ld, const RenderContext *data, const DepthResources *dr)
{
    VkAttachmentDescription colorAttachment = {0};
    colorAttachment.format = data->format.format;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment = {0};
    depthAttachment.format = dr->format;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachRef = {0};
    colorAttachRef.attachment = 0;
    colorAttachRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachRef = {0};
    if (dr)
    {
        depthAttachRef.attachment = 1;
        depthAttachRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    VkSubpassDescription subpass = {0};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachRef;
    subpass.pDepthStencilAttachment = &depthAttachRef;

    VkSubpassDependency dependency = {0};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;

    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription arr[] = {colorAttachment, depthAttachment};

    VkRenderPassCreateInfo renderPassInfo = {0};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 2;
    renderPassInfo.pAttachments = arr;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    VkRenderPass renderPass;

    if (vkCreateRenderPass(ld->dev, &renderPassInfo, NULL, &renderPass) != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }
    return renderPass;
}

VkFramebuffer *CreateFrameBuffers(const LogicalDevice *ld, const RenderContext *data, VkRenderPass renderpass,
                                  const DepthResources *dr)
{
    VkFramebuffer *ret = malloc(sizeof(ret[0]) * data->imageCount);
    for (u32 i = 0; i < data->imageCount; i++)
    {
        VkImageView attachments[] = {data->imageViews[i], dr->view};

        VkFramebufferCreateInfo fbci = {0};
        fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass = renderpass;
        fbci.attachmentCount = countof(attachments);
        fbci.pAttachments = attachments;
        fbci.width = data->e.width;
        fbci.height = data->e.height;
        fbci.layers = 1;

        if (vkCreateFramebuffer(ld->dev, &fbci, NULL, &ret[i]) != VK_SUCCESS)
        {
            free(ret);
            return NULL;
        }
    }

    return ret;
}

VkCommandPool CreateCommandPool(LogicalDevice *ld, VkCommandPoolCreateFlags flags)
{

    VkCommandPoolCreateInfo poolInfo = {0};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = ld->indices.graphicsIndex;
    poolInfo.flags = flags;

    VkCommandPool ret;
    if (vkCreateCommandPool(ld->dev, &poolInfo, NULL, &ret) != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }
    return ret;
}

bool FindMemoryType(VkPhysicalDevice physdev, u32 typefilter,
                    VkMemoryPropertyFlags properties, u32 *out)
{
    VkPhysicalDeviceMemoryProperties memproperties;

    vkGetPhysicalDeviceMemoryProperties(physdev, &memproperties);
    for (u32 i = 0; i < memproperties.memoryTypeCount; i++)
    {
        if (typefilter & (i << i) && memproperties.memoryTypes[i].propertyFlags & properties)
        {
            *out = i;
            return true;
        }
    }
    return false;
}

VkResult CreateGPUBufferData(LogicalDevice *ld,
                             size_t vertexBufferSize,
                             VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                             GPUBufferData *buffer)

{
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkBufferCreateInfo bufferInfo = {0};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = vertexBufferSize;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(ld->dev, &bufferInfo, NULL, &vertexBuffer) != VK_SUCCESS)
    {

        return ERROR_EXTERNAL_LIB;
    }

    VkMemoryRequirements memReq = {0};
    vkGetBufferMemoryRequirements(ld->dev, vertexBuffer, &memReq);

    VkMemoryAllocateInfo allocInfo = {0};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    if (!FindMemoryType(ld->physdev, memReq.memoryTypeBits,
                        properties, &allocInfo.memoryTypeIndex))
    {

        return -1;
    }

    VkDeviceMemory vertexBufferMem;
    if (vkAllocateMemory(ld->dev, &allocInfo, NULL, &vertexBufferMem) != VK_SUCCESS)
    {

        return -1;
    }

    vkBindBufferMemory(ld->dev, vertexBuffer, vertexBufferMem, 0);
    *buffer = (GPUBufferData){vertexBuffer, vertexBufferMem};
    return VK_SUCCESS;
}

void DestroyGPUBufferInfo(LogicalDevice *ld, GPUBufferData *buffer)
{
    vkDestroyBuffer(ld->dev, buffer->buffer, NULL);
    vkFreeMemory(ld->dev, buffer->deviceMemory, NULL);
}

void CopyGPUBuffer(LogicalDevice *ld,
                   GPUBufferData *dest, GPUBufferData *src,
                   VkDeviceSize size, VkDeviceSize offsetDest,
                   VkDeviceSize offsetSrc, VkCommandPool commandPool)
{
    VkCommandBufferAllocateInfo allocInfo = {0};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(ld->dev, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo = {0};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    {
        VkBufferCopy copyRegion = {0};
        copyRegion.dstOffset = offsetDest;
        copyRegion.srcOffset = offsetSrc;
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, src->buffer, dest->buffer, 1, &copyRegion);
    }
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo = {0};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(ld->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(ld->graphicsQueue);

    vkFreeCommandBuffers(ld->dev, commandPool, 1, &commandBuffer);
}

VkDescriptorPool CreateDescriptorPool(LogicalDevice *ld, RenderContext *swapchain,
                                      u32 poolSizeCount, VkDescriptorPoolSize *poolSizes)
{

    VkDescriptorPoolCreateInfo poolInfo = {0};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = poolSizeCount;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = swapchain->imageCount;
    VkDescriptorPool ret;

    if (vkCreateDescriptorPool(ld->dev, &poolInfo, NULL, &ret) != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }
    return ret;
}

VkDescriptorSet *AllocateDescriptorSets(LogicalDevice *ld, RenderContext *data,
                                        VkDescriptorPool descriptorPool,
                                        GPUBufferData *buffers, VkDescriptorSetLayout layout,
                                        VkDeviceSize typeSize, VkImageView imageView, VkSampler sampler)
{
    VkDescriptorSet *ret = malloc(data->imageCount * sizeof(*ret));
    VkDescriptorSetLayout descriptorSetLayouts[data->imageCount];
    for (u32 i = 0; i < data->imageCount; i++)
    {
        descriptorSetLayouts[i] = layout;
    }

    VkDescriptorSetAllocateInfo allocInfo = {0};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = data->imageCount;
    allocInfo.pSetLayouts = descriptorSetLayouts;

    if (vkAllocateDescriptorSets(ld->dev, &allocInfo, ret) != VK_SUCCESS)
    {
        return NULL;
    }

    for (u32 i = 0; i < data->imageCount; i++)
    {
        VkDescriptorBufferInfo bufferInfo = {0};
        bufferInfo.buffer = buffers[i].buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = typeSize;

        VkDescriptorImageInfo imageInfo = {0};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = imageView;
        imageInfo.sampler = sampler;

        VkWriteDescriptorSet descriptorWrites[2] = {0};
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = ret[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = ret[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(ld->dev, countof(descriptorWrites), descriptorWrites, 0, NULL);
    }
    return ret;
}
bool CreateImageView(LogicalDevice *ld, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags,
                     VkImageView *out)
{
    VkImageViewCreateInfo viewInfo = {0};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(ld->dev, &viewInfo, NULL, out) != VK_SUCCESS)
    {
        return false;
    }

    return true;
}
bool FindSupportedFormat(LogicalDevice *ld, u32 candidateCount, VkFormat *candidates,
                         VkImageTiling tiling, VkFormatFeatureFlags features,
                         VkFormat *out)
{
    for (u32 i = 0; i < candidateCount; i++)
    {
        VkFormat format = candidates[i];

        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(ld->physdev, format, &props);
        if (tiling == VK_IMAGE_TILING_LINEAR &&
            (props.linearTilingFeatures & features) == features)
        {
            *out = format;
            return true;
        }
        else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
        {
            *out = format;
            return true;
        }
    }

    return false;
}

bool CreateVkImage(LogicalDevice *ld, u32 x, u32 y, VkFormat format, VkImageUsageFlags usage,
                   VkImage *outImage, VkDeviceMemory *outMem)
{
    VkImageCreateInfo imageInfo = {0};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = x;
    imageInfo.extent.height = y;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    if (vkCreateImage(ld->dev, &imageInfo, NULL, outImage) != VK_SUCCESS)
    {
        return false;
    }

    VkMemoryRequirements memReq = {0};
    vkGetImageMemoryRequirements(ld->dev, *outImage, &memReq);

    VkMemoryAllocateInfo allocInfo = {0};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    if (!FindMemoryType(ld->physdev, memReq.memoryTypeBits,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        &allocInfo.memoryTypeIndex))
    {
        return ERROR_EXTERNAL_LIB;
    }

    if (vkAllocateMemory(ld->dev, &allocInfo, NULL, outMem))
    {
        return false;
    }

    vkBindImageMemory(ld->dev, *outImage, *outMem, 0);
    return true;
}

bool CreateDepthResources(LogicalDevice *ld, RenderContext *rc, VkCommandPool commandPool, DepthResources *out)
{

    VkFormat candidates[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT};
    if (!FindSupportedFormat(ld, countof(candidates), candidates, VK_IMAGE_TILING_OPTIMAL,
                             VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, &out->format))
    {
        return false;
    }

    if (!CreateVkImage(ld, rc->e.width, rc->e.height, out->format,
                       VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                       &out->image, &out->mem))
    {
        return false;
    }

    if (!CreateImageView(ld, out->image, out->format, VK_IMAGE_ASPECT_DEPTH_BIT, &out->view))
    {
        return false;
    }

    TransitionImageLayout(ld, commandPool,
                          out->image, out->format,
                          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    return true;
}

void DestroyDepthResources(LogicalDevice *ld, DepthResources *dr)
{
    vkDestroyImage(ld->dev, dr->image, NULL);
    vkFreeMemory(ld->dev, dr->mem, NULL);
    vkDestroyImageView(ld->dev, dr->view, NULL);
}

void TransitionImageLayout(LogicalDevice *ld, VkCommandPool commandPool, VkImage image, VkFormat format,
                           VkImageLayout oldLayout, VkImageLayout newLayout)
{
    ignore format;
    VkCommandBuffer commandBuffer = BeginSingleTimeCommandBuffer(ld, commandPool);
    {
        VkImageMemoryBarrier barrier = {0};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;

        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        barrier.image = image;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = 0;

        if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        {
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

            if (HasStencilComponent(format))
            {
                barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
            }
        }
        else
        {
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        }

        VkPipelineStageFlags sourceStage;
        VkPipelineStageFlags destinationStage;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        }
        else
        {
            ERROR("Invalid state transition");
        }

        vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, NULL, 0, 0, 1, &barrier);
    }
    EndSingleTimeCommandBuffer(ld, commandPool, commandBuffer);
}

VkCommandBuffer BeginSingleTimeCommandBuffer(LogicalDevice *ld, VkCommandPool commandPool)
{
    VkCommandBufferAllocateInfo allocInfo = {0};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(ld->dev, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo = {0};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

void EndSingleTimeCommandBuffer(LogicalDevice *ld, VkCommandPool commandPool, VkCommandBuffer commandBuffer)
{
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo = {0};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(ld->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(ld->graphicsQueue);

    vkFreeCommandBuffers(ld->dev, commandPool, 1, &commandBuffer);
}
