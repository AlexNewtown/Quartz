/*
 * Copyright (C) 2018 Michał Siejak
 * This file is part of Quartz - a raytracing aspect for Qt3D.
 * See LICENSE file for licensing information.
 */

#pragma once

#include <qtgui-config.h>
#include <Qt3DCore/qt3dcore_global.h>

#if !QT_CONFIG(vulkan)
#error "Quartz requires Qt version with Vulkan support enabled at compile time."
#endif

#if defined(QUARTZ_SHARED)
#  if defined(QT3DRAYTRACE_LIBRARY)
#    define QT3DRAYTRACESHARED_EXPORT Q_DECL_EXPORT
#  else
#    define QT3DRAYTRACESHARED_EXPORT Q_DECL_IMPORT
#  endif
#else
#  define QT3DRAYTRACESHARED_EXPORT
#endif
