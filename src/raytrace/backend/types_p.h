/*
 * Copyright (C) 2018-2019 Michał Siejak
 * This file is part of Quartz - a raytracing aspect for Qt3D.
 * See LICENSE file for licensing information.
 */

#pragma once

#include <qt3draytrace_global_p.h>

#include <QColor>

namespace Qt3DRaytrace {
namespace Raytrace {

struct LinearColor
{
    LinearColor()
        : r(0.0f), g(0.0f), b(0.0f)
    {}
    LinearColor(float r, float g, float b)
        : r(r), g(g), b(b)
    {}
    LinearColor(const QColor &c, float intensity=1.0f)
        : r(float(c.redF()) * intensity)
        , g(float(c.greenF()) * intensity)
        , b(float(c.blueF()) * intensity)
    {}

    bool isBlack() const
    {
        float maxComponent = std::max(std::max(r, g), b);
        return qFuzzyIsNull(maxComponent);
    }

    void writeToBuffer(float *buffer) const
    {
        buffer[0] = r;
        buffer[1] = g;
        buffer[2] = b;
    }

    float r, g, b;
};

} // Raytrace
} // Qt3DRaytrace
