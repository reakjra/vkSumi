#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "config.hpp"
#include "logger.hpp"
#include "swapchain.hpp"
#include "vkdispatch.hpp"

#define VKSUMI_LAYER_NAME "VK_LAYER_vksumi_color_grading"
#define VKSUMI_EXPORT __attribute__((visibility("default")))

namespace vksumi
{
    static std::mutex                                  g_lock;
    static std::unordered_map<void*, InstanceDispatch> g_instanceDispatch;
    static std::unordered_map<void*, VkInstance>       g_instanceMap;

    using scoped_lock = std::lock_guard<std::mutex>;

    template<typename T>
    static void* GetKey(T handle)
    {
        return *reinterpret_cast<void**>(handle);
    }

    static VkLayerInstanceCreateInfo* findInstanceChainInfo(const VkInstanceCreateInfo* info)
    {
        auto* p = reinterpret_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(info->pNext));
        while (p && !(p->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && p->function == VK_LAYER_LINK_INFO))
            p = reinterpret_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(p->pNext));
        return p;
    }

    static VkLayerDeviceCreateInfo* findDeviceChainInfo(const VkDeviceCreateInfo* info)
    {
        auto* p = reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(info->pNext));
        while (p && !(p->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && p->function == VK_LAYER_LINK_INFO))
            p = reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(p->pNext));
        return p;
    }

    static VkResult VKAPI_CALL vksumi_CreateInstance(const VkInstanceCreateInfo*  pCreateInfo,
                                                     const VkAllocationCallbacks* pAllocator,
                                                     VkInstance*                  pInstance)
    {
        VKSUMI_TRACE("vkCreateInstance");

        VkLayerInstanceCreateInfo* chain = findInstanceChainInfo(pCreateInfo);
        if (!chain) return VK_ERROR_INITIALIZATION_FAILED;

        PFN_vkGetInstanceProcAddr nextGipa = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
        chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;

        auto createFunc = reinterpret_cast<PFN_vkCreateInstance>(nextGipa(VK_NULL_HANDLE, "vkCreateInstance"));
        if (!createFunc) return VK_ERROR_INITIALIZATION_FAILED;

        VkResult ret = createFunc(pCreateInfo, pAllocator, pInstance);
        if (ret != VK_SUCCESS) return ret;

        InstanceDispatch table{};
        fillInstanceDispatch(*pInstance, nextGipa, &table);

        {
            scoped_lock l(g_lock);
            g_instanceDispatch[GetKey(*pInstance)] = table;
            g_instanceMap     [GetKey(*pInstance)] = *pInstance;
        }

        // load config + start the watcher now that we know Vulkan is actually a thing here
        (void)currentConfig();
        startConfigWatcher();

        return VK_SUCCESS;
    }

    static void VKAPI_CALL vksumi_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator)
    {
        if (!instance) return;
        VKSUMI_TRACE("vkDestroyInstance");

        InstanceDispatch table;
        {
            scoped_lock l(g_lock);
            table = g_instanceDispatch[GetKey(instance)];
            g_instanceDispatch.erase(GetKey(instance));
            g_instanceMap.erase(GetKey(instance));
        }
        table.DestroyInstance(instance, pAllocator);
    }

    static void pickGraphicsQueue(const InstanceDispatch& vki,
                                  VkPhysicalDevice        phys,
                                  const VkDeviceCreateInfo* pCreateInfo,
                                  uint32_t&               outFamily,
                                  uint32_t&               outIndex)
    {
        uint32_t fcount = 0;
        vki.GetPhysicalDeviceQueueFamilyProperties(phys, &fcount, nullptr);
        std::vector<VkQueueFamilyProperties> props(fcount);
        vki.GetPhysicalDeviceQueueFamilyProperties(phys, &fcount, props.data());

        for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; ++i)
        {
            uint32_t f = pCreateInfo->pQueueCreateInfos[i].queueFamilyIndex;
            if (f < fcount && (props[f].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            {
                outFamily = f;
                outIndex  = 0;
                return;
            }
        }
        outFamily = UINT32_MAX;
        outIndex  = 0;
    }

    static VkResult VKAPI_CALL vksumi_CreateDevice(VkPhysicalDevice             physicalDevice,
                                                   const VkDeviceCreateInfo*    pCreateInfo,
                                                   const VkAllocationCallbacks* pAllocator,
                                                   VkDevice*                    pDevice)
    {
        VKSUMI_TRACE("vkCreateDevice");

        VkLayerDeviceCreateInfo* chain = findDeviceChainInfo(pCreateInfo);
        if (!chain) return VK_ERROR_INITIALIZATION_FAILED;

        PFN_vkGetInstanceProcAddr nextGipa = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
        PFN_vkGetDeviceProcAddr   nextGdpa = chain->u.pLayerInfo->pfnNextGetDeviceProcAddr;
        chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;

        auto createFunc = reinterpret_cast<PFN_vkCreateDevice>(nextGipa(VK_NULL_HANDLE, "vkCreateDevice"));
        if (!createFunc) return VK_ERROR_INITIALIZATION_FAILED;

        VkResult ret = createFunc(physicalDevice, pCreateInfo, pAllocator, pDevice);
        if (ret != VK_SUCCESS) return ret;

        DeviceState state{};
        state.device         = *pDevice;
        state.physicalDevice = physicalDevice;

        {
            scoped_lock l(g_lock);
            state.vki = g_instanceDispatch[GetKey(physicalDevice)];
        }
        fillDeviceDispatch(*pDevice, nextGdpa, &state.vkd);

        uint32_t qIdx = 0;
        pickGraphicsQueue(state.vki, physicalDevice, pCreateInfo, state.graphicsFamily, qIdx);
        if (state.graphicsFamily != UINT32_MAX)
            state.vkd.GetDeviceQueue(*pDevice, state.graphicsFamily, qIdx, &state.graphicsQueue);

        registerDevice(*pDevice, state);
        return VK_SUCCESS;
    }

    static void VKAPI_CALL vksumi_DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator)
    {
        if (!device) return;
        VKSUMI_TRACE("vkDestroyDevice");

        DeviceState* st = findDevice(device);
        if (!st) return;
        DeviceDispatch vkd = st->vkd;
        unregisterDevice(device);
        vkd.DestroyDevice(device, pAllocator);
    }

    static VkResult VKAPI_CALL vksumi_CreateSwapchainKHR(VkDevice                        device,
                                                         const VkSwapchainCreateInfoKHR* pCreateInfo,
                                                         const VkAllocationCallbacks*    pAllocator,
                                                         VkSwapchainKHR*                 pSwapchain)
    {
        DeviceState* st = findDevice(device);
        if (!st) return VK_ERROR_DEVICE_LOST;

        VKSUMI_TRACE("vkCreateSwapchainKHR %ux%u fmt=%d cs=%d",
                     pCreateInfo->imageExtent.width, pCreateInfo->imageExtent.height,
                     pCreateInfo->imageFormat, pCreateInfo->imageColorSpace);

        // HDR / wide gamut: our shader is SDR-only, running it on PQ or scRGB
        // values would just nuke the colors. bail to passthrough so the game keeps
        // working, vksumi just chills for this swapchain. proper HDR support is a
        // whole other project (decode PQ -> linear -> math -> re-encode), maybe later
        if (pCreateInfo->imageColorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            VKSUMI_LOG("HDR / non-sRGB swapchain (colorspace %d), passthrough only",
                       pCreateInfo->imageColorSpace);
            return st->vkd.CreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
        }

        // we render INTO the real swapchain so it needs COLOR_ATTACHMENT.
        // most apps already ask for it but be explicit
        VkSwapchainCreateInfoKHR ci = *pCreateInfo;
        ci.imageUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        VkResult r = st->vkd.CreateSwapchainKHR(device, &ci, pAllocator, pSwapchain);
        if (r != VK_SUCCESS) return r;

        auto state = SwapchainState::create(*st, *pSwapchain, ci);
        if (state) registerSwapchain(*pSwapchain, state);
        else       VKSUMI_LOG("hijack failed, this swapchain will passthrough");
        return r;
    }

    static void VKAPI_CALL vksumi_DestroySwapchainKHR(VkDevice                     device,
                                                      VkSwapchainKHR               swapchain,
                                                      const VkAllocationCallbacks* pAllocator)
    {
        // drop our shared_ptr first, otherwise our destructors run after the
        // device is already gone and everything blows up
        unregisterSwapchain(swapchain);
        DeviceState* st = findDevice(device);
        if (st) st->vkd.DestroySwapchainKHR(device, swapchain, pAllocator);
    }

    static VkResult VKAPI_CALL vksumi_GetSwapchainImagesKHR(VkDevice       device,
                                                            VkSwapchainKHR swapchain,
                                                            uint32_t*      pCount,
                                                            VkImage*       pImages)
    {
        if (auto s = findSwapchain(swapchain))
            return s->getImages(pCount, pImages);

        DeviceState* st = findDevice(device);
        if (!st) return VK_ERROR_DEVICE_LOST;
        return st->vkd.GetSwapchainImagesKHR(device, swapchain, pCount, pImages);
    }

    static VkResult VKAPI_CALL vksumi_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
    {
        DeviceState* st = findDeviceByQueue(queue);
        if (!st) return VK_ERROR_DEVICE_LOST;

        // submit our blit for each hijacked swapchain. ONLY the first submit
        // waits on the app's present semaphores cuz they're binary, one signal
        // = one wait. each submit signals one of our own that the present waits on.
        std::vector<VkSemaphore>                  ourWaits;
        std::vector<std::shared_ptr<SwapchainState>> hijacked(pPresentInfo->swapchainCount);
        ourWaits.reserve(pPresentInfo->swapchainCount);

        bool anyHijacked = false;
        for (uint32_t i = 0; i < pPresentInfo->swapchainCount; ++i)
        {
            hijacked[i] = findSwapchain(pPresentInfo->pSwapchains[i]);
            if (!hijacked[i]) continue;
            anyHijacked = true;

            const VkSemaphore* ws = (ourWaits.empty()) ? pPresentInfo->pWaitSemaphores : nullptr;
            uint32_t           wc = (ourWaits.empty()) ? pPresentInfo->waitSemaphoreCount : 0;

            VkSemaphore sem = hijacked[i]->submitBlit(st->graphicsQueue,
                                                     pPresentInfo->pImageIndices[i],
                                                     ws, wc);
            if (sem != VK_NULL_HANDLE) ourWaits.push_back(sem);
        }

        if (!anyHijacked)
            return st->vkd.QueuePresentKHR(queue, pPresentInfo);

        // copy the ORIGINAL present info so pNext survives (VK_KHR_present_id,
        // VK_EXT_swapchain_maintenance1, etc). drop pNext and DXVK 2.x's frame
        // tracking just silently dies and the app deadlocks. ask me how i know lol
        VkPresentInfoKHR mi   = *pPresentInfo;
        mi.waitSemaphoreCount = static_cast<uint32_t>(ourWaits.size());
        mi.pWaitSemaphores    = ourWaits.data();
        return st->vkd.QueuePresentKHR(queue, &mi);
    }

    static VkResult VKAPI_CALL vksumi_EnumerateInstanceLayerProperties(uint32_t* pCount, VkLayerProperties* pProperties)
    {
        if (pProperties == nullptr) { *pCount = 1; return VK_SUCCESS; }
        if (*pCount < 1) return VK_INCOMPLETE;
        std::strncpy(pProperties[0].layerName, VKSUMI_LAYER_NAME, VK_MAX_EXTENSION_NAME_SIZE);
        std::strncpy(pProperties[0].description, "vksumi color grading layer", VK_MAX_DESCRIPTION_SIZE);
        pProperties[0].implementationVersion = 1;
        pProperties[0].specVersion           = VK_API_VERSION_1_3;
        *pCount = 1;
        return VK_SUCCESS;
    }

    static VkResult VKAPI_CALL vksumi_EnumerateDeviceLayerProperties(VkPhysicalDevice,
                                                                     uint32_t*          pCount,
                                                                     VkLayerProperties* pProperties)
    {
        return vksumi_EnumerateInstanceLayerProperties(pCount, pProperties);
    }

    static VkResult VKAPI_CALL vksumi_EnumerateInstanceExtensionProperties(const char*            pLayerName,
                                                                           uint32_t*              pCount,
                                                                           VkExtensionProperties* /*pProperties*/)
    {
        if (pLayerName && !std::strcmp(pLayerName, VKSUMI_LAYER_NAME)) { *pCount = 0; return VK_SUCCESS; }
        return VK_ERROR_LAYER_NOT_PRESENT;
    }

    static VkResult VKAPI_CALL vksumi_EnumerateDeviceExtensionProperties(VkPhysicalDevice       physicalDevice,
                                                                         const char*            pLayerName,
                                                                         uint32_t*              pCount,
                                                                         VkExtensionProperties* pProperties)
    {
        if (pLayerName && !std::strcmp(pLayerName, VKSUMI_LAYER_NAME))
        { if (pCount) *pCount = 0; return VK_SUCCESS; }
        if (physicalDevice == VK_NULL_HANDLE) return VK_SUCCESS;

        InstanceDispatch table;
        {
            scoped_lock l(g_lock);
            table = g_instanceDispatch[GetKey(physicalDevice)];
        }
        return table.EnumerateDeviceExtensionProperties(physicalDevice, pLayerName, pCount, pProperties);
    }
}

