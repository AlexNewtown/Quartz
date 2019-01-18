/*
 * Copyright (C) 2018-2019 Michał Siejak
 * This file is part of Quartz - a raytracing aspect for Qt3D.
 * See LICENSE file for licensing information.
 */

#pragma once

#include <qt3draytrace_global_p.h>
#include <backend/abstractrenderer_p.h>

#include <Qt3DCore/QBackendNode>

namespace Qt3DRaytrace {
namespace Raytrace {

class BackendNode : public Qt3DCore::QBackendNode
{
public:
    explicit BackendNode(Qt3DCore::QBackendNode::Mode mode = ReadOnly);

    void setRenderer(AbstractRenderer *renderer)
    {
        m_renderer = renderer;
    }

protected:
    void markDirty(AbstractRenderer::DirtySet changes);

    AbstractRenderer *m_renderer = nullptr;
};

template<typename BackendNodeType, typename ManagerType>
class BackendNodeMapper : public Qt3DCore::QBackendNodeMapper
{
public:
    BackendNodeMapper(ManagerType *manager, AbstractRenderer *renderer)
        : m_manager(manager)
        , m_renderer(renderer)
    {}

    virtual Qt3DCore::QBackendNode *create(const Qt3DCore::QNodeCreatedChangeBasePtr &change) const override
    {
        BackendNodeType *backendNode = m_manager->getOrCreateResource(change->subjectId());
        backendNode->setRenderer(m_renderer);
        return backendNode;
    }

    Qt3DCore::QBackendNode *get(Qt3DCore::QNodeId id) const override
    {
        return m_manager->lookupResource(id);
    }

    void destroy(Qt3DCore::QNodeId id) const override
    {
        m_manager->releaseResource(id);
    }

protected:
    ManagerType *m_manager;
    AbstractRenderer *m_renderer;
};

} // Raytrace
} // Qt3DRaytrace
