/*
 * Copyright (C) 2018-2019 Michał Siejak
 * This file is part of Quartz - a raytracing aspect for Qt3D.
 * See LICENSE file for licensing information.
 */

#pragma once

#include <Qt3DRaytraceExtras/qt3draytraceextras_global.h>

#include <QWindow>
#include <QString>

class QVulkanInstance;

namespace Qt3DCore {
class QEntity;
class QAbstractAspect;
} // Qt3DCore

namespace Qt3DInput {
class QInputAspect;
} // Qt3DInput

namespace Qt3DRaytrace {
class QRaytraceAspect;
} // Qt3DRaytrace

namespace Qt3DRaytraceExtras {

class Qt3DWindowPrivate;

class QT3DRAYTRACEEXTRASSHARED_EXPORT Qt3DWindow : public QWindow
{
    Q_OBJECT
    Q_DECLARE_PRIVATE(Qt3DWindow)
public:
    explicit Qt3DWindow(QWindow *parent = nullptr);

    void registerAspect(Qt3DCore::QAbstractAspect *aspect);
    void registerAspect(const QString &name);

    Qt3DRaytrace::QRaytraceAspect *raytraceAspect() const;
    Qt3DInput::QInputAspect *inputAspect() const;

    void setRootEntity(Qt3DCore::QEntity *root);

signals:
    void aboutToClose();

protected:
    Qt3DWindow(Qt3DWindowPrivate &dd, QWindow *parent);

    virtual bool event(QEvent *event) override;
    virtual void showEvent(QShowEvent *event) override;
};

} // Qt3DRaytraceExtras
