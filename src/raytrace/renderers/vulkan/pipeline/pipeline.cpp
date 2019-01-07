/*
 * Copyright (C) 2018 Michał Siejak
 * This file is part of Quartz - a raytracing aspect for Qt3D.
 * See LICENSE file for licensing information.
 */

#include <renderers/vulkan/pipeline/pipeline.h>
#include <renderers/vulkan/device.h>

namespace Qt3DRaytrace {
namespace Vulkan {

Pipeline::Pipeline(VkPipelineBindPoint bindPoint)
    : m_bindPoint(bindPoint)
    , pipelineLayout(VK_NULL_HANDLE)
{}

PipelineBuilder::PipelineBuilder(Device *device)
    : m_device(*device)
    , m_defaultSampler(VK_NULL_HANDLE)
{
    Q_ASSERT(m_device != VK_NULL_HANDLE);
}

QVector<VkDescriptorSetLayout> PipelineBuilder::buildDescriptorSetLayouts() const
{
    struct DescriptorSetLayoutBinding {
        VkDescriptorType type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
        VkShaderStageFlags stageFlags = 0;
        uint32_t count = 0;
        QVector<VkSampler> samplers;
    };
    using DescriptorSetLayout = QVector<DescriptorSetLayoutBinding>;
    QVector<DescriptorSetLayout> layouts;

    QMultiMap<QString, DescriptorSetLayoutBinding*> bindingsByName;

    for(const ShaderModule *shader : m_shaders) {
        for(const ShaderModule::DescriptorSetLayout &shaderSetLayout : shader->descriptorSets()) {
            int setNumber = int(shaderSetLayout.set);
            if(setNumber >= layouts.size()) {
                layouts.resize(setNumber+1);
            }

            DescriptorSetLayout &setLayout = layouts[setNumber];
            for(const ShaderModule::DescriptorSetLayoutBinding &shaderBinding : shaderSetLayout.bindings) {
                int bindingNumber = int(shaderBinding.binding);
                if(bindingNumber >= setLayout.size()) {
                    setLayout.resize(bindingNumber+1);
                }

                DescriptorSetLayoutBinding &binding = setLayout[bindingNumber];
                if(binding.type == VK_DESCRIPTOR_TYPE_MAX_ENUM) {
                    binding.type = shaderBinding.type;
                }
                if(binding.count == 0) {
                    binding.count = shaderBinding.count;
                }

                bool bindingConflict = (binding.type != shaderBinding.type) || (binding.count != shaderBinding.count);
                if(bindingConflict) {
                    qCWarning(logVulkan) << "PipelineBuilder: Conflicting descriptor set layout binding detected at (set ="
                                         << setNumber << ", binding =" << bindingNumber << ")";
                }
                else {
                    binding.stageFlags |= shader->stage();
                    bindingsByName.insert(shaderBinding.name, &binding);
                    if(binding.type == VK_DESCRIPTOR_TYPE_SAMPLER || binding.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                        VkSampler sampler = m_samplersByID.value(qMakePair(setNumber, bindingNumber), VK_NULL_HANDLE);
                        if(sampler != VK_NULL_HANDLE) {
                            binding.samplers.fill(sampler, int(binding.count));
                        }
                    }
                }
            }
        }
    }

    for(auto it = m_samplersByName.begin(); it != m_samplersByName.end(); ++it) {
        auto bindings = bindingsByName.values(it.key());
        for(DescriptorSetLayoutBinding *binding : bindings) {
            if(binding->samplers.size() == 0) {
                binding->samplers.fill(it.value(), int(binding->count));
            }
            else {
                qCWarning(logVulkan) << "PipelineBuilder: Conflicting immutable sampler assignment detected for descriptor set binding variable name"
                                     << it.key();
            }
        }
    }

    if(m_defaultSampler != VK_NULL_HANDLE) {
        for(DescriptorSetLayout &setLayout : layouts) {
            for(DescriptorSetLayoutBinding &binding : setLayout) {
                if(binding.count > 0 && (binding.type == VK_DESCRIPTOR_TYPE_SAMPLER || binding.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)) {
                    binding.samplers.fill(m_defaultSampler, int(binding.count));
                }
            }
        }
    }

    QVector<VkDescriptorSetLayout> vulkanLayouts;
    for(const DescriptorSetLayout &setLayout : layouts) {
        QVector<VkDescriptorSetLayoutBinding> vulkanBindings;
        VkDescriptorSetLayoutCreateInfo layoutCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };

        if(setLayout.size() > 0) {
            for(uint32_t bindingNumber=0; bindingNumber < uint32_t(setLayout.size()); ++bindingNumber) {
                const DescriptorSetLayoutBinding &binding = setLayout[int(bindingNumber)];
                if(binding.count > 0) {
                    VkDescriptorSetLayoutBinding vulkanBinding = {};
                    vulkanBinding.binding = bindingNumber;
                    vulkanBinding.stageFlags = binding.stageFlags;
                    vulkanBinding.descriptorType = binding.type;
                    vulkanBinding.descriptorCount = binding.count;
                    if(binding.samplers.size() > 0) {
                        vulkanBinding.pImmutableSamplers = binding.samplers.data();
                    }
                    vulkanBindings.append(vulkanBinding);
                }
            }
            if(vulkanBindings.size() > 0) {
                layoutCreateInfo.bindingCount = uint32_t(vulkanBindings.size());
                layoutCreateInfo.pBindings = vulkanBindings.data();
            }
        }

        VkDescriptorSetLayout vulkanSetLayout;
        if(VKFAILED(vkCreateDescriptorSetLayout(m_device, &layoutCreateInfo, nullptr, &vulkanSetLayout))) {
            qCCritical(logVulkan) << "PipelineBuilder: Failed to create descriptor set layout";
            break;
        }
        vulkanLayouts.append(vulkanSetLayout);
    }
    return vulkanLayouts;
}

VkPipelineLayout PipelineBuilder::buildPipelineLayout(const QVector<VkDescriptorSetLayout> &descriptorSetLayouts) const
{
    // TODO: Add support for push constants.
    VkPipelineLayoutCreateInfo createInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    createInfo.setLayoutCount = uint32_t(descriptorSetLayouts.size());
    if(createInfo.setLayoutCount > 0) {
        createInfo.pSetLayouts = descriptorSetLayouts.data();
    }

    VkPipelineLayout pipelineLayout;
    if(VKFAILED(vkCreatePipelineLayout(m_device, &createInfo, nullptr, &pipelineLayout))) {
        qCCritical(logVulkan) << "PipelineBuilder: Failed to create pipeline layout";
        return VK_NULL_HANDLE;
    }
    return pipelineLayout;
}

PipelineBuilder::~PipelineBuilder()
{
    qDeleteAll(m_ownedModules);
}

PipelineBuilder &PipelineBuilder::shaders(const QVector<const ShaderModule*> &modules)
{
    auto pipelineContainsShaderStage = [this](VkShaderStageFlagBits stage) -> bool {
        for(const ShaderModule *module : m_shaders) {
            if(module->stage() == stage) {
                return true;
            }
        }
        return false;
    };

    for(const ShaderModule *module : modules) {
        if(pipelineContainsShaderStage(module->stage())) {
            qCWarning(logVulkan) << "PipelineBuilder: pipeline already contains shader module for stage:" << module->stage();
        }
        else {
            m_shaders.append(module);
        }
    }
    return *this;
}

PipelineBuilder &PipelineBuilder::shaders(std::initializer_list<QString> moduleNames)
{
    QVector<const ShaderModule*> modules;
    modules.reserve(static_cast<int>(moduleNames.size()));
    for(const QString &name : moduleNames) {
        ShaderModule *module{new ShaderModule(m_device, name)};
        modules.append(module);
        m_ownedModules.append(module);
    }
    return shaders(modules);
}

PipelineBuilder &PipelineBuilder::bytecodes(std::initializer_list<QByteArray> moduleBytecodes)
{
    QVector<const ShaderModule*> modules;
    modules.reserve(static_cast<int>(moduleBytecodes.size()));
    for(const QByteArray &bytecode : moduleBytecodes) {
        ShaderModule *module{new ShaderModule(m_device, bytecode)};
        modules.append(module);
        m_ownedModules.append(module);
    }
    return shaders(modules);
}

PipelineBuilder &PipelineBuilder::defaultSampler(VkSampler sampler)
{
    m_defaultSampler = sampler;
    return *this;
}

PipelineBuilder &PipelineBuilder::sampler(uint32_t set, uint32_t binding, VkSampler sampler)
{
    m_samplersByID.insert(qMakePair(set, binding), sampler);
    return *this;
}

PipelineBuilder &PipelineBuilder::sampler(const QString &name, VkSampler sampler)
{
    m_samplersByName.insert(name, sampler);
    return *this;
}

} // Vulkan
} // Qt3DRaytrace
