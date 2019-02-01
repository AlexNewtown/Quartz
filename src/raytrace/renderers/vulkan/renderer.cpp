/*
 * Copyright (C) 2018-2019 Michał Siejak
 * This file is part of Quartz - a raytracing aspect for Qt3D.
 * See LICENSE file for licensing information.
 */

#include <renderers/vulkan/renderer.h>
#include <renderers/vulkan/vkcommon.h>
#include <renderers/vulkan/commandbuffer.h>
#include <renderers/vulkan/initializers.h>
#include <renderers/vulkan/shadermodule.h>
#include <renderers/vulkan/pipeline/graphicspipeline.h>
#include <renderers/vulkan/pipeline/computepipeline.h>
#include <renderers/vulkan/pipeline/raytracingpipeline.h>

#include <renderers/vulkan/jobs/buildscenetlasjob.h>
#include <renderers/vulkan/jobs/buildgeometryjob.h>

#include <backend/managers_p.h>

#include <QVulkanInstance>
#include <QWindow>
#include <QTimer>

#include <QtMath>

namespace Qt3DRaytrace {
namespace Vulkan {

namespace {

constexpr VkFormat RenderBufferFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr uint32_t DescriptorPoolCapacity = 128;

}

Q_LOGGING_CATEGORY(logVulkan, "raytrace.vulkan")

Renderer::Renderer(QObject *parent)
    : QObject(parent)
    , m_renderTimer(new QTimer(this))
    , m_frameAdvanceService(new FrameAdvanceService)
    , m_updateWorldTransformJob(new Raytrace::UpdateWorldTransformJob)
    , m_destroyRetiredResourcesJob(new DestroyRetiredResourcesJob(this))
{
    QObject::connect(m_renderTimer, &QTimer::timeout, this, &Renderer::renderFrame);
}

bool Renderer::initialize()
{
    static const QByteArrayList RequiredDeviceExtensions {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_NV_RAY_TRACING_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
    };

    if(!m_window) {
        qCCritical(logVulkan) << "Cannot initialize renderer: no surface set";
        return false;
    }
    if(!m_window->vulkanInstance()) {
        qCCritical(logVulkan) << "Cannot initialize renderer: no Vulkan instance set";
        return false;
    }

    if(VKFAILED(volkInitialize())) {
        qCCritical(logVulkan) << "Failed to initialize Vulkan function loader";
        return false;
    }

    m_instance = m_window->vulkanInstance();
    volkLoadInstance(m_instance->vkInstance());

    uint32_t queueFamilyIndex;
    VkPhysicalDevice physicalDevice = choosePhysicalDevice(RequiredDeviceExtensions, queueFamilyIndex);
    if(physicalDevice == VK_NULL_HANDLE) {
        qCCritical(logVulkan) << "No suitable Vulkan physical device found";
        return false;
    }

    m_device = QSharedPointer<Device>(Device::create(physicalDevice, queueFamilyIndex, RequiredDeviceExtensions));
    if(!m_device) {
        return false;
    }
    vkGetDeviceQueue(*m_device, queueFamilyIndex, 0, &m_graphicsQueue);

    int numConcurrentFrames;
    if(!querySwapchainProperties(physicalDevice, m_swapchainFormat, numConcurrentFrames)) {
        return false;
    }
    m_frameResources.resize(numConcurrentFrames);

    m_commandBufferManager.reset(new CommandBufferManager(m_device.get()));
    m_descriptorManager.reset(new DescriptorManager(m_device.get()));

    if(!createResources()) {
        return false;
    }

    m_renderTimer->start();
    m_frameAdvanceService->proceedToNextFrame();
    return true;
}

void Renderer::shutdown()
{
    m_renderTimer->stop();

    if(m_device) {
        m_device->waitIdle();

        m_commandBufferManager.reset();
        m_descriptorManager.reset();

        releaseSwapchainResources();
        m_device->destroySwapchain(m_swapchain);
        releaseResources();

        m_device.reset();
    }

    m_swapchain = {};
    m_graphicsQueue = VK_NULL_HANDLE;
}

QVector<Qt3DCore::QAspectJobPtr> Renderer::createGeometryJobs()
{
    QVector<Qt3DCore::QAspectJobPtr> geometryJobs;

    auto *geometryManager = &m_nodeManagers->geometryManager;
    auto dirtyGeometry = geometryManager->acquireDirtyComponents();

    QVector<Qt3DCore::QAspectJobPtr> buildGeometryJobs;
    buildGeometryJobs.reserve(dirtyGeometry.size());
    for(const Qt3DCore::QNodeId &geometryId : dirtyGeometry) {
        Raytrace::HGeometry handle = geometryManager->lookupHandle(geometryId);
        if(!handle.isNull()) {
            auto job = BuildGeometryJobPtr::create(this, m_nodeManagers, handle);
            buildGeometryJobs.append(job);
        }
    }

    geometryJobs.append(buildGeometryJobs);
    return geometryJobs;
}

bool Renderer::createResources()
{
    if(!m_descriptorManager->createDescriptorPool(ResourceClass::AttributeBuffer, DescriptorPoolCapacity)) {
        qCCritical(logVulkan) << "Failed to create attribute buffer descriptor pool";
        return false;
    }
    if(!m_descriptorManager->createDescriptorPool(ResourceClass::IndexBuffer, DescriptorPoolCapacity)) {
        qCCritical(logVulkan) << "Failed to create index buffer descriptor pool";
        return false;
    }

    m_renderingFinishedSemaphore = m_device->createSemaphore();
    m_presentationFinishedSemaphore = m_device->createSemaphore();

    m_frameCommandPool = m_device->createCommandPool(VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    {
        QVector<CommandBuffer> frameCommandBuffers = m_device->allocateCommandBuffers({m_frameCommandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, uint32_t(numConcurrentFrames())});
        for(int i=0; i<int(numConcurrentFrames()); ++i) {
            m_frameResources[i].commandBuffer = frameCommandBuffers[i];
            m_frameResources[i].commandBuffersExecutedFence = m_device->createFence(VK_FENCE_CREATE_SIGNALED_BIT);
        }
    }

    {
        const QVector<VkDescriptorPoolSize> descriptorPoolSizes = {
            { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV, numConcurrentFrames() },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, numConcurrentFrames() },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, numConcurrentFrames() },
        };
        const uint32_t descriptorPoolCapacity = uint32_t(descriptorPoolSizes.size()) * numConcurrentFrames();
        m_frameDescriptorPool = m_device->createDescriptorPool({ descriptorPoolCapacity, descriptorPoolSizes});
    }

