#pragma once

#include "math.h"
#include <algorithm>
#include <cmath>

inline Vec3 PropModelToEngine(const Vec3& v) {
    return {v.x,v.z,-v.y};
}

inline Vec3 PropRotateEngine(const Vec3& input, const Vec3& angles) {
    constexpr float kDegToRad=3.14159265358979323846f/180.0f;
    Vec3 v=input;
    const float cz=std::cos(angles.z*kDegToRad);
    const float sz=std::sin(angles.z*kDegToRad);
    {
        const float x=v.x*cz-v.y*sz;
        const float y=v.x*sz+v.y*cz;
        v.x=x;
        v.y=y;
    }
    const float cx=std::cos(-angles.x*kDegToRad);
    const float sx=std::sin(-angles.x*kDegToRad);
    {
        const float y=v.y*cx-v.z*sx;
        const float z=v.y*sx+v.z*cx;
        v.y=y;
        v.z=z;
    }
    const float cy=std::cos(angles.y*kDegToRad);
    const float sy=std::sin(angles.y*kDegToRad);
    {
        const float x=v.x*cy-v.z*sy;
        const float z=v.x*sy+v.z*cy;
        v.x=x;
        v.z=z;
    }
    return v;
}

inline Vec3 PropWorldToModelPoint(const Vec3& world, const Vec3& origin, const Vec3& angles, float scale) {
    const float safeScale=std::max(0.0001f,std::abs(scale));
    Vec3 v=(world-origin)*(1.0f/safeScale);
    constexpr float kDegToRad=3.14159265358979323846f/180.0f;
    const float cy=std::cos(-angles.y*kDegToRad);
    const float sy=std::sin(-angles.y*kDegToRad);
    {
        const float x=v.x*cy-v.z*sy;
        const float z=v.x*sy+v.z*cy;
        v.x=x;
        v.z=z;
    }
    const float cx=std::cos(angles.x*kDegToRad);
    const float sx=std::sin(angles.x*kDegToRad);
    {
        const float y=v.y*cx-v.z*sx;
        const float z=v.y*sx+v.z*cx;
        v.y=y;
        v.z=z;
    }
    const float cz=std::cos(-angles.z*kDegToRad);
    const float sz=std::sin(-angles.z*kDegToRad);
    {
        const float x=v.x*cz-v.y*sz;
        const float y=v.x*sz+v.y*cz;
        v.x=x;
        v.y=y;
    }
    return {v.x,-v.z,v.y};
}

inline Vec3 PropWorldToModelDirection(const Vec3& worldDir, const Vec3& angles) {
    Vec3 v=worldDir;
    constexpr float kDegToRad=3.14159265358979323846f/180.0f;
    const float cy=std::cos(-angles.y*kDegToRad);
    const float sy=std::sin(-angles.y*kDegToRad);
    {
        const float x=v.x*cy-v.z*sy;
        const float z=v.x*sy+v.z*cy;
        v.x=x;
        v.z=z;
    }
    const float cx=std::cos(angles.x*kDegToRad);
    const float sx=std::sin(angles.x*kDegToRad);
    {
        const float y=v.y*cx-v.z*sx;
        const float z=v.y*sx+v.z*cx;
        v.y=y;
        v.z=z;
    }
    const float cz=std::cos(-angles.z*kDegToRad);
    const float sz=std::sin(-angles.z*kDegToRad);
    {
        const float x=v.x*cz-v.y*sz;
        const float y=v.x*sz+v.y*cz;
        v.x=x;
        v.y=y;
    }
    return Vec3{v.x,-v.z,v.y}.normalize();
}

inline Vec3 PropModelNormalToWorld(const Vec3& localNormal, const Vec3& angles) {
    return PropRotateEngine(PropModelToEngine(localNormal),angles).normalize();
}
