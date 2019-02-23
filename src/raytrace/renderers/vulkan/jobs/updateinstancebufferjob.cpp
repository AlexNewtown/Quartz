/*
 * Copyright (C) 2018-2019 Michał Siejak
 * This file is part of Quartz - a raytracing aspect for Qt3D.
 * See LICENSE file for licensing information.
 */

#include <renderers/vulkan/jobs/updateinstancebufferjob.h>
#include <renderers/vulkan/renderer.h>

#include <backend/managers_p.h>

using namespace Qt3DCore;

namespace Qt3DRaytrace {
namespace Vulkan {

UpdateInstanceBufferJob::UpdateInstanceBufferJob(Renderer *renderer)
    : m_renderer(renderer)
{
    Q_ASSERT(m_renderer);
}

void UpdateInstanceBufferJob::run()
{
    auto *device = m_renderer->device();
    auto *commandBufferManager = m_renderer->commandBufferManager();
    auto *sceneManager = m_renderer->sceneManager();

    const auto &renderables = sceneManager->renderables();
    Q_ASSERT(renderables.size() > 0);

    const uint32_t instanceCount = uint32_t(renderables.size());
    const VkDeviceSize instanceBufferSize = sizeof(EntityInstance) * instanceCount;

    BufferCreateInfo instanceBufferCreateInfo;
    instanceBufferCreateInfo.size = instanceBufferSize;
    instanceBufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    Buffer instanceBuffer = device->createBuffer(instanceBufferCreateInfo, VMA_MEMORY_USAGE_GPU_ONLY);
    if(!instanceBuffer) {
        qCCritical(logVulkan) << "Failed to create instance buffer";
        return;
    }

    Buffer stagingBuffer = device->createStagingBuffer(instanceBufferSize);
    if(!stagingBuffer || !stagingBuffer.isHostAccessible()) {
        qCCritical(logVulkan) << "Failed to create staging buffer for instance data";
        device->destroyBuffer(instanceBuffer);
        return;
    }

    EntityInstance *instanceData = stagingBuffer.memory<EntityInstance>();
    for(uint32_t instanceIndex=0; instanceIndex < instanceCount; ++instanceIndex) {
        const Raytrace::Entity *renderable = renderables[int(instanceIndex)].data();
        const Raytrace::GeometryRenderer *geometryRenderer = renderable->geometryRendererComponent();
        Q_ASSERT(geometryRenderer);

        EntityInstance &instance = instanceData[instanceIndex];
        instance.materialIndex = sceneManager->lookupMaterialIndex(renderable->materialComponentId());

        Geometry renderableGeometry;
        instance.geometryIndex = sceneManager->lookupGeometry(geometryRenderer->geometryId(), renderableGeometry);
        instance.geometryNumFaces = renderableGeometry.numIndices / 3;

        const QMatrix4x4 entityTransform = renderable->worldTransformMatrix.toQMatrix4x4();
        instance.transform = entityTransform;
        instance.basisTransform = entityTransform.normalMatrix();
    }

    TransientCommandBuffer commandBuffer = commandBufferManager->acquireCommandBuffer();
    {
        commandBuffer->copyBuffer(stagingBuffer, 0, instanceBuffer, 0, instanceBufferSize);
        commandBuffer->resourceBarrier({instanceBuffer, BufferState::CopyDest, BufferState::ShaderRead});
    }
    commandBufferManager->releaseCommandBuffer(commandBuffer, QVector<Buffer>{stagingBuffer});

    sceneManager->updateInstanceBuffer(instanceBuffer);
}

} // Vulkan
} // Qt3DRaytrace