    m_defaultSampler = m_device->createSampler({VK_FILTER_NEAREST});

    m_displayRenderPass = createDisplayRenderPass(m_swapchainFormat.format);
    m_displayPipeline = GraphicsPipelineBuilder(m_device.get(), m_displayRenderPass)
            .shaders({"display.vert", "display.frag"})
            .defaultSampler(m_defaultSampler)
            .build();

    m_rayTracingPipeline = RayTracingPipelineBuilder(m_device.get())
            .shaders({"test.rgen", "test.rmiss", "test.rchit"})
            .maxRecursionDepth(1)
            .build();

    for(auto &frame : m_frameResources) {
        const QVector<VkDescriptorSetLayout> descriptorSetLayouts = {
            m_displayPipeline.descriptorSetLayouts[0],
            m_rayTracingPipeline.descriptorSetLayouts[0],
        };
        auto descriptorSets = m_device->allocateDescriptorSets({m_frameDescriptorPool, descriptorSetLayouts});
        frame.displayDescriptorSet = descriptorSets[0];
        frame.renderDescriptorSet = descriptorSets[1];
    }

    return true;
}

void Renderer::releaseResources()
{
    m_device->destroySemaphore(m_renderingFinishedSemaphore);
    m_device->destroySemaphore(m_presentationFinishedSemaphore);

    m_device->destroyCommandPool(m_frameCommandPool);
    m_device->destroyDescriptorPool(m_frameDescriptorPool);

    m_device->destroySampler(m_defaultSampler);

    m_device->destroyRenderPass(m_displayRenderPass);
    m_device->destroyPipeline(m_displayPipeline);
    m_device->destroyPipeline(m_rayTracingPipeline);

    for(auto &frame : m_frameResources) {
        m_device->destroyFence(frame.commandBuffersExecutedFence);
    }

    if(m_sceneResources.sceneTLAS) {
        m_device->destroyAccelerationStructure(m_sceneResources.sceneTLAS);
    }
    for(auto &retiredTLAS : m_sceneResources.retiredTLAS) {
        m_device->destroyAccelerationStructure(retiredTLAS.resource);
    }
    for(auto &geometry : m_sceneResources.geometry) {
        m_device->destroyGeometry(geometry);
    }
    m_sceneResources = {};

    m_descriptorManager->destroyAllDescriptorPools();
}

void Renderer::createSwapchainResources()
{
    Result result;

    const uint32_t swapchainWidth = uint32_t(m_swapchainSize.width());
    const uint32_t swapchainHeight = uint32_t(m_swapchainSize.height());

    QVector<VkImage> swapchainImages;
    uint32_t numSwapchainImages = 0;
    vkGetSwapchainImagesKHR(*m_device, m_swapchain, &numSwapchainImages, nullptr);
    swapchainImages.resize(int(numSwapchainImages));
    if(VKFAILED(result = vkGetSwapchainImagesKHR(*m_device, m_swapchain, &numSwapchainImages, swapchainImages.data()))) {
        qCWarning(logVulkan) << "Failed to obtain swapchain image handles:" << result.toString();
        return;
    }

    m_swapchainAttachments.resize(int(numSwapchainImages));
    for(uint32_t imageIndex=0; imageIndex < numSwapchainImages; ++imageIndex) {
        auto &attachment = m_swapchainAttachments[int(imageIndex)];
        attachment.image.handle = swapchainImages[int(imageIndex)];
        attachment.image.view = m_device->createImageView({attachment.image, VK_IMAGE_VIEW_TYPE_2D, m_swapchainFormat.format});
        attachment.framebuffer = m_device->createFramebuffer({m_displayRenderPass, { attachment.image.view }, swapchainWidth, swapchainHeight});
    }

    for(auto &frame : m_frameResources) {
        ImageCreateInfo renderBufferCreateInfo{VK_IMAGE_TYPE_2D, RenderBufferFormat, m_swapchainSize};
        renderBufferCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if(!(frame.renderBuffer = m_device->createImage(renderBufferCreateInfo, VMA_MEMORY_USAGE_GPU_ONLY))) {
            qCCritical(logVulkan) << "Failed to create render buffer";
            return;
        }
    }

    auto *previousFrame = &m_frameResources[int(numConcurrentFrames()-1)];
    for(auto &frame : m_frameResources) {
        const QVector<WriteDescriptorSet> descriptorWrites = {
            { frame.displayDescriptorSet, 0, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, DescriptorImageInfo(frame.renderBuffer.view, ImageState::ShaderRead) },
            { frame.renderDescriptorSet, 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, DescriptorImageInfo(frame.renderBuffer.view, ImageState::ShaderReadWrite) },
            { frame.renderDescriptorSet, 2, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, DescriptorImageInfo(previousFrame->renderBuffer.view, ImageState::ShaderReadWrite) },
        };
        m_device->writeDescriptors(descriptorWrites);
        previousFrame = &frame;
    }

    m_clearPreviousRenderBuffer = true;
}

void Renderer::releaseSwapchainResources()
{
    for(auto &attachment : m_swapchainAttachments) {
        m_device->destroyImageView(attachment.image.view);
        m_device->destroyFramebuffer(attachment.framebuffer);
    }
    m_swapchainAttachments.clear();

    for(auto &frame : m_frameResources) {
        m_device->destroyImage(frame.renderBuffer);
    }
    m_renderBuffersReady = false;
}

bool Renderer::querySwapchainProperties(VkPhysicalDevice physicalDevice, VkSurfaceFormatKHR &surfaceFormat, int &minImageCount) const
{
    VkSurfaceKHR surface = QVulkanInstance::surfaceForWindow(m_window);

    Result result;

    VkSurfaceCapabilitiesKHR surfaceCaps;
    if(VKFAILED(result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCaps))) {
        qCCritical(logVulkan) << "Failed to query physical device surface capabilities" << result.toString();
        return false;
    }

