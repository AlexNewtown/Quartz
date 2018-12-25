/*
 * Copyright (C) 2018 Michał Siejak
 * This file is part of Quartz - a raytracing aspect for Qt3D.
 * See LICENSE file for licensing information.
 */

#include <Qt3DRaytrace/qraytraceaspect.h>
#include <qraytraceaspect_p.h>

#include <Qt3DCore/private/qservicelocator_p.h>
#include <Qt3DCore/private/qabstractframeadvanceservice_p.h>

using namespace Qt3DCore;

namespace Qt3DRaytrace {

QRaytraceAspectPrivate::QRaytraceAspectPrivate()
    : m_renderer(nullptr)
{}

QRaytraceAspect::QRaytraceAspect(QObject *parent)
    : QRaytraceAspect(*new QRaytraceAspectPrivate, parent)
{}

QAbstractRenderer *QRaytraceAspect::renderer() const
{
    Q_D(const QRaytraceAspect);
    return d->m_renderer;
}

void QRaytraceAspect::setRenderer(QAbstractRenderer *renderer)
{
    Q_D(QRaytraceAspect);
    d->m_renderer = renderer;
    updateServiceProviders();
}

QRaytraceAspect::QRaytraceAspect(QRaytraceAspectPrivate &dd, QObject *parent)
    : QAbstractAspect(dd, parent)
{
    setObjectName(QStringLiteral("Raytrace Aspect"));
}

QVector<QAspectJobPtr> QRaytraceAspect::jobsToExecute(qint64 time)
{
    QVector<QAspectJobPtr> jobs;
    return jobs;
}

void QRaytraceAspect::onRegistered()
{
    updateServiceProviders();
}

void QRaytraceAspect::onUnregistered()
{
}

void QRaytraceAspect::updateServiceProviders()
{
    Q_D(QRaytraceAspect);

    if(!d->m_aspectManager) {
        return;
    }
    if(d->m_renderer) {
        QAbstractFrameAdvanceService *advanceService = d->m_renderer->frameAdvanceService();
        if(advanceService) {
            d->services()->registerServiceProvider(Qt3DCore::QServiceLocator::FrameAdvanceService, advanceService);
        }
    }
}

} // Qt3DRaytrace
