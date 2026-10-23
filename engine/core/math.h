#pragma once
#include <cmath>
#include <algorithm>

struct Vec2 {
    float x, y;
};

struct Vec3 {
    float x, y, z;
    Vec3() : x(0), y(0), z(0) {}
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

    Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
    Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
    Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    Vec3 cross(const Vec3& o) const { return { y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x }; }
    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    float length() const { return std::sqrt(x * x + y * y + z * z); }
    Vec3 normalize() const { float l = length(); return l ? *this * (1.0f / l) : Vec3{ 0, 0, 0 }; }
};

struct Vec4 {
    float x, y, z, w;
    Vec4() : x(0), y(0), z(0), w(0) {}
    Vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
};

inline Vec3 ClipVelocity(const Vec3& in, const Vec3& normal, float overbounce) {
    float backoff = in.dot(normal) * overbounce;
    Vec3 out = in - (normal * backoff);
    float adjust = out.dot(normal);
    if (adjust < 0.0f) {
        out = out - (normal * adjust);
    }
    return out;
}

inline bool rayTriangleIntersect(const Vec3& orig, const Vec3& dir, const Vec3& v0, const Vec3& v1, const Vec3& v2, float& t, Vec3& norm) {
    Vec3 edge1 = v1 - v0;
    Vec3 edge2 = v2 - v0;
    Vec3 h = dir.cross(edge2);
    float a = edge1.dot(h);
    if (a > -0.0001f && a < 0.0001f) return false;
    float f = 1.0f / a;
    Vec3 s = orig - v0;
    float u = f * s.dot(h);
    if (u < 0.0f || u > 1.0f) return false;
    Vec3 q = s.cross(edge1);
    float v = f * dir.dot(q);
    if (v < 0.0f || u + v > 1.0f) return false;
    float hitT = f * edge2.dot(q);
    if (hitT > 0.0001f) {
        t = hitT;
        norm = edge1.cross(edge2).normalize();
        if (norm.dot(dir) > 0) norm = norm * -1.0f;
        return true;
    }
    return false;
}

                                                                      
inline bool raySphereIntersect(const Vec3& orig, const Vec3& dir, const Vec3& center, float radius, float& t, Vec3& norm) {
    Vec3 oc = orig - center;
    float b = oc.dot(dir);
    float c = oc.dot(oc) - radius * radius;
    float h = b * b - c;
    if (h < 0.0f) return false;
    h = std::sqrt(h);
    float t1 = -b - h;
    if (t1 > 0.0001f) {
        t = t1;
        norm = ((orig + dir * t) - center).normalize();
        return true;
    }
    return false;
}