    QVector<VkSurfaceFormatKHR> surfaceFormats;
    uint32_t surfaceFormatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatCount, nullptr);
    surfaceFormats.resize(int(surfaceFormatCount));
    if(VKFAILED(result = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatCount, surfaceFormats.data()))) {
        qCCritical(logVulkan) << "Failed to enumerate physical device surface formats" << result.toString();
        return false;
    }

    surfaceFormat = surfaceFormats[0];
    minImageCount = int(surfaceCaps.minImageCount);
    return true;
}

void Renderer::resizeSwapchain()
{
    Q_ASSERT(m_device);
    Q_ASSERT(m_window);

    if(m_swapchainSize != m_window->size()) {
        m_device->waitIdle();
        if(m_swapchain) {
            releaseSwapchainResources();
        }
        Swapchain newSwapchain = m_device->createSwapchain(m_window, m_swapchainFormat, uint32_t(numConcurrentFrames()), m_swapchain);
        if(newSwapchain) {
            m_device->destroySwapchain(m_swapchain);
            m_swapchain = newSwapchain;
            m_swapchainSize = m_window->size();
            createSwapchainResources();
        }
        else {
            qCWarning(logVulkan) << "Failed to resize swapchain";
        }
    }
}

bool Renderer::acquireNextSwapchainImage(uint32_t &imageIndex) const
{
    Result result;
    if(VKFAILED(result = vkAcquireNextImageKHR(*m_device, m_swapchain, UINT64_MAX, m_presentationFinishedSemaphore, VK_NULL_HANDLE, &imageIndex))) {
        if(result != VK_SUBOPTIMAL_KHR) {
            qCCritical(logVulkan) << "Failed to acquire next swapchain image:" << result.toString();
            return false;
        }
    }
    return true;
}

