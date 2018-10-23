/* Feature macros */

#define GLFW_INCLUDE_VULKAN

#include "rutils/math.h"

#include "rutils/string.h"
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#define WIDTH 800
#define HEIGHT 600

#define NULL_QUEUE_INDEX -1

typedef struct VkRenderContext
{
    VkDevice dev;
    VkQueue graphicsQueue;
    VkQueue presentQueue;
} VkRenderContext;

typedef struct VkQueueIndices
{
    bool graphicsSupport;
    u32 graphicsIndex;
    bool presentSupport;
    u32 presentIndex;
} VkQueueIndices;

typedef struct SwapChainSupportDetails
{
    VkSurfaceCapabilitiesKHR capabilities;
    u32 formatCount;
    VkSurfaceFormatKHR *formats;
    u32 modeCount;
    VkPresentModeKHR *presentModes;
} SwapChainSupportDetails;

errcode glfwCreateVkInstance(VkInstance *instance, const char *appName, u32 appVer, u32 apiVer)
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
    cinfo.ppEnabledExtensionNames = glfwExtensions;
    cinfo.enabledExtensionCount = glfwExtensionCount;

    if (vkCreateInstance(&cinfo, NULL, instance) != VK_SUCCESS)
    {

        return ERROR_INITIALIZATION_FAILURE;
    }
    return ERROR_SUCCESS;
}

typedef int (*SuitableDeviceCheck)(VkPhysicalDevice dev,
                                   VkSurfaceKHR surf,
                                   const char **expectedDeviceExtensions,
                                   size_t numExpectedExtensions);

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

VkQueueIndices GetDeviceQueueGraphicsAndPresentationIndices(VkPhysicalDevice dev, VkSurfaceKHR surf)
{
    u32 queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, NULL);

    VkQueueFamilyProperties pArr[queueFamilyCount];

    VkQueueIndices ret = {0};

    vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, pArr);
    for (u32 i = 0; i < queueFamilyCount; i++)
    {
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surf, &presentSupport);

        if (pArr[i].queueCount > 0 && presentSupport)
        {
            ret.presentSupport = true;
            ret.presentIndex = i;
        }

        if (pArr[i].queueCount > 0 && pArr[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            ret.graphicsSupport = true;
            ret.graphicsIndex = i;
        }
        if (ret.presentSupport && ret.graphicsSupport)
        {
            break;
        }
    }
    return ret;
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

int ApplicationCheckDevice(VkPhysicalDevice dev,
                           VkSurfaceKHR surf,
                           const char **extensionList,
                           size_t extensionCount)
{
    VkQueueIndices indices = GetDeviceQueueGraphicsAndPresentationIndices(dev, surf);
    if (indices.graphicsSupport && indices.presentSupport &&
        CheckDeviceExtensionSupport(dev, extensionList, extensionCount))
    {
        SwapChainSupportDetails sd = QuerySwapChainSupport(dev, surf);

        bool validSwapChain = sd.formats && sd.presentModes;

        DeleteSwapChainSupportDetails(sd);
        return validSwapChain;
    }
    return false;
}

errcode CreateVkRenderContext(VkPhysicalDevice physdev, VkPhysicalDeviceFeatures *df,
                              VkRenderContext *outrc, VkSurfaceKHR surf)
{
    VkRenderContext rc = {0};

    VkQueueIndices qi = GetDeviceQueueGraphicsAndPresentationIndices(physdev, surf);
    if (!qi.graphicsSupport || !qi.presentSupport)
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
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 2;
    dci.pQueueCreateInfos = qci;
    dci.pEnabledFeatures = df;
    /* TODO: validation */
    if (vkCreateDevice(physdev, &dci, NULL, &rc.dev) != VK_SUCCESS)
    {
        return ERROR_INITIALIZATION_FAILURE;
    }
    vkGetDeviceQueue(rc.dev, qi.graphicsIndex, 0, &rc.graphicsQueue);
    vkGetDeviceQueue(rc.dev, qi.presentIndex, 0, &rc.presentQueue);
    *outrc = rc;

    return ERROR_SUCCESS;
}

void DestroyVkRenderContext(VkRenderContext rc)
{
    vkDestroyDevice(rc.dev, NULL);
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
        fputs("ERROR! Could not create window\n", stderr);
        returnValue = ERROR_INITIALIZATION_FAILURE;
        goto errorNoWin;
    }

    VkInstance instance;
    if (glfwCreateVkInstance(&instance, "Vulkan tutorial",
                             VK_MAKE_VERSION(0, 0, 0),
                             VK_API_VERSION_1_0))

    {
        fputs("ERROR! could not create instance\n", stderr);
        returnValue = ERROR_INITIALIZATION_FAILURE;
        goto errorNoInstance;
    }

    VkSurfaceKHR surf;
    if (glfwCreateWindowSurface(instance, win, NULL, &surf) != VK_SUCCESS)
    {
        fputs("NOT ABLE TO CREATE SURFACE\n", stdout);
        returnValue = ERROR_INITIALIZATION_FAILURE;
        goto errorNoSurf;
    }

    const char *extensionList[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkPhysicalDevice physdev = GetVkPhysicalDevice(instance, surf, extensionList,
                                                   countof(extensionList),
                                                   ApplicationCheckDevice);
    if (physdev == VK_NULL_HANDLE)
    {
        fputs("NO SUITABLE DEVICE\n", stderr);
        returnValue = ERROR_INITIALIZATION_FAILURE;
        goto errorNoContext;
    }

    VkPhysicalDeviceFeatures features = {0};
    VkRenderContext rc;
    if (CreateVkRenderContext(physdev, &features, &rc, surf) != ERROR_SUCCESS)
    {
        fputs("NOT ABLE TO CREATE DEVICE\n", stderr);
        returnValue = ERROR_INITIALIZATION_FAILURE;
        goto errorNoContext;
    }

    while (!glfwWindowShouldClose(win))
    {
        glfwPollEvents();
    }

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
