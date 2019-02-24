/*
 * Copyright (C) 2018-2019 Michał Siejak
 * This file is part of Quartz - a raytracing aspect for Qt3D.
 * See LICENSE file for licensing information.
 */

#ifndef QUARTZ_SHADERS_RESOURCES_H
#define QUARTZ_SHADERS_RESOURCES_H

#include "common.glsl"

layout(set=DS_Render, binding=Binding_Instances, std430) readonly buffer InstanceBuffer {
    EntityInstance instances[];
} instanceBuffer;

layout(set=DS_Render, binding=Binding_Materials, std430) readonly buffer MaterialBuffer {
    Material materials[];
} materialBuffer;

layout(set=DS_Render, binding=Binding_Emitters, std430) readonly buffer EmitterBuffer {
    Emitter emitters[];
} emitterBuffer;

layout(set=DS_AttributeBuffer, binding=0, std430) readonly buffer AttributeBuffer {
    Attributes attributes[];
} attributeBuffer[];

layout(set=DS_IndexBuffer, binding=0, std430) readonly buffer IndexBuffer {
    Face faces[];
} indexBuffer[];

layout(set=DS_Render, binding=Binding_TextureSampler) uniform sampler textureSampler;
layout(set=DS_TextureImage, binding=0) uniform texture2D textures[];

Triangle fetchTriangle(uint geometryIndex, uint faceIndex)
{
    const Face face = indexBuffer[nonuniformEXT(geometryIndex)].faces[faceIndex];

    Triangle result;
    result.v1 = attributeBuffer[nonuniformEXT(geometryIndex)].attributes[face.vertices[0]];
    result.v2 = attributeBuffer[nonuniformEXT(geometryIndex)].attributes[face.vertices[1]];
    result.v3 = attributeBuffer[nonuniformEXT(geometryIndex)].attributes[face.vertices[2]];
    return result;
}

EntityInstance fetchInstance(uint instanceIndex)
{
    return instanceBuffer.instances[instanceIndex];
}

Material fetchMaterial(uint instanceIndex)
{
    uint materialIndex = instanceBuffer.instances[instanceIndex].materialIndex;
    return materialBuffer.materials[materialIndex];
}

Emitter fetchEmitter(uint emitterIndex)
{
    return emitterBuffer.emitters[emitterIndex];
}

vec3 fetchMaterialAlbedo(Material material, vec2 uv)
{
    vec3 albedo = material.albedo.rgb;
    if(material.albedoTexture != ~0u) {
        albedo = texture(sampler2D(textures[nonuniformEXT(material.albedoTexture)], textureSampler), uv).rgb;
    }
    return albedo;
}

float fetchMaterialRoughness(Material material, vec2 uv)
{
    float roughness = material.albedo.a;
    if(material.roughnessTexture != ~0u) {
        roughness = 1.0 - min(1.0, texture(sampler2D(textures[nonuniformEXT(material.roughnessTexture)], textureSampler), uv).r);
    }
    return max(MinRoughness, roughness);
}

float fetchMaterialMetalness(Material material, vec2 uv)
{
    float metalness = material.emission.a;
    if(material.metalnessTexture != ~0u) {
        metalness = 1.0 - min(1.0, texture(sampler2D(textures[nonuniformEXT(material.metalnessTexture)], textureSampler), uv).r);
    }
    return metalness;
}

vec3 fetchSkyRadiance(vec2 uv)
{
    Emitter skyEmitter = emitterBuffer.emitters[0];
    vec3 radiance = skyEmitter.radiance;
    if(skyEmitter.textureIndex != ~0u) {
        radiance = skyEmitter.intensity * texture(sampler2D(textures[nonuniformEXT(skyEmitter.textureIndex)], textureSampler), uv).rgb;
    }
    return radiance;
}

#endif // QUARTZ_SHADERS_RESOURCES_H