bool Renderer::submitFrameCommandsAndPresent(uint32_t imageIndex)
{
    const FrameResources &frame = m_frameResources[m_frameIndex];

    Result result;

    VkPipelineStageFlags submitWaitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &m_presentationFinishedSemaphore.handle;
    submitInfo.pWaitDstStageMask = &submitWaitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &frame.commandBuffer.handle;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &m_renderingFinishedSemaphore.handle;
    if(VKFAILED(result = vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, frame.commandBuffersExecutedFence))) {
        qCCritical(logVulkan) << "Failed to submit frame commands to the graphics queue:" << result.toString();
        return false;
    }

    m_frameIndex = (m_frameIndex + 1) % int(numConcurrentFrames());

    VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &m_renderingFinishedSemaphore.handle;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapchain.handle;
    presentInfo.pImageIndices = &imageIndex;
    result = vkQueuePresentKHR(m_graphicsQueue, &presentInfo);
    if(VKSUCCEEDED(result) || result == VK_SUBOPTIMAL_KHR) {
        Q_ASSERT(m_instance);
        m_instance->presentQueued(m_window);
    }
    else if(result != VK_ERROR_OUT_OF_DATE_KHR) {
        qCCritical(logVulkan) << "Failed to queue swapchain image for presentation:" << result.toString();
        return false;
    }

    return true;
}

