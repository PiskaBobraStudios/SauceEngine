#pragma once

#include "bsp_l.h"
#include "mdl_l.h"
#include <algorithm>
#include <array>
#include <cmath>

inline Vec3 PropModelToEngine(const Vec3& v) {
    return { v.x, v.z, -v.y };
}

inline Vec3 PropRotateEngine(const Vec3& input, const Vec3& angles) {
    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
    Vec3 v = input;

                                                                   
                                                             
    const float cz = std::cos(angles.z * kDegToRad);
    const float sz = std::sin(angles.z * kDegToRad);
    {
        const float x = v.x * cz - v.y * sz;
        const float y = v.x * sz + v.y * cz;
        v.x = x;
        v.y = y;
    }

    const float cx = std::cos(-angles.x * kDegToRad);
    const float sx = std::sin(-angles.x * kDegToRad);
    {
        const float y = v.y * cx - v.z * sx;
        const float z = v.y * sx + v.z * cx;
        v.y = y;
        v.z = z;
    }

    const float cy = std::cos(angles.y * kDegToRad);
    const float sy = std::sin(angles.y * kDegToRad);
    {
        const float x = v.x * cy - v.z * sy;
        const float z = v.x * sy + v.z * cy;
        v.x = x;
        v.z = z;
    }
    return v;
}

inline Vec3 PropWorldToModelPoint(const Vec3& world, const Vec3& origin,
                                  const Vec3& angles, float scale) {
    const float safeScale = std::max(0.0001f, std::abs(scale));
    Vec3 v = (world - origin) * (1.0f / safeScale);

    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

                                                                      
    const float cy = std::cos(-angles.y * kDegToRad);
    const float sy = std::sin(-angles.y * kDegToRad);
    {
        const float x = v.x * cy - v.z * sy;
        const float z = v.x * sy + v.z * cy;
        v.x = x;
        v.z = z;
    }

    const float cx = std::cos(angles.x * kDegToRad);
    const float sx = std::sin(angles.x * kDegToRad);
    {
        const float y = v.y * cx - v.z * sx;
        const float z = v.y * sx + v.z * cx;
        v.y = y;
        v.z = z;
    }

    const float cz = std::cos(-angles.z * kDegToRad);
    const float sz = std::sin(-angles.z * kDegToRad);
    {
        const float x = v.x * cz - v.y * sz;
        const float y = v.x * sz + v.y * cz;
        v.x = x;
        v.y = y;
    }

                                                               
    return { v.x, -v.z, v.y };
}

inline Vec3 PropWorldToModelDirection(const Vec3& worldDir, const Vec3& angles) {
    Vec3 v = worldDir;
    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

    const float cy = std::cos(-angles.y * kDegToRad);
    const float sy = std::sin(-angles.y * kDegToRad);
    {
        const float x = v.x * cy - v.z * sy;
        const float z = v.x * sy + v.z * cy;
        v.x = x;
        v.z = z;
    }

    const float cx = std::cos(angles.x * kDegToRad);
    const float sx = std::sin(angles.x * kDegToRad);
    {
        const float y = v.y * cx - v.z * sx;
        const float z = v.y * sx + v.z * cx;
        v.y = y;
        v.z = z;
    }

    const float cz = std::cos(-angles.z * kDegToRad);
    const float sz = std::sin(-angles.z * kDegToRad);
    {
        const float x = v.x * cz - v.y * sz;
        const float y = v.x * sz + v.y * cz;
        v.x = x;
        v.y = y;
    }

    return v.normalize();
}

inline Vec3 PropModelNormalToWorld(const Vec3& localNormal, const Vec3& angles) {
    return PropRotateEngine(PropModelToEngine(localNormal), angles).normalize();
}

inline void SamplePropLighting(const BSP_L* map, const MDL_L& model,
                               const Vec3& worldOrigin, const Vec3& angles,
                               float scale, Vec3& outColor, float& outBrightness) {
    outColor = {1.0f, 1.0f, 1.0f};
    outBrightness = 1.0f;
    if (!map || !model.loaded) return;

    Vec3 mn = model.hasRenderBounds ? model.renderBoundsMin : model.hullMin;
    Vec3 mx = model.hasRenderBounds ? model.renderBoundsMax : model.hullMax;
    if (mn.x > mx.x) std::swap(mn.x, mx.x);
    if (mn.y > mx.y) std::swap(mn.y, mx.y);
    if (mn.z > mx.z) std::swap(mn.z, mx.z);

    const Vec3 ext = mx - mn;
    const float maxExtent = std::max(ext.x, std::max(ext.y, ext.z));

    std::array<Vec3, 7> samples{};
    std::array<float, 7> weights{};
    int count = 1;
    samples[0] = (mn + mx) * 0.5f;
    weights[0] = 1.0f;

                                                                            
                                                                                  
    if (maxExtent > 48.0f) {
        const Vec3 c = samples[0];
        samples[0] = c;
        weights[0] = 0.34f;
        samples[1] = { c.x, c.y, mn.z }; weights[1] = 0.24f;              
        samples[2] = { c.x, c.y, mx.z }; weights[2] = 0.10f;              
        samples[3] = { mn.x, c.y, c.z }; weights[3] = 0.08f;
        samples[4] = { mx.x, c.y, c.z }; weights[4] = 0.08f;
        samples[5] = { c.x, mn.y, c.z }; weights[5] = 0.08f;
        samples[6] = { c.x, mx.y, c.z }; weights[6] = 0.08f;
        count = 7;
    }

    Vec3 sumColor{0.0f, 0.0f, 0.0f};
    float sumBrightness = 0.0f;
    float sumWeight = 0.0f;
    for (int i = 0; i < count; ++i) {
        const Vec3 modelPoint = samples[i];
        const Vec3 engineOffset = PropRotateEngine(PropModelToEngine(modelPoint), angles) * scale;
        const Vec3 worldPoint = worldOrigin + engineOffset;
        float brightness = 1.0f;
        Vec3 color = map->sampleLightmapLighting(worldPoint, &brightness);
        if (!std::isfinite(brightness) || !std::isfinite(color.x) ||
            !std::isfinite(color.y) || !std::isfinite(color.z)) {
            continue;
        }
        const float w = std::max(0.0f, weights[i]);
        sumColor += color * w;
        sumBrightness += brightness * w;
        sumWeight += w;
    }

    if (sumWeight <= 0.0f) return;
    outColor = sumColor * (1.0f / sumWeight);
    outBrightness = sumBrightness / sumWeight;

    outColor.x = std::max(0.0f, std::min(1.25f, outColor.x));
    outColor.y = std::max(0.0f, std::min(1.25f, outColor.y));
    outColor.z = std::max(0.0f, std::min(1.25f, outColor.z));
    outBrightness = std::max(0.35f, std::min(1.18f, outBrightness));
}
