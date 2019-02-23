/*
 * Copyright (C) 2018-2019 Michał Siejak
 * This file is part of Quartz - a raytracing aspect for Qt3D.
 * See LICENSE file for licensing information.
 */

#include <io/defaultimageimporter_p.h>

#include <QFile>

// NOTE: Qt's own QImage lacks support for HDR formats, hence usage of stb_image.
// TODO: Implement QImageReader for Radiance RGBE format, and possibly others.
#include <stb_image.h>

namespace Qt3DRaytrace {
namespace Raytrace {

DefaultImageImporter::DefaultImageImporter()
{
    stbi_set_flip_vertically_on_load(1);
}

bool DefaultImageImporter::import(const QUrl &url, QImageData &data)
{
    QByteArray imageFileBytes;
    {
        QFile imageFile(url.path());
        if(!imageFile.open(QFile::ReadOnly)) {
            qCCritical(logImport) << "Cannot open image file:" << url.toString();
            return false;
        }

        qCInfo(logImport) << "Loading texture image:" << url.toString();
        imageFileBytes = imageFile.readAll();
        if(imageFileBytes.size() == 0) {
            qCCritical(logImport) << "Failed to read image file:" << url.toString();
            return false;
        }
    }

    const stbi_uc *compressedData = reinterpret_cast<const stbi_uc*>(imageFileBytes.constData());
    const int compressedDataSize = imageFileBytes.size();

    int imageWidth, imageHeight, imageChannels;
    if(!stbi_info_from_memory(compressedData, compressedDataSize, &imageWidth, &imageHeight, &imageChannels)) {
        qCCritical(logImport) << "Failed to query image file properties:" << url.toString();
        return false;
    }

    if(stbi_is_hdr_from_memory(compressedData, compressedDataSize) == 1) {
        float *image = stbi_loadf_from_memory(compressedData, compressedDataSize, &data.imageWidth, &data.imageHeight, &data.numChannels, 0);
        if(image) {
            const int imageSize = data.imageWidth * data.imageHeight * data.numChannels * sizeof(float);
            data.type = QImageDataType::Float32;
            data.data = QByteArray(reinterpret_cast<const char*>(image), imageSize);
            stbi_image_free(image);
            return true;
        }
    }
    else {
        // Expand RGB LDR images to RGBA.
        if(imageChannels == 3) {
            imageChannels = 4;
        }
        data.numChannels = imageChannels;

        int numActualChannels;
        stbi_uc *image = stbi_load_from_memory(compressedData, compressedDataSize, &data.imageWidth, &data.imageHeight, &numActualChannels, imageChannels);
        if(image) {
            const int imageSize = data.imageWidth * data.imageHeight * data.numChannels;
            data.type = QImageDataType::UInt8;
            data.data = QByteArray(reinterpret_cast<const char*>(image), imageSize);
            stbi_image_free(image);
            return true;
        }
    }

    qCCritical(logImport) << "Failed to import texture image file:" << url.toString();
    return false;
}

} // Raytrace
} // Qt3DRaytrace
