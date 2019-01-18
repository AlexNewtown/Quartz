/*
 * Copyright (C) 2018 Michał Siejak
 * This file is part of Quartz - a raytracing aspect for Qt3D.
 * See LICENSE file for licensing information.
 */

#pragma once

#include <qt3draytrace_global_p.h>
#include <Qt3DRaytrace/qrendererinterface.h>

#include <QVector>
#include <Qt3DCore/QAspectJob>

class QSurface;

namespace Qt3DCore {
class QAbstractFrameAdvanceService;
} // Qt3DCore

namespace Qt3DRaytrace {
namespace Raytrace {

class BackendNode;
class Entity;

class AbstractRenderer : public QRendererInterface
{
public:
    enum DirtyFlag {
        TransformDirty = 1 << 0,
        GeometryDirty  = 1 << 1,
        NoneDirty      = 0,
        AllDirty       = 0xffffff,
    };
    Q_DECLARE_FLAGS(DirtySet, DirtyFlag)

    virtual ~AbstractRenderer() = default;

    virtual void markDirty(DirtySet changes, BackendNode *node) = 0;

    virtual Entity *sceneRoot() const = 0;
    virtual void setSceneRoot(Entity *rootEntity) = 0;

    virtual Qt3DCore::QAbstractFrameAdvanceService *frameAdvanceService() const = 0;
    virtual QVector<Qt3DCore::QAspectJobPtr> renderJobs() = 0;
};

} // Raytrace
} // Qt3DRaytrace