void Renderer::renderFrame()
{
    resizeSwapchain();
    const QRect renderRect = { {0, 0}, m_swapchainSize };

    FrameResources &currentFrame = m_frameResources[currentFrameIndex()];
    FrameResources &previousFrame = m_frameResources[previousFrameIndex()];

    m_device->waitForFence(currentFrame.commandBuffersExecutedFence);
    m_device->resetFence(currentFrame.commandBuffersExecutedFence);

    updateRetiredResources();

    if(m_sceneResources.sceneTLAS) {
        m_device->writeDescriptor({currentFrame.renderDescriptorSet, 0, 0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV}, m_sceneResources.sceneTLAS);
    }
    m_commandBufferManager->submitCommandBuffers(m_graphicsQueue);

    uint32_t swapchainImageIndex = 0;

    CommandBuffer &commandBuffer = currentFrame.commandBuffer;
    commandBuffer.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    {
        if(!m_renderBuffersReady) {
            QVector<ImageTransition> transitions(static_cast<int>(numConcurrentFrames()));
            for(int i=0; i<int(numConcurrentFrames()); ++i) {
                transitions[i] = { m_frameResources[i].renderBuffer, ImageState::Undefined, ImageState::ShaderReadWrite, VK_IMAGE_ASPECT_COLOR_BIT };
            }
            commandBuffer.resourceBarrier(transitions);
            m_renderBuffersReady = true;
        }
        if(m_clearPreviousRenderBuffer) {
            commandBuffer.clearColorImage(previousFrame.renderBuffer, ImageState::ShaderReadWrite);
            m_clearPreviousRenderBuffer = false;
        }

        if(m_sceneResources.sceneTLAS) {
            commandBuffer.bindPipeline(m_rayTracingPipeline);
            commandBuffer.bindDescriptorSets(m_rayTracingPipeline, 0, {currentFrame.renderDescriptorSet});
            commandBuffer.traceRays(m_rayTracingPipeline, uint32_t(renderRect.width()), uint32_t(renderRect.height()));
        }

        commandBuffer.resourceBarrier({currentFrame.renderBuffer, ImageState::ShaderReadWrite, ImageState::ShaderRead});

        if(acquireNextSwapchainImage(swapchainImageIndex)) {
            const auto &attachment = m_swapchainAttachments[int(swapchainImageIndex)];
            commandBuffer.beginRenderPass({m_displayRenderPass, attachment.framebuffer, renderRect}, VK_SUBPASS_CONTENTS_INLINE);
            commandBuffer.bindPipeline(m_displayPipeline);
            commandBuffer.bindDescriptorSets(m_displayPipeline, 0, {currentFrame.displayDescriptorSet});
            commandBuffer.setViewport(renderRect);
            commandBuffer.setScissor(renderRect);
            commandBuffer.draw(3, 1);
            commandBuffer.endRenderPass();
        }

        commandBuffer.resourceBarrier({currentFrame.renderBuffer, ImageState::ShaderRead, ImageState::ShaderReadWrite});
    }
    commandBuffer.end();

    submitFrameCommandsAndPresent(swapchainImageIndex);

    m_commandBufferManager->proceedToNextFrame();
    m_frameAdvanceService->proceedToNextFrame();
}

VkPhysicalDevice Renderer::choosePhysicalDevice(const QByteArrayList &requiredExtensions, uint32_t &queueFamilyIndex) const
{
    Q_ASSERT(m_instance);

    VkPhysicalDevice selectedPhysicalDevice = VK_NULL_HANDLE;
    uint32_t selectedQueueFamilyIndex = uint32_t(-1);

    QVector<VkPhysicalDevice> physicalDevices;
    uint32_t physicalDeviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance->vkInstance(), &physicalDeviceCount, nullptr);
    if(physicalDeviceCount > 0) {
        physicalDevices.resize(int(physicalDeviceCount));
        if(VKFAILED(vkEnumeratePhysicalDevices(m_instance->vkInstance(), &physicalDeviceCount, physicalDevices.data()))) {
            qCWarning(logVulkan) << "Failed to enumerate available physical devices";
            return VK_NULL_HANDLE;
        }
    }
    else {
        qCWarning(logVulkan) << "No Vulkan capable physical devices found";
        return VK_NULL_HANDLE;
    }

    for(VkPhysicalDevice physicalDevice : physicalDevices) {
        QVector<VkQueueFamilyProperties> queueFamilies;
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
        if(queueFamilyCount == 0) {
            continue;
        }
        queueFamilies.resize(int(queueFamilyCount));
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

        selectedQueueFamilyIndex = uint32_t(-1);
        for(uint32_t index=0; index < queueFamilyCount; ++index) {
            const auto &queueFamily = queueFamilies[int(index)];

            constexpr VkQueueFlags RequiredQueueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
            if((queueFamily.queueFlags & RequiredQueueFlags) != RequiredQueueFlags) {
                continue;
            }
            if(!m_instance->supportsPresent(physicalDevice, index, m_window)) {
                continue;
            }
            selectedQueueFamilyIndex = index;
            break;
        }
        if(selectedQueueFamilyIndex == uint32_t(-1)) {
            continue;
        }

        QVector<VkExtensionProperties> extensions;
        uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
        if(extensionCount == 0) {
            continue;
        }
        extensions.resize(int(extensionCount));
        if(VKFAILED(vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, extensions.data()))) {
            qCWarning(logVulkan) << "Failed to enumerate device extensions for physical device:" << physicalDevice;
            continue;
        }

        bool allRequiredExtensionsFound = true;
        for(const QByteArray &requiredExtension : requiredExtensions) {
            bool extensionFound = false;
            for(const VkExtensionProperties &extension : extensions) {
                if(requiredExtension == extension.extensionName) {
                    extensionFound = true;
                    break;
                }
            }
            if(!extensionFound) {
                allRequiredExtensionsFound = false;
                break;
            }
        }
        if(!allRequiredExtensionsFound) {
            continue;
        }

        selectedPhysicalDevice = physicalDevice;
        break;
    }
    if(selectedPhysicalDevice == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    VkPhysicalDeviceProperties selectedDeviceProperties;
    vkGetPhysicalDeviceProperties(selectedPhysicalDevice, &selectedDeviceProperties);
    qCInfo(logVulkan) << "Selected physical device:" << selectedDeviceProperties.deviceName;

    queueFamilyIndex = selectedQueueFamilyIndex;
    return selectedPhysicalDevice;
}

