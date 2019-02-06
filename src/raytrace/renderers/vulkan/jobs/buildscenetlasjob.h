/*
 * Copyright (C) 2018-2019 Michał Siejak
 * This file is part of Quartz - a raytracing aspect for Qt3D.
 * See LICENSE file for licensing information.
 */

#pragma once

#include <renderers/vulkan/vkcommon.h>
#include <renderers/vulkan/geometry.h>
#include <Qt3DCore/QAspectJob>

namespace Qt3DRaytrace {
namespace Vulkan {

class Renderer;

class BuildSceneTopLevelAccelerationStructureJob final : public Qt3DCore::QAspectJob
{
public:
    BuildSceneTopLevelAccelerationStructureJob(Renderer *renderer);

    void run() override;

private:
    QVector<GeometryInstance> gatherGeometryInstances() const;

    Renderer *m_renderer;
};

using BuildSceneTopLevelAccelerationStructureJobPtr = QSharedPointer<BuildSceneTopLevelAccelerationStructureJob>;

} // Vulkan
} // Qt3DRaytrace
