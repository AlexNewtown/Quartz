/*
 * Copyright (C) 2018 Michał Siejak
 * This file is part of Quartz - a raytracing aspect for Qt3D.
 * See LICENSE file for licensing information.
 */

#pragma once

#include <Qt3DRaytrace/qt3draytrace_global.h>
#include <Qt3DCore/qabstractaspect.h>

namespace Qt3DRaytrace {

class QRendererInterface;
class QRaytraceAspectPrivate;

class QT3DRAYTRACESHARED_EXPORT QRaytraceAspect : public Qt3DCore::QAbstractAspect
{
    Q_OBJECT
public:
    explicit QRaytraceAspect(QObject *parent = nullptr);

    QRendererInterface *renderer() const;

public slots:
    void suspendJobs();
    void resumeJobs();

protected:
    QRaytraceAspect(QRaytraceAspectPrivate &dd, QObject *parent);
    Q_DECLARE_PRIVATE(QRaytraceAspect)

private:
    QVector<Qt3DCore::QAspectJobPtr> jobsToExecute(qint64 time) override;

    void onRegistered() override;
    void onUnregistered() override;
    void onEngineStartup() override;
    void onEngineShutdown() override;
};

} // Qt3DRaytrace