RenderPass Renderer::createDisplayRenderPass(VkFormat swapchainFormat) const
{
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = swapchainFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    RenderPassCreateInfo createInfo;
    createInfo.attachmentCount = 1;
    createInfo.pAttachments = &colorAttachment;
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;

    RenderPass renderPass;
    if(!(renderPass = m_device->createRenderPass(createInfo))) {
        qCCritical(logVulkan) << "Could not create display render pass";
        return VK_NULL_HANDLE;
    }
    return renderPass;
}

int Renderer::currentFrameIndex() const
{
    return m_frameIndex;
}

int Renderer::previousFrameIndex() const
{
    return (m_frameIndex > 0) ? (m_frameIndex - 1) : (int(numConcurrentFrames()) - 1);
}

QSurface *Renderer::surface() const
{
    return m_window;
}

void Renderer::setSurface(QObject *surfaceObject)
{
    if(surfaceObject) {
        if(QWindow *window = qobject_cast<QWindow*>(surfaceObject)) {
            m_window = window;
        }
        else {
            qCWarning(logVulkan) << "Incompatible surface object: expected QWindow instance";
        }
    }
    else {
        m_window = nullptr;
    }
}

Device *Renderer::device() const
{
    Q_ASSERT(m_device);
    return m_device.get();
}

void Renderer::markDirty(DirtySet changes, Raytrace::BackendNode *node)
{
    Q_UNUSED(node);
    m_dirtySet |= changes;
}

QVector<Geometry> Renderer::geometry() const
{
    QMutexLocker lock(&m_sceneMutex);
    return m_sceneResources.geometry;
}

AccelerationStructure Renderer::sceneTLAS() const
{
    QMutexLocker lock(&m_sceneMutex);
    return m_sceneResources.sceneTLAS;
}

void Renderer::addGeometry(Qt3DCore::QNodeId geometryNodeId, const Geometry &geometry)
{
    QMutexLocker lock(&m_sceneMutex);

    DescriptorHandle geometryAttributesDescriptor = m_descriptorManager->allocateDescriptor(ResourceClass::AttributeBuffer);
    DescriptorHandle geometryIndicesDescriptor = m_descriptorManager->allocateDescriptor(ResourceClass::IndexBuffer);
    m_descriptorManager->updateBufferDescriptor(geometryAttributesDescriptor, DescriptorBufferInfo(geometry.attributes));
    m_descriptorManager->updateBufferDescriptor(geometryIndicesDescriptor, DescriptorBufferInfo(geometry.indices));

    uint32_t geometryIndex = uint32_t(m_sceneResources.geometry.size());
    m_sceneResources.geometry.append(geometry);
    m_sceneResources.geometryIndexLookup.insert(geometryNodeId, geometryIndex);
}

