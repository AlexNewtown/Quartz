/*
 * Copyright (C) 2018-2019 Michał Siejak
 * This file is part of Quartz - a raytracing aspect for Qt3D.
 * See LICENSE file for licensing information.
 */

#include <io/defaultmeshimporter_p.h>

#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/Importer.hpp>
#include <assimp/LogStream.hpp>
#include <assimp/DefaultLogger.hpp>

#include <QMutex>
#include <QMutexLocker>

namespace Qt3DRaytrace {

Q_LOGGING_CATEGORY(logImport, "raytrace.import")

namespace Raytrace {

static constexpr unsigned int ImportFlags =
        aiProcess_CalcTangentSpace |
        aiProcess_Triangulate |
        aiProcess_SortByPType |
        aiProcess_PreTransformVertices |
        aiProcess_GenNormals |
        aiProcess_GenUVCoords |
        aiProcess_OptimizeMeshes |
        aiProcess_Debone |
        aiProcess_ValidateDataStructure;

class LogStream final : public Assimp::LogStream
{
public:
    static void initialize()
    {
        QMutexLocker lock(&LoggerInitMutex);
        if(Assimp::DefaultLogger::isNullLogger()) {
            Assimp::DefaultLogger::create("", Assimp::Logger::VERBOSE);
            Assimp::DefaultLogger::get()->attachStream(new LogStream, Assimp::Logger::Err | Assimp::Logger::Warn);
        }
    }
    void write(const char *message) override
    {
        qCWarning(logImport) << message;
    }

private:
    LogStream() = default;
    static QMutex LoggerInitMutex;
};

QMutex LogStream::LoggerInitMutex;

static bool importScene(const aiScene *scene, QGeometryData &data)
{
    int totalNumVertices = 0;
    int totalNumFaces = 0;

    QVector<quint32> meshBaseIndices;
    meshBaseIndices.reserve(int(scene->mNumMeshes));
    for(int i=0; i<int(scene->mNumMeshes); ++i) {
        const aiMesh *mesh = scene->mMeshes[i];
        if(mesh->HasPositions() && mesh->HasNormals()) {
            meshBaseIndices.append(quint32(totalNumVertices));
            totalNumVertices += int(mesh->mNumVertices);
            totalNumFaces += int(mesh->mNumFaces);
        }
    }
    if(totalNumVertices == 0 || totalNumFaces == 0) {
        return false;
    }

    data.vertices.resize(totalNumVertices);
    data.faces.resize(totalNumFaces);

    for(int i=0, vertexIndex=0, triangleIndex=0; i<int(scene->mNumMeshes); ++i) {
        const aiMesh *mesh = scene->mMeshes[i];
        if(!mesh->HasPositions() || !mesh->HasNormals()) {
            continue;
        }
        for(int j=0; j<int(mesh->mNumVertices); ++j, ++vertexIndex) {
            auto &vertex = data.vertices[vertexIndex];

            vertex.position = { mesh->mVertices[j].x, mesh->mVertices[j].y, mesh->mVertices[j].z };
            vertex.normal = { mesh->mNormals[j].x, mesh->mNormals[j].y, mesh->mNormals[j].z };

            if(mesh->HasTextureCoords(0)) {
                vertex.texcoord = { mesh->mTextureCoords[0][j].x, mesh->mTextureCoords[0][j].y };
            }
            if(mesh->HasTangentsAndBitangents()) {
                vertex.tangent = { mesh->mTangents[j].x, mesh->mTangents[j].y, mesh->mTangents[j].z };
                vertex.bitangent = { mesh->mBitangents[j].x, mesh->mBitangents[j].y, mesh->mBitangents[j].z };
            }
            else {
                // TODO: Compute tangents & bitangents.
            }
        }
        for(int j=0; j<int(mesh->mNumFaces); ++j, ++triangleIndex) {
            auto &triangle = data.faces[triangleIndex];
            triangle.vertices[0] = meshBaseIndices[i] + mesh->mFaces[j].mIndices[0];
            triangle.vertices[1] = meshBaseIndices[i] + mesh->mFaces[j].mIndices[1];
            triangle.vertices[2] = meshBaseIndices[i] + mesh->mFaces[j].mIndices[2];
        }
    }
    return true;
}

bool DefaultMeshImporter::import(const QUrl &url, QGeometryData &data)
{
    // TODO: Support not only local files.
    if(url.isEmpty() || !url.isLocalFile()) {
        qCCritical(logImport) << "Invalid URL:" << url;
        return false;
    }

    LogStream::initialize();

    qCInfo(logImport) << "Loading mesh:" << url.toString();

    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(url.path().toUtf8().data(), ImportFlags);

    bool result = false;
    if(scene && scene->HasMeshes()) {
        result = importScene(scene, data);
    }
    if(!result) {
        qCCritical(logImport) << "Failed to import mesh from file:" << url;
    }
    return result;
}

} // Raytrace
} // Qt3DRaytrace
