#include "vkdispatch.hpp"

namespace vksumi
{
    void fillInstanceDispatch(VkInstance instance, PFN_vkGetInstanceProcAddr gipa, InstanceDispatch* table)
    {
        table->GetInstanceProcAddr = gipa;
#define FORVKFUNC(func) \
    if (!table->func) \
        table->func = (PFN_vk##func) gipa(instance, "vk" #func);
        VK_INSTANCE_FUNCS
#undef FORVKFUNC
    }

    void fillDeviceDispatch(VkDevice device, PFN_vkGetDeviceProcAddr gdpa, DeviceDispatch* table)
    {
        table->GetDeviceProcAddr = gdpa;
#define FORVKFUNC(func) \
    if (!table->func) \
        table->func = (PFN_vk##func) gdpa(device, "vk" #func);
        VK_DEVICE_FUNCS
#undef FORVKFUNC
    }
}
