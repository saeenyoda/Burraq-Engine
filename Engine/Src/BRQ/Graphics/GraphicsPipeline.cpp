#include <BRQ.h>

#include "GraphicsPipeline.h"
#include <spirv_reflect.h>

#include "Platform/Vulkan/RenderContext.h"

namespace BRQ {

    GraphicsPipeline::GraphicsPipeline() {

        m_Layout = VK_NULL_HANDLE;
        m_Pipeline = VK_NULL_HANDLE;
    }

    void GraphicsPipeline::Init(const GraphicsPipelineCreateInfo& info) {

        m_Stages = info.Shaders;

        std::vector<VkPipelineShaderStageCreateInfo> pipelineStages(m_Stages.size());
        std::vector<VkPushConstantRange> ranges;

        RenderContext* context = RenderContext::GetInstance();

        for (U64 i = 0; i < m_Stages.size(); i++) {

            const auto result = m_Stages[i].ShaderFilename.find(".spv");

            if (result == std::string_view::npos) {

                BRQ_CORE_ERROR("Invalid shader file extension(Only SPIRV shaders are valid)! File: {}", m_Stages[i].ShaderFilename.data());
                return;
            }

            std::vector<BYTE> code = std::move(ShaderSource(m_Stages[i].ShaderFilename));

            if (code.empty()) {

                BRQ_CORE_ERROR("Empty Shader Code (Reason: Can't Read File) File: {}", m_Stages[i].ShaderFilename.data());
                return;
            }

            ReflectionData reflection = std::move(GetSPIRVReflection(code));
       
            VkPipelineShaderStageCreateInfo stageInfo = {};

            stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stageInfo.stage = reflection.Stage;
            stageInfo.module = CreateShader(code);
            stageInfo.pName = "main";
            
            pipelineStages[i] = stageInfo;
            
            // WHY LOL
            for (U64 j = 0; j < reflection.PushConstantRanges.size(); j++) {

                ranges.push_back(reflection.PushConstantRanges[j]);
            }

            for (U64 j = 0; j < reflection.DescriptorSetLayoutData.size(); j++) {

                CreateDescriptorSetLayout(reflection.DescriptorSetLayoutData[j].CreateInfo);
            }
        }

        CreatePipelineLayout(ranges);
        CreatePipeline(info, pipelineStages);

        for (U64 i = 0; i < pipelineStages.size(); i++) {

            DestroyShader(pipelineStages[i].module);
        }
    }

    void GraphicsPipeline::Destroy() {

        RenderContext* context = RenderContext::GetInstance();

        for (U64 i = 0; i < m_DescriptorSetLayouts.size(); i++) {

            VK::DestoryDescriptorSetLayout(context->GetDevice(), m_DescriptorSetLayouts[i]);
        }

        VK::DestroyPipelineLayout(context->GetDevice(), m_Layout);
        VK::DestroyGraphicsPipeline(context->GetDevice(), m_Pipeline);
    }

