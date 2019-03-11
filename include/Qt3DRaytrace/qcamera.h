/*
 * Copyright (C) 2018-2019 Michał Siejak
 * This file is part of Quartz - a raytracing aspect for Qt3D.
 * See LICENSE file for licensing information.
 */

#pragma once

#include <Qt3DRaytrace/qt3draytrace_global.h>

#include <Qt3DCore/QEntity>
#include <QVector3D>
#include <QQuaternion>

namespace Qt3DCore {
class QTransform;
};

namespace Qt3DRaytrace {

class QCameraLens;
class QCameraPrivate;

class QT3DRAYTRACESHARED_EXPORT QCamera : public Qt3DCore::QEntity
{
    Q_OBJECT
    // QCamera
    Q_PROPERTY(QVector3D position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(QQuaternion rotation READ rotation WRITE setRotation NOTIFY rotationChanged)
    Q_PROPERTY(float rotationPitch READ rotationPitch WRITE setRotationPitch NOTIFY rotationPitchChanged)
    Q_PROPERTY(float rotationYaw READ rotationYaw WRITE setRotationYaw NOTIFY rotationYawChanged)
    Q_PROPERTY(float rotationRoll READ rotationRoll WRITE setRotationRoll NOTIFY rotationRollChanged)
    Q_PROPERTY(QVector3D lookAtTarget READ lookAtTarget WRITE setLookAtTarget NOTIFY lookAtTargetChanged)
    Q_PROPERTY(QVector3D lookAtUp READ lookAtUp WRITE setLookAtUp NOTIFY lookAtUpChanged)
    // QCameraLens
    Q_PROPERTY(float aspectRatio READ aspectRatio WRITE setAspectRatio NOTIFY aspectRatioChanged)
    Q_PROPERTY(float fieldOfView READ fieldOfView WRITE setFieldOfView NOTIFY fieldOfViewChanged)
    Q_PROPERTY(float lensDiameter READ lensDiameter WRITE setLensDiameter NOTIFY lensDiameterChanged)
    Q_PROPERTY(float lensFocalDistance READ lensFocalDistance WRITE setLensFocalDistance NOTIFY lensFocalDistanceChanged)
    Q_PROPERTY(float gamma READ gamma WRITE setGamma NOTIFY gammaChanged)
    Q_PROPERTY(float exposure READ exposure WRITE setExposure NOTIFY exposureChanged)
    Q_PROPERTY(float tonemapFactor READ tonemapFactor WRITE setTonemapFactor NOTIFY tonemapFactorChanged)
public:
    explicit QCamera(Qt3DCore::QNode *parent = nullptr);

    QCameraLens *lens() const;
    Qt3DCore::QTransform *transform() const;

    QVector3D position() const;
    QQuaternion rotation() const;

    float rotationPitch() const;
    float rotationYaw() const;
    float rotationRoll() const;

    QVector3D lookAtTarget() const;
    QVector3D lookAtUp() const;

    float aspectRatio() const;
    float fieldOfView() const;
    float lensDiameter() const;
    float lensFocalDistance() const;
    float gamma() const;
    float exposure() const;
    float tonemapFactor() const;

    Q_INVOKABLE void translate(const QVector3D &t);
    Q_INVOKABLE void translateWorld(const QVector3D &t);
    Q_INVOKABLE void rotate(const QQuaternion &q);

    Q_INVOKABLE void tilt(float angle);
    Q_INVOKABLE void pan(float angle);
    Q_INVOKABLE void roll(float angle);
    Q_INVOKABLE void tiltWorld(float angle);
    Q_INVOKABLE void panWorld(float angle);
    Q_INVOKABLE void rollWorld(float angle);

    Q_INVOKABLE void setLensFocalRatio(float fstop);

public slots:
    void setPosition(const QVector3D &position);
    void setRotation(const QQuaternion &rotation);

    void setRotationPitch(float rotationPitch);
    void setRotationYaw(float rotationYaw);
    void setRotationRoll(float rotationRoll);

    void setLookAtTarget(const QVector3D &lookAtTarget);
    void setLookAtUp(const QVector3D &lookAtUp);

    void setAspectRatio(float aspectRatio);
    void setFieldOfView(float fov);
    void setLensDiameter(float diameter);
    void setLensFocalDistance(float distance);
    void setGamma(float gamma);
    void setExposure(float exposure);
    void setTonemapFactor(float factor);

signals:
    void positionChanged(const QVector3D &position);
    void rotationChanged(const QQuaternion &rotation);

    void rotationPitchChanged(float rotationPitch);
    void rotationYawChanged(float rotationYaw);
    void rotationRollChanged(float rotationRoll);

    void lookAtTargetChanged(const QVector3D &lookAtTarget);
    void lookAtUpChanged(const QVector3D &lookAtUp);

    void aspectRatioChanged(float aspectRatio);
    void fieldOfViewChanged(float fov);
    void lensDiameterChanged(float diameter);
    void lensFocalDistanceChanged(float distance);
    void gammaChanged(float gamma);
    void exposureChanged(float exposure);
    void tonemapFactorChanged(float factor);

protected:
    explicit QCamera(QCameraPrivate &dd, Qt3DCore::QNode *parent = nullptr);
    Q_DECLARE_PRIVATE(QCamera)
};

} // Qt3DRaytrace