void Renderer::updateSceneTLAS(const AccelerationStructure &tlas)
{
    QMutexLocker lock(&m_sceneMutex);
    if(m_sceneResources.sceneTLAS) {
        RetiredResource<AccelerationStructure> retiredTLAS;
        retiredTLAS.resource = m_sceneResources.sceneTLAS;
        retiredTLAS.ttl = numConcurrentFrames();
        m_sceneResources.retiredTLAS.append(retiredTLAS);
    }
    m_sceneResources.sceneTLAS = tlas;
}

uint32_t Renderer::lookupGeometryBLAS(Qt3DCore::QNodeId geometryNodeId, uint64_t &blasHandle) const
{
    QMutexLocker lock(&m_sceneMutex);
    uint32_t index = m_sceneResources.geometryIndexLookup.value(geometryNodeId, ~0u);
    if(index != ~0u) {
        blasHandle = m_sceneResources.geometry[int(index)].blasHandle;
    }
    return index;
}

void Renderer::updateRetiredResources()
{
    QMutexLocker lock(&m_sceneMutex);
    for(auto &retiredTLAS : m_sceneResources.retiredTLAS) {
        retiredTLAS.updateTTL();
    }
}

void Renderer::destroyRetiredResources()
{
    QVector<AccelerationStructure> retiredTLAS;
    {
        QMutexLocker lock(&m_sceneMutex);
        QMutableVectorIterator<RetiredResource<AccelerationStructure>> it(m_sceneResources.retiredTLAS);
        while(it.hasNext()) {
            const auto &tlas = it.value();
            if(tlas.ttl == 0) {
                retiredTLAS.append(tlas.resource);
                it.remove();
            }
        }
    }
    for(auto &tlas : retiredTLAS) {
        m_device->destroyAccelerationStructure(tlas);
    }
}

Raytrace::Entity *Renderer::sceneRoot() const
{
    return m_sceneRoot;
}

void Renderer::setSceneRoot(Raytrace::Entity *rootEntity)
{
    m_sceneRoot = rootEntity;
    m_updateWorldTransformJob->setRoot(m_sceneRoot);
}

void Renderer::setNodeManagers(Raytrace::NodeManagers *nodeManagers)
{
    m_nodeManagers = nodeManagers;
}

Qt3DCore::QAbstractFrameAdvanceService *Renderer::frameAdvanceService() const
{
    return m_frameAdvanceService.get();
}

CommandBufferManager *Renderer::commandBufferManager() const
{
    return m_commandBufferManager.get();
}

DescriptorManager *Renderer::descriptorManager() const
{
    return m_descriptorManager.get();
}

QVector<Qt3DCore::QAspectJobPtr> Renderer::renderJobs()
{
    QVector<Qt3DCore::QAspectJobPtr> jobs;
    bool shouldUpdateTLAS = false;

    if(m_dirtySet & DirtyFlag::TransformDirty) {
        jobs.append(m_updateWorldTransformJob);
        shouldUpdateTLAS = true;
    }

    QVector<Qt3DCore::QAspectJobPtr> geometryJobs;
    if(m_dirtySet & DirtyFlag::GeometryDirty) {
        geometryJobs = createGeometryJobs();
        jobs.append(geometryJobs);
        shouldUpdateTLAS = true;
    }

    if(shouldUpdateTLAS) {
        Qt3DCore::QAspectJobPtr buildSceneTLASJob = BuildSceneTopLevelAccelerationStructureJobPtr::create(this, m_nodeManagers);
        buildSceneTLASJob->addDependency(m_updateWorldTransformJob);
        for(const auto &job : geometryJobs) {
            buildSceneTLASJob->addDependency(job);
        }
        jobs.append(buildSceneTLASJob);
    }

    jobs.append(m_destroyRetiredResourcesJob);

    m_dirtySet = DirtyFlag::NoneDirty;
    return jobs;
}

uint32_t Renderer::numConcurrentFrames() const
{
    return uint32_t(m_frameResources.size());
}

} // Vulkan
} // Qt3DRaytrace
