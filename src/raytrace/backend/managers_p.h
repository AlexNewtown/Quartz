/*
 * Copyright (C) 2018-2019 Michał Siejak
 * This file is part of Quartz - a raytracing aspect for Qt3D.
 * See LICENSE file for licensing information.
 */

#pragma once

#include <private/qresourcemanager_p.h>

#include <backend/entity_p.h>
#include <backend/transform_p.h>

namespace Qt3DRaytrace {
namespace Raytrace {

class EntityManager : public Qt3DCore::QResourceManager<Entity, Qt3DCore::QNodeId>
{
public:
    ~EntityManager()
    {
        Allocator::for_each([](Entity *e) {
            if(e) {
                e->m_nodeManagers = nullptr;
            }
        });
    }
};

using TransformManager = Qt3DCore::QResourceManager<Transform, Qt3DCore::QNodeId>;

struct NodeManagers
{
    EntityManager entityManager;
    TransformManager transformManager;
};

} // Raytrace
} // Qt3DRaytrace