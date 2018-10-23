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
                                   const char **expectedDeviceExtensions,
                                   size_t numExpectedExtensions);

VkPhysicalDevice GetVkPhysicalDevice(VkInstance instance, const char **expectedDeviceExtensions,
                                     size_t numExpectedExtensions,
                                     SuitableDeviceCheck checkFun)
{
    u32 dcount = 0;
    vkEnumeratePhysicalDevices(instance, &dcount, NULL);
    VkPhysicalDevice *devArr = malloc(sizeof(*devArr) * dcount);
    vkEnumeratePhysicalDevices(instance, &dcount, devArr);

    for (u32 i = 0; i < dcount; i++)
    {
        if (checkFun(devArr[i], expectedDeviceExtensions, numExpectedExtensions))
        {
            VkPhysicalDevice ret = devArr[i];
            free(devArr);
            return ret;
        }
    }

    return VK_NULL_HANDLE;
}

i64 GetDeviceQueueGraphicsIndex(VkPhysicalDevice dev)
{
    u32 queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, NULL);

    VkQueueFamilyProperties *pArr = malloc(sizeof(VkQueueFamilyProperties) * queueFamilyCount);

    vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, pArr);
    for (u32 i = 0; i < queueFamilyCount; i++)
    {
        if (pArr[i].queueCount > 0 && pArr[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            free(pArr);
            return i;
        }
    }
    free(pArr);
    return NULL_QUEUE_INDEX;
}

VkQueueIndices GetDeviceQueueGraphicsAndPresentationIndices(VkPhysicalDevice dev, VkSurfaceKHR surf)
{
    u32 queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, NULL);

    VkQueueFamilyProperties *pArr = malloc(sizeof(VkQueueFamilyProperties) * queueFamilyCount);

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
    free(pArr);
    return ret;
}

bool CheckDeviceExtensionSupport(VkPhysicalDevice dev,
                                 const char **extensionList,
                                 size_t extensionCount)
{
    u32 devExtensionCount = 0;
    vkEnumerateDeviceExtensionProperties(dev, NULL, &devExtensionCount, NULL);
    VkExtensionProperties *extensionArr = malloc(sizeof(*extensionArr) * devExtensionCount);
    vkEnumerateDeviceExtensionProperties(dev, NULL, &devExtensionCount, extensionArr);
    bool *confirmArray = calloc(extensionCount, sizeof(bool));

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

    free(confirmArray);
    free(extensionArr);

    return ret;
}

int ApplicationCheckDevice(VkPhysicalDevice dev,
                           const char **extensionList,
                           size_t extensionCount)
{
    return (GetDeviceQueueGraphicsIndex(dev) != NULL_QUEUE_INDEX &&
            CheckDeviceExtensionSupport(dev, extensionList, extensionCount));
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
    ignore argc, ignore argv;

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow *win = glfwCreateWindow(WIDTH, HEIGHT, "vulkan", NULL, NULL);

    VkInstance instance;
    if (glfwCreateVkInstance(&instance, "Vulkan tutorial",
                             VK_MAKE_VERSION(0, 0, 0),
                             VK_API_VERSION_1_0))

    {
        fputs("ERROR! could not create instance", stderr);
        return 1;
    }

    VkSurfaceKHR surf;
    if (glfwCreateWindowSurface(instance, win, NULL, &surf) != VK_SUCCESS)
    {
        fputs("NOT ABLE TO CREATE SURFACE", stdout);
        return 1;
    }

    const char *extensionList[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkPhysicalDevice physdev = GetVkPhysicalDevice(instance, extensionList,
                                                   countof(extensionList),
                                                   ApplicationCheckDevice);
    if (physdev == VK_NULL_HANDLE)
    {
        fputs("NO SUITABLE DEVICE", stderr);
        return 1;
    }

    VkPhysicalDeviceFeatures features = {0};
    VkRenderContext rc;
    if (CreateVkRenderContext(physdev, &features, &rc, surf) != ERROR_SUCCESS)
    {
        fputs("NOT ABLE TO CREATE DEVICE", stderr);
        return 1;
    }

    while (!glfwWindowShouldClose(win))
    {
        glfwPollEvents();
    }

    vkDestroySurfaceKHR(instance, surf, NULL);
    DestroyVkRenderContext(rc);
    vkDestroyInstance(instance, NULL);
    glfwDestroyWindow(win);
    glfwTerminate();

    return EXIT_SUCCESS;
}
