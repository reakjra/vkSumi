#pragma once

#include <vulkan/vulkan.h>

#include <memory>
#include <vector>

#include "vkdispatch.hpp"

namespace vksumi
{
    // cached per-device stuff the swapchain code wants
    struct DeviceState
    {
        VkDevice          device         = VK_NULL_HANDLE;
        VkPhysicalDevice  physicalDevice = VK_NULL_HANDLE;
        VkQueue           graphicsQueue  = VK_NULL_HANDLE;
        uint32_t          graphicsFamily = UINT32_MAX;
        DeviceDispatch    vkd{};
        InstanceDispatch  vki{};
    };

    // app renders to fakeImages thinking they're swapchain images. on present
    // we sample the fake, run the color shader, write to the real swapchain
    // image, present that. app never knows.
    class SwapchainState
    {
    public:
        // returns null on failure, layer just falls back to passthrough in
        // that case (we never register the swapchain so present sees no match)
        static std::shared_ptr<SwapchainState> create(const DeviceState&              dev,
                                                      VkSwapchainKHR                  realSwapchain,
                                                      const VkSwapchainCreateInfoKHR& createInfo);

        ~SwapchainState();

        SwapchainState(const SwapchainState&)            = delete;
        SwapchainState& operator=(const SwapchainState&) = delete;

        // Hand the app our fake images instead of the real ones.
        VkResult getImages(uint32_t* pCount, VkImage* pImages);

        // submit the blit (apply Knobs, render to the real image). returns the
        // semaphore the driver-side present needs to wait on, or VK_NULL_HANDLE
        // if the submit died. pass app's wait semaphores ONLY on the first
        // swapchain submit per present, binary semaphores cant be waited twice.
        VkSemaphore submitBlit(VkQueue                     submitQueue,
                               uint32_t                    imageIndex,
                               const VkSemaphore*          waitSemaphores,
                               uint32_t                    waitCount);

        VkSwapchainKHR realSwapchain() const { return m_realSwapchain; }

    private:
        SwapchainState(const DeviceState& dev, VkSwapchainKHR sc, VkExtent2D extent, VkFormat fmt);

        bool initImages(const VkSwapchainCreateInfoKHR& createInfo);
        bool initSampler();
        bool initRenderPass();
        bool initDescriptorLayout();
        bool initPipeline();
        bool initFramebuffersAndDescriptors();
        bool initCommandResources();
        bool initSemaphores();
        bool initFences();

        void recordCommandBuffer(uint32_t imageIndex);

        DeviceState                  m_dev;
        VkSwapchainKHR               m_realSwapchain = VK_NULL_HANDLE;
        VkExtent2D                   m_extent{};
        VkFormat                     m_format = VK_FORMAT_UNDEFINED;

        // Real images we render INTO (presented).
        std::vector<VkImage>         m_realImages;
        std::vector<VkImageView>     m_realViews;
        std::vector<VkFramebuffer>   m_framebuffers;

        // Fake images the app renders TO (we sample these).
        std::vector<VkImage>         m_fakeImages;
        std::vector<VkDeviceMemory>  m_fakeMemory;
        std::vector<VkImageView>     m_fakeViews;

        VkSampler                    m_sampler        = VK_NULL_HANDLE;
        VkRenderPass                 m_renderPass     = VK_NULL_HANDLE;
        VkDescriptorSetLayout        m_dsLayout       = VK_NULL_HANDLE;
        VkDescriptorPool             m_dsPool         = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_dsets;
        VkPipelineLayout             m_pipeLayout     = VK_NULL_HANDLE;
        VkShaderModule               m_vert           = VK_NULL_HANDLE;
        VkShaderModule               m_frag           = VK_NULL_HANDLE;
        VkPipeline                   m_pipeline       = VK_NULL_HANDLE;

        VkCommandPool                m_cmdPool        = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> m_cmdBuffers;
        std::vector<VkSemaphore>     m_renderDone;
        std::vector<VkFence>         m_inFlight;

        // first use of an image we transition from UNDEFINED, after that PRESENT_SRC
        std::vector<bool>            m_realFirstUse;
        uint32_t                     m_presentCount = 0;
    };

    void                            registerSwapchain(VkSwapchainKHR sc, std::shared_ptr<SwapchainState> state);
    std::shared_ptr<SwapchainState> findSwapchain(VkSwapchainKHR sc);
    void                            unregisterSwapchain(VkSwapchainKHR sc);

    void  registerDevice(VkDevice device, DeviceState state);
    DeviceState* findDevice(VkDevice device);
    DeviceState* findDeviceByQueue(VkQueue queue);
    void  unregisterDevice(VkDevice device);
}