    void GraphicsPipeline::Bind(const VkCommandBuffer& commandBuffer) {

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);
    }

    void GraphicsPipeline::BindDescriptorSets(const VkCommandBuffer& commandBuffer, const VkDescriptorSet* sets, U32 size) {

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Layout, 0, size, sets, 0, nullptr);
    }

    void GraphicsPipeline::PushConstantData(const VkCommandBuffer& commandBuffer, PipelineStage stage, const void* data, U32 size, U32 offset) {

        vkCmdPushConstants(commandBuffer, m_Layout, (VkShaderStageFlags)stage, offset, size, data);
    }

    std::vector<BYTE> GraphicsPipeline::ShaderSource(const std::string_view& filename) {

        using namespace Utilities;

        FileSystem* fs = FileSystem::GetInstance();

        std::vector<BYTE> source;

        const auto result = filename.find(".spv");

        if (result == std::string_view::npos) {

            BRQ_CORE_ERROR("Invalid shader file extension(Only SPIRV shaders are valid)! File: {}", filename.data());
            return source;
        }

        source = std::move(fs->ReadFile(filename, FileSystem::InputMode::ReadBinary));

        if (source.empty()) {

            BRQ_CORE_ERROR("Empty Shader Code (Reason: Can't Read File) File: {}", filename.data());
            return source;
        }

        return source;
    }

    GraphicsPipeline::ReflectionData GraphicsPipeline::GetSPIRVReflection(const std::vector<BYTE>& code) {
       
        // this is pretty BAD and NOT Optimized neither i care for this block of code;

        GraphicsPipeline::ReflectionData out = {};

        SpvReflectShaderModule reflectModule;
        SpvReflectResult result = spvReflectCreateShaderModule(code.size(), code.data(), &reflectModule);

        if (result != SPV_REFLECT_RESULT_SUCCESS) {

            BRQ_CORE_FATAL("Failed to create reflection for shader");
            return out;
        }

        out.Stage = (VkShaderStageFlagBits)reflectModule.shader_stage;

        // --------------------------- DescriptorSet Reflection Data -----------------------------
        U32 setCount = 0;
        result = spvReflectEnumerateDescriptorSets(&reflectModule, &setCount, NULL);

        // prolly allocate outside of loop but whatever
        std::vector<SpvReflectDescriptorSet*> sets(setCount);
        result = spvReflectEnumerateDescriptorSets(&reflectModule, &setCount, sets.data());

        if (result != SPV_REFLECT_RESULT_SUCCESS) {

            BRQ_CORE_FATAL("Failed to Enumerate DescriptorSets for shader");
            return out;
        }

        out.DescriptorSetLayoutData.resize(sets.size());

        for (U64 i = 0; i < sets.size(); ++i) {

            const SpvReflectDescriptorSet& refSet = *(sets[i]);
            DescriptorSetLayoutData& layout = out.DescriptorSetLayoutData[i];

            layout.Bindings.resize(refSet.binding_count);

            for (U32 j = 0; j < refSet.binding_count; ++j) {

                const SpvReflectDescriptorBinding& refBinding = *(refSet.bindings[j]);
                VkDescriptorSetLayoutBinding& binding = layout.Bindings[j];

                binding.binding = refBinding.binding;
                binding.descriptorType = static_cast<VkDescriptorType>(refBinding.descriptor_type);
                binding.descriptorCount = 1;

                for (U32 k = 0; k < refBinding.array.dims_count; ++k) {
                    binding.descriptorCount *= refBinding.array.dims[k];
                }
                binding.stageFlags = (VkShaderStageFlagBits)(reflectModule.shader_stage);
            }
            layout.SetNumber = refSet.set;
            layout.CreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layout.CreateInfo.bindingCount = refSet.binding_count;
            layout.CreateInfo.pBindings = layout.Bindings.data();
        }

        // ------------------------------ PushConstant Reflection Data --------------------------

        U32 PushConstantCount = 0;
        result = spvReflectEnumeratePushConstantBlocks(&reflectModule, &PushConstantCount, NULL);

        // BREH
        std::vector<SpvReflectBlockVariable*> blocks(PushConstantCount);
        result = spvReflectEnumeratePushConstantBlocks(&reflectModule, &PushConstantCount, blocks.data());

        if (result != SPV_REFLECT_RESULT_SUCCESS) {

            BRQ_CORE_FATAL("Failed to Enumerate Push Constants for shader");
            return out;
        }

        out.PushConstantRanges.resize(blocks.size());

        for (U64 i = 0; i < blocks.size(); ++i) {

            const SpvReflectBlockVariable& blockVar = *(blocks[i]);
            VkPushConstantRange& layout = out.PushConstantRanges[i];

            layout.offset = blockVar.offset;
            layout.size = blockVar.size;
            layout.stageFlags = (VkShaderStageFlags)reflectModule.shader_stage;
        }
        
        return out;
    }

    void GraphicsPipeline::CreateDescriptorSetLayout(const VkDescriptorSetLayoutCreateInfo& createInfo) {

        RenderContext* context = RenderContext::GetInstance();

        VK::DescriptorSetLayoutCreateInfo info = {};
        info.BindingCount = createInfo.bindingCount;
        info.Bindings = createInfo.pBindings;

        m_DescriptorSetLayouts.push_back(VK::CreateDescriptorSetLayout(context->GetDevice(), info));
    }

    void GraphicsPipeline::CreatePipelineLayout(const std::vector<VkPushConstantRange>& ranges) {

        RenderContext* context = RenderContext::GetInstance();

        VK::PipelineLayoutCreateInfo info = {};
        info.SetLayouts = m_DescriptorSetLayouts;
        info.PushConstantRanges = ranges;

        m_Layout = VK::CreatePipelineLayout(context->GetDevice(), info);
    }

    VkShaderModule GraphicsPipeline::CreateShader(const std::vector<BYTE>& code) {

        RenderContext* context = RenderContext::GetInstance();

        VkShaderModule shader = VK_NULL_HANDLE;

        VkShaderModuleCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = code.size();
        info.pCode = (U32*)code.data();
        VK_CHECK(vkCreateShaderModule(context->GetDevice(), &info, nullptr, &shader));

        return shader;
    }

    void GraphicsPipeline::DestroyShader(VkShaderModule& shader) {

        RenderContext* context = RenderContext::GetInstance();

        if (shader != VK_NULL_HANDLE) {

            vkDestroyShaderModule(context->GetDevice(), shader, nullptr);
            shader = VK_NULL_HANDLE;
        }
    }

    void GraphicsPipeline::CreatePipeline(const GraphicsPipelineCreateInfo& info, std::vector<VkPipelineShaderStageCreateInfo>& stageInfos) {

        RenderContext* context = RenderContext::GetInstance();

        const std::vector<BufferElement>& elements = info.Layout.GetElements();

        VkVertexInputBindingDescription bindingDescription = {};
        bindingDescription.binding = 0;
        bindingDescription.stride = (U32)info.Layout.GetStride();
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::vector<VkVertexInputAttributeDescription> attributeDescription(elements.size());

        for (U64 i = 0; i < elements.size(); i++) {

            attributeDescription[i].binding = 0;
            attributeDescription[i].location = (U32)i;
            attributeDescription[i].format = ToVulkanFormat(elements[i].Type);
            attributeDescription[i].offset = elements[i].Offset;
        }

        VkPipelineVertexInputStateCreateInfo vertexInfo = {};
        vertexInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInfo.vertexBindingDescriptionCount = 1;
        vertexInfo.vertexAttributeDescriptionCount = (U32)attributeDescription.size();
        vertexInfo.pVertexAttributeDescriptions = attributeDescription.data();
        vertexInfo.pVertexBindingDescriptions = &bindingDescription;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportState = {};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer = {};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.lineWidth = 1.0f;
        rasterizer.depthBiasEnable = VK_FALSE;

        if (info.Flags & PolygonModeLine) {

            rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
        }
        else {

            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        }

        if (info.Flags & EnableCulling) {

            rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

            if (info.Flags & CullModeFrontFace) {

                rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
            }
            else {
                rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
            }
        }

        VkPipelineMultisampleStateCreateInfo multisampling = {};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending = {};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.logicOp = VK_LOGIC_OP_COPY;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;
        colorBlending.blendConstants[0] = 0.0f;
        colorBlending.blendConstants[1] = 0.0f;
        colorBlending.blendConstants[2] = 0.0f;
        colorBlending.blendConstants[3] = 0.0f;

        std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

        VkPipelineDynamicStateCreateInfo dynamicStateInfo = {};
        dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicStateInfo.pDynamicStates = dynamicStates.data();
        dynamicStateInfo.dynamicStateCount = (U32)dynamicStates.size();

        VkPipelineDepthStencilStateCreateInfo depthStencil = {};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

        if (info.Flags & DepthWriteEnabled) {

            depthStencil.depthWriteEnable = VK_TRUE;
        }

        if (info.Flags & DepthTestEnabled) {

            depthStencil.depthTestEnable = VK_TRUE;
        }

        if (info.Flags & DepthCompareLess) {

            depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        }
        else if(info.Flags & DepthCompareEqual) {

            depthStencil.depthCompareOp = VK_COMPARE_OP_EQUAL;
        }
        else if (info.Flags & DepthCompareGreater) {

            depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER;
        }
        else if ((info.Flags & DepthCompareLess) && (info.Flags & DepthCompareEqual)) {

            depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        }
        else if ((info.Flags & DepthCompareGreater) && (info.Flags & DepthCompareEqual)) {

            depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
        }

        VK::GraphicsPipelineCreateInfo pipelineInfo = {};
        pipelineInfo.Stages = stageInfos;
        pipelineInfo.VertexInputState = vertexInfo;
        pipelineInfo.InputAssemblyState = inputAssembly;
        pipelineInfo.ViewportState = viewportState;
        pipelineInfo.RasterizationState = rasterizer;
        pipelineInfo.MultisampleState = multisampling;
        pipelineInfo.DepthStencilState = depthStencil;
        pipelineInfo.ColorBlendState = colorBlending;
        pipelineInfo.DynamicState = dynamicStateInfo;
        pipelineInfo.Layout = m_Layout;
        pipelineInfo.RenderPass = context->GetRenderPass();

        m_Pipeline = VK::CreateGraphicsPipeline(context->GetDevice(), pipelineInfo);
    }
}