extern "C"
{
    VKSUMI_EXPORT PFN_vkVoidFunction VKAPI_CALL vksumi_GetDeviceProcAddr(VkDevice device, const char* pName);
    VKSUMI_EXPORT PFN_vkVoidFunction VKAPI_CALL vksumi_GetInstanceProcAddr(VkInstance instance, const char* pName);

#define INTERCEPT(func) \
    if (!std::strcmp(pName, "vk" #func)) \
        return reinterpret_cast<PFN_vkVoidFunction>(&vksumi::vksumi_##func);

#define INTERCEPT_ALL \
    if (!std::strcmp(pName, "vkGetInstanceProcAddr")) \
        return reinterpret_cast<PFN_vkVoidFunction>(&vksumi_GetInstanceProcAddr); \
    if (!std::strcmp(pName, "vkGetDeviceProcAddr")) \
        return reinterpret_cast<PFN_vkVoidFunction>(&vksumi_GetDeviceProcAddr); \
    INTERCEPT(EnumerateInstanceLayerProperties) \
    INTERCEPT(EnumerateInstanceExtensionProperties) \
    INTERCEPT(EnumerateDeviceLayerProperties) \
    INTERCEPT(EnumerateDeviceExtensionProperties) \
    INTERCEPT(CreateInstance) \
    INTERCEPT(DestroyInstance) \
    INTERCEPT(CreateDevice) \
    INTERCEPT(DestroyDevice) \
    INTERCEPT(CreateSwapchainKHR) \
    INTERCEPT(DestroySwapchainKHR) \
    INTERCEPT(GetSwapchainImagesKHR) \
    INTERCEPT(QueuePresentKHR)

    VKSUMI_EXPORT PFN_vkVoidFunction VKAPI_CALL vksumi_GetInstanceProcAddr(VkInstance instance, const char* pName)
    {
        INTERCEPT_ALL
        vksumi::scoped_lock l(vksumi::g_lock);
        auto it = vksumi::g_instanceDispatch.find(vksumi::GetKey(instance));
        if (it == vksumi::g_instanceDispatch.end() || !it->second.GetInstanceProcAddr)
            return nullptr;
        return it->second.GetInstanceProcAddr(instance, pName);
    }

    VKSUMI_EXPORT PFN_vkVoidFunction VKAPI_CALL vksumi_GetDeviceProcAddr(VkDevice device, const char* pName)
    {
        INTERCEPT_ALL
        auto* st = vksumi::findDevice(device);
        if (!st || !st->vkd.GetDeviceProcAddr) return nullptr;
        return st->vkd.GetDeviceProcAddr(device, pName);
    }
}
