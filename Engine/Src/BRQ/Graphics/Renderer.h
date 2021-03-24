#pragma once

#include "Events/Event.h"
#include "Camera/Camera.h"
#include "Texture2D.h"
#include "Platform/Vulkan/VKShader.h"
#include "Platform/Vulkan/RenderContext.h"

namespace BRQ {

    class Renderer {

    private:
        static Renderer*											        s_Renderer;

        const Window*												        m_Window;

        RenderContext*                                                      m_RenderContext;
        
        Texture2D*                                                          m_Texture2D;

        VkRenderPass												        m_RenderPass;
        VkPipelineLayout											        m_Layout;
        VkPipeline      											        m_GraphicsPipeline;
        VkDescriptorSetLayout                                               m_DescriptorSetLayout;
        VkSampler                                                           m_TextureSampler2D;

        std::vector<VkFramebuffer>                                          m_Framebuffers;
        std::vector<VkDescriptorPool>                                       m_DescriptorPool;
        std::vector<VkDescriptorSet>                                        m_DescriptorSet;
        std::vector<VkCommandPool>                                          m_CommandPools;
        std::vector<VkCommandBuffer>								        m_CommandBuffers;
        std::vector<VkSemaphore>									        m_ImageAvailableSemaphores;
        std::vector<VkSemaphore>									        m_RenderFinishedSemaphores;
        std::vector<VkFence>										        m_CommandBufferExecutedFences;
        std::vector<VKShader>										        m_Shaders;

        std::vector<std::pair<std::string, VKShader::ShaderType>>           m_ShaderResources;

    protected:
        Renderer();
        Renderer(const Renderer& renderer) = default;

    public:
        ~Renderer() = default;

        static void Init(const Window* window);
        static void Shutdown();

        static Renderer* GetInstance() { return s_Renderer;  }

        void SubmitResources(const std::vector<std::pair<std::string, VKShader::ShaderType>>& resources);

        void BeginScene(const Camera& camera);
        void EndScene();

        //void Submit();

        void Present();

    private:
        void InitInternal(const Window* window);
        void DestroyInternal();

        void RecreateSwapchain();

        void LoadShaderResources();
        void DestroyShaderRescources();

        void CreateRenderPass();
        void DestroyRenderPass();

        void CreateFramebuffers();
        void DestroyFramebuffers();

        void CreatePipelineLayout();
        void DestroyPipelineLayout();

        void CreateGraphicsPipeline();
        void DestroyGraphicsPipeline();

        void CreateCommands();
        void DestroyCommands();

        void CreateSyncronizationPrimitives();
        void DestroySyncronizationPrimitives();

        void CreateDescriptorSetLayout();
        void DestoryDescriptorSetLayout();

        void CreateDescriptorPool();
        void DestroyDescriptorPool();

        void CreateDescriptorSets();

        void CreateTextureSampler2D();
        void DestroyTextureSampler2D();

        // this is temp
        void CreateTexture();
        void DestroyTexture();
    };
}