/*
 * Copyright (C) 2018-2019 Michał Siejak
 * This file is part of Quartz - a raytracing aspect for Qt3D.
 * See LICENSE file for licensing information.
 */

#include <backend/entity_p.h>
#include <backend/managers_p.h>

#include <Qt3DCore/private/qentity_p.h>

#include <Qt3DCore/QComponentAddedChange>
#include <Qt3DCore/QComponentRemovedChange>
#include <Qt3DCore/QPropertyNodeAddedChange>
#include <Qt3DCore/QPropertyNodeRemovedChange>

#include <Qt3DRaytrace/qgeometryrenderer.h>
#include <Qt3DRaytrace/qmaterial.h>
#include <Qt3DRaytrace/qcameralens.h>

using namespace Qt3DCore;

namespace Qt3DRaytrace {
namespace Raytrace {

EntityMapper::EntityMapper(NodeManagers *managers, AbstractRenderer *renderer)
    : m_nodeManagers(managers)
    , m_renderer(renderer)
{}

QBackendNode *EntityMapper::create(const Qt3DCore::QNodeCreatedChangeBasePtr &change) const
{
    EntityManager &entityManager = m_nodeManagers->entityManager;
    HEntity entityHandle = entityManager.getOrAcquireHandle(change->subjectId());
    Entity *entity = entityManager.data(entityHandle);
    entity->m_nodeManagers = m_nodeManagers;
    entity->m_handle = entityHandle;
    entity->setRenderer(m_renderer);
    return entity;
}

QBackendNode *EntityMapper::get(Qt3DCore::QNodeId id) const
{
    Q_ASSERT(m_nodeManagers);
    return m_nodeManagers->entityManager.lookupResource(id);
}

void EntityMapper::destroy(Qt3DCore::QNodeId id) const
{
    Q_ASSERT(m_nodeManagers);
    m_nodeManagers->entityManager.releaseResource(id);
}

Entity::~Entity()
{
    if(m_nodeManagers) {
        setParentHandle(HEntity());
    }
}

void Entity::setParentHandle(HEntity parentHandle)
{
    Q_ASSERT(m_nodeManagers);

    EntityManager &entityManager = m_nodeManagers->entityManager;
    if(parentHandle != m_parentHandle) {
        if(Entity *parent = entityManager.data(m_parentHandle)) {
            parent->removeChildHandle(m_handle);
        }
        if(Entity *parent = entityManager.data(parentHandle)) {
            parent->appendChildHandle(m_handle);
        }
    }
}

Entity *Entity::parent() const
{
    Q_ASSERT(m_nodeManagers);
    return m_nodeManagers->entityManager.data(m_parentHandle);
}

void Entity::appendChildHandle(HEntity childHandle)
{
    Q_ASSERT(m_nodeManagers);
    if(!m_childrenHandles.contains(childHandle)) {
        m_childrenHandles.append(childHandle);
        Entity *child = m_nodeManagers->entityManager.data(childHandle);
        if(child) {
            child->m_parentHandle = m_handle;
        }
    }
}

void Entity::removeChildHandle(HEntity childHandle)
{
    Q_ASSERT(m_nodeManagers);
    if(m_childrenHandles.contains(childHandle)) {
        m_childrenHandles.removeAll(childHandle);
        Entity *child = m_nodeManagers->entityManager.data(childHandle);
        if(child) {
            child->m_parentHandle = HEntity{};
        }
    }
    m_childrenHandles.removeAll(childHandle);
}

QVector<Entity*> Entity::children() const
{
    Q_ASSERT(m_nodeManagers);

    QVector<Entity*> result;
    result.reserve(m_childrenHandles.size());
    for(const HEntity &handle : m_childrenHandles) {
        Entity *child = m_nodeManagers->entityManager.data(handle);
        if(child) {
            result.append(child);
        }
    }
    return result;
}

void Entity::addComponent(QNodeIdTypePair idAndType)
{
    const auto type = idAndType.type;
    const auto id = idAndType.id;

    if(type->inherits(&Qt3DCore::QTransform::staticMetaObject)) {
        m_transformComponent = id;
    }
    else if(type->inherits(&Qt3DRaytrace::QGeometryRenderer::staticMetaObject)) {
        m_geometryRendererComponent = id;
    }
    else if(type->inherits(&Qt3DRaytrace::QMaterial::staticMetaObject)) {
        m_materialComponent = id;
    }
    else if(type->inherits(&Qt3DRaytrace::QCameraLens::staticMetaObject)) {
        m_cameraLensComponent = id;
    }
}

void Entity::removeComponent(QNodeId nodeId)
{
    if(nodeId == m_transformComponent) {
        m_transformComponent = QNodeId{};
    }
    else if(nodeId == m_geometryRendererComponent) {
        m_geometryRendererComponent = QNodeId{};
    }
    else if(nodeId == m_materialComponent) {
        m_materialComponent = QNodeId{};
    }
    else if(nodeId == m_cameraLensComponent) {
        m_cameraLensComponent = QNodeId{};
    }
}

Transform *Entity::transformComponent() const
{
    Q_ASSERT(m_nodeManagers);
    return m_nodeManagers->transformManager.lookupResource(m_transformComponent);
}

GeometryRenderer *Entity::geometryRendererComponent() const
{
    Q_ASSERT(m_nodeManagers);
    return m_nodeManagers->geometryRendererManager.lookupResource(m_geometryRendererComponent);
}

Material *Entity::materialComponent() const
{
    Q_ASSERT(m_nodeManagers);
    return m_nodeManagers->materialManager.lookupResource(m_materialComponent);
}

CameraLens *Entity::cameraLensComponent() const
{
    Q_ASSERT(m_nodeManagers);
    return m_nodeManagers->cameraManager.lookupResource(m_cameraLensComponent);
}

bool Entity::isRenderable() const
{
    if(m_geometryRendererComponent.isNull() || m_materialComponent.isNull()) {
        return false;
    }
    return !geometryRendererComponent()->geometryId().isNull();
}

bool Entity::isCamera() const
{
    return !m_transformComponent.isNull() && !m_cameraLensComponent.isNull();
}

void Entity::sceneChangeEvent(const QSceneChangePtr &changeEvent)
{
    switch(changeEvent->type()) {
    case ComponentAdded: {
        QComponentAddedChangePtr change = qSharedPointerCast<QComponentAddedChange>(changeEvent);
        addComponent(QNodeIdTypePair{change->componentId(), change->componentMetaObject()});
        markDirty(AbstractRenderer::AllDirty);
        break;
    }
    case ComponentRemoved: {
        QComponentRemovedChangePtr change = qSharedPointerCast<QComponentRemovedChange>(changeEvent);
        removeComponent(change->componentId());
        markDirty(AbstractRenderer::AllDirty);
        break;
    }
    case PropertyValueAdded: {
        QPropertyNodeAddedChangePtr change = qSharedPointerCast<QPropertyNodeAddedChange>(changeEvent);
        if(change->metaObject()->inherits(&QEntity::staticMetaObject)) {
            appendChildHandle(m_nodeManagers->entityManager.lookupHandle(change->addedNodeId()));
            markDirty(AbstractRenderer::AllDirty);
        }
        break;
    }
    case PropertyValueRemoved: {
        QPropertyNodeRemovedChangePtr change = qSharedPointerCast<QPropertyNodeRemovedChange>(changeEvent);
        if(change->metaObject()->inherits(&QEntity::staticMetaObject)) {
            removeChildHandle(m_nodeManagers->entityManager.lookupHandle(change->removedNodeId()));
            markDirty(AbstractRenderer::AllDirty);
        }
        break;
    }
    default:
        break;
    }

    BackendNode::sceneChangeEvent(changeEvent);
}

void Entity::initializeFromPeer(const QNodeCreatedChangeBasePtr &change)
{
    const auto typedChange = qSharedPointerCast<QNodeCreatedChange<Qt3DCore::QEntityData>>(change);
    const auto &data = typedChange->data;

    m_transformComponent = QNodeId{};
    m_geometryRendererComponent = QNodeId{};
    m_materialComponent = QNodeId{};
    m_cameraLensComponent = QNodeId{};

    for(const auto &idAndType : qAsConst(data.componentIdsAndTypes)) {
        addComponent(idAndType);
    }

    if(!data.parentEntityId.isNull()) {
        setParentHandle(m_nodeManagers->entityManager.lookupHandle(data.parentEntityId));
    }
}

} // Raytrace
} // Qt3DRaytrace
