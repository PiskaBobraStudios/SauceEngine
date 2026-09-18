#include "bsp_l.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <GLFW/glfw3.h>

float BSP_L::parseSafeFloat(const std::string& str, float def) {
    if (str.empty()) return def;
    std::istringstream ss(str);
    float val;
    if (ss >> val) return val;
    return def;
}

int BSP_L::parseSafeInt(const std::string& str, int def) {
    if (str.empty()) return def;
    std::istringstream ss(str);
    int val;
    if (ss >> val) return val;
    return def;
}

bool BSP_L::loadMap(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cout << "[BSP] ERROR: Cannot open file " << filename << std::endl;
        return false;
    }
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < 0 || size > static_cast<std::streamoff>(1024ll * 1024ll * 1024ll)) {
        std::cout << "[BSP] ERROR: Invalid BSP size" << std::endl;
        return false;
    }
    file.seekg(0, std::ios::beg);
    std::vector<char> data(static_cast<size_t>(size));
    if (!data.empty() && !file.read(data.data(), size)) {
        std::cout << "[BSP] ERROR: Cannot read BSP file " << filename << std::endl;
        return false;
    }
    return loadMapFromData(data, filename);
}

bool BSP_L::loadMapFromData(const std::vector<char>& data, const std::string& sourceName) {
    vertices.clear(); edges.clear(); surfedges.clear(); faces.clear(); texinfos.clear();
    texdatas.clear(); dispinfos.clear(); dispverts.clear(); lightingData.clear();
    faceLightmapTextures.clear(); faceBloomEligible.clear(); texDataStringData.clear();
    texDataStringTable.clear(); models.clear(); pakfileData.clear(); entityData.clear();
    staticPropData.clear(); staticPropVersion = 0; changelevelTriggers.clear();
    faceRenderable.clear(); faceBounds.clear(); dispTriangles.clear(); mapSkyboxName.clear();

    if (data.size() < sizeof(BSPHeader)) {
        std::cout << "[BSP] ERROR: File too small to contain BSP header: " << sourceName << std::endl;
        return false;
    }

    BSPHeader h{};
    std::memcpy(&h, data.data(), sizeof(h));
    if (h.magic != 0x50534256) {
        std::cout << "[BSP] ERROR: Invalid BSP magic in " << sourceName << std::endl;
        return false;
    }

    const size_t fileSize = data.size();
    bool ok = true;
    ok &= readLumpSafe(data, h.lumps[LUMP_VERTICES], vertices, fileSize, "Vertices");
    ok &= readLumpSafe(data, h.lumps[LUMP_EDGES], edges, fileSize, "Edges");
    ok &= readLumpSafe(data, h.lumps[LUMP_SURFEDGES], surfedges, fileSize, "Surfedges");
    ok &= readLumpSafe(data, h.lumps[LUMP_TEXINFO], texinfos, fileSize, "TexInfo");
    ok &= readLumpSafe(data, h.lumps[LUMP_TEXDATA], texdatas, fileSize, "TexData");
    ok &= readLumpSafe(data, h.lumps[LUMP_LIGHTING], lightingData, fileSize, "Lighting");
    ok &= readLumpSafe(data, h.lumps[LUMP_MODELS], models, fileSize, "Models");
    ok &= readLumpSafe(data, h.lumps[LUMP_DISPINFO], dispinfos, fileSize, "DispInfo");
    ok &= readLumpSafe(data, h.lumps[LUMP_DISPVERTS], dispverts, fileSize, "DispVerts");
    ok &= readLumpSafe(data, h.lumps[LUMP_TEXDATA_STRING_DATA], texDataStringData, fileSize, "TexDataStringData");
    ok &= readLumpSafe(data, h.lumps[LUMP_TEXDATA_STRING_TABLE], texDataStringTable, fileSize, "TexDataStringTable");
    if (!ok) {
        std::cout << "[BSP] ERROR: Critical lump read failure, cannot continue." << std::endl;
        return false;
    }

    readLumpSafe(data, h.lumps[LUMP_FACES], faces, fileSize, "Faces");
    if (faces.empty()) readLumpSafe(data, h.lumps[LUMP_FACES_HDR], faces, fileSize, "Faces HDR");

                                                                               
                                                       
    {
        const auto& pak = h.lumps[LUMP_PAKFILE];
        const size_t off = static_cast<size_t>(pak.offset);
        const size_t len = static_cast<size_t>(pak.length);
        if (len > 0 && off > 0 && off <= fileSize && len <= fileSize - off) {
            pakfileData.assign(data.data() + off, data.data() + off + len);
        }
    }

               
    {
        const auto& ent = h.lumps[LUMP_ENTITIES];
        const size_t off = static_cast<size_t>(ent.offset);
        const size_t len = static_cast<size_t>(ent.length);
        if (len > 0 && off > 0 && off <= fileSize && len <= fileSize - off && len < 100ull * 1024ull * 1024ull) {
            entityData.assign(data.data() + off, data.data() + off + len);
        }
    }

                               
    {
        const int LUMP_GAME_LUMP = 35;
        const auto& gl = h.lumps[LUMP_GAME_LUMP];
        const size_t glOff = static_cast<size_t>(gl.offset);
        const size_t glLen = static_cast<size_t>(gl.length);
        if (glLen > 4 && glOff > 0 && glOff <= fileSize && glLen <= fileSize - glOff) {
            const size_t base = glOff;
            const char* ptr = data.data() + base;
            auto readI32 = [&](size_t rel, int32_t& out) -> bool {
                if (rel + 4 > glLen) return false;
                std::memcpy(&out, ptr + rel, 4);
                return true;
            };
            auto readU16 = [&](size_t rel, uint16_t& out) -> bool {
                if (rel + 2 > glLen) return false;
                std::memcpy(&out, ptr + rel, 2);
                return true;
            };
            int32_t count = 0;
            if (readI32(0, count) && count > 0 && count < 64 && 4ull + static_cast<size_t>(count) * 16ull <= glLen) {
                size_t cursor = 4;
                for (int i = 0; i < count; ++i) {
                    int32_t glId = 0, glFileOfs = 0, glFileLen = 0;
                    uint16_t glFlags = 0, glVersion = 0;
                    if (!readI32(cursor, glId) || !readU16(cursor + 4, glFlags) ||
                        !readU16(cursor + 6, glVersion) || !readI32(cursor + 8, glFileOfs) ||
                        !readI32(cursor + 12, glFileLen)) break;
                    cursor += 16;
                    if (glId == 0x70727073 && glFileLen > 0 && glFileOfs > 0) {
                        const size_t sOff = static_cast<size_t>(glFileOfs);
                        const size_t sLen = static_cast<size_t>(glFileLen);
                        if (sOff <= fileSize && sLen <= fileSize - sOff) {
                            staticPropVersion = glVersion;
                            staticPropData.assign(data.data() + sOff, data.data() + sOff + sLen);
                        }
                        break;
                    }
                }
            }
        }
    }

                                                      
    for (auto& v : vertices) { float ty = v.position.y; v.position.y = v.position.z; v.position.z = -ty; }
    for (auto& d : dispinfos) { float ty = d.startPosition.y; d.startPosition.y = d.startPosition.z; d.startPosition.z = -ty; }
    for (auto& dv : dispverts) { float ty = dv.vec.y; dv.vec.y = dv.vec.z; dv.vec.z = -ty; }

    faceRenderable.assign(faces.size(), true);
    validateFaces();
    parseEntities();
    precalculateBounds();
    return true;
}

void BSP_L::validateFaces() {
    for (size_t fi = 0; fi < faces.size(); ++fi) {
        const auto& f = faces[fi];
        if (f.texinfo < 0 || f.texinfo >= static_cast<int>(texinfos.size()) ||
            f.numedges < 3 || f.numedges > 512 ||
            f.firstedge < 0 || f.firstedge + f.numedges > static_cast<int>(surfedges.size())) {
            faceRenderable[fi] = false;
            continue;
        }

        bool valid = true;
        for (int j = 0; j < f.numedges && valid; ++j) {
            int seIdx = f.firstedge + j;
            int se = surfedges[seIdx];
            int eIdx = std::abs(se);

            if (eIdx < 0 || eIdx >= static_cast<int>(edges.size())) { valid = false; break; }
            int vIdx = edges[eIdx].v[se < 0 ? 1 : 0];
            if (vIdx < 0 || vIdx >= static_cast<int>(vertices.size())) { valid = false; break; }
        }
        if (!valid) faceRenderable[fi] = false;
    }
}

void BSP_L::precalculateBounds() {
    faceBounds.resize(faces.size());
    for (size_t fIdx = 0; fIdx < faces.size(); ++fIdx) {
        if (!faceRenderable[fIdx]) {
            faceBounds[fIdx] = { 0, 0, 0, 0, 0, 0 };
            continue;
        }

        const auto& f = faces[fIdx];
        float minX = 1e9f, minY = 1e9f, minZ = 1e9f;
        float maxX = -1e9f, maxY = -1e9f, maxZ = -1e9f;

        for (int i = 0; i < f.numedges; ++i) {
            int seIdx = f.firstedge + i;
            int se = surfedges[seIdx];
            int eIdx = std::abs(se);
            int vIdx = edges[eIdx].v[se < 0 ? 1 : 0];
            const Vec3& v = vertices[vIdx].position;

            if (v.x < minX) minX = v.x;
            if (v.y < minY) minY = v.y;
            if (v.z < minZ) minZ = v.z;
            if (v.x > maxX) maxX = v.x;
            if (v.y > maxY) maxY = v.y;
            if (v.z > maxZ) maxZ = v.z;
        }
        faceBounds[fIdx] = { minX, minY, minZ, maxX, maxY, maxZ };
    }
}

std::string BSP_L::extractEntityValue(const std::string& block, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = block.find(search);
    if (pos == std::string::npos) return "";

    size_t valStart = block.find('\"', pos + search.length());
    if (valStart == std::string::npos) return "";
    valStart++;

    size_t valEnd = block.find('\"', valStart);
    if (valEnd == std::string::npos) return "";

    return block.substr(valStart, valEnd - valStart);
}

void BSP_L::parseEntities() {
    changelevelTriggers.clear();
    size_t pos = 0;
    while ((pos = entityData.find('{', pos)) != std::string::npos) {
        size_t endPos = entityData.find('}', pos);
        if (endPos == std::string::npos) break;

        std::string block = entityData.substr(pos, endPos - pos);
        std::string classname = extractEntityValue(block, "classname");

        if (classname == "worldspawn") {
            mapSkyboxName = extractEntityValue(block, "skyname");
        }
        else if (classname == "trigger_changelevel") {
            std::string mapName = extractEntityValue(block, "map");
            std::string modelStr = extractEntityValue(block, "model");
            std::string landmarkStr = extractEntityValue(block, "landmark");

            if (!mapName.empty() && !modelStr.empty() && modelStr[0] == '*') {
                int modelIdx = parseSafeInt(modelStr.substr(1), -1);
                if (modelIdx >= 0 && modelIdx < static_cast<int>(models.size())) {
                    MapTrigger trig;
                    trig.targetMap = mapName;
                    trig.landmark = landmarkStr;

                    trig.bounds.minX = models[modelIdx].mins.x;
                    trig.bounds.maxX = models[modelIdx].maxs.x;
                    trig.bounds.minY = models[modelIdx].mins.z;
                    trig.bounds.maxY = models[modelIdx].maxs.z;
                    trig.bounds.minZ = -models[modelIdx].maxs.y;
                    trig.bounds.maxZ = -models[modelIdx].mins.y;

                    changelevelTriggers.push_back(trig);
                }
            }
        }
        pos = endPos + 1;
    }
}

TriggerResult BSP_L::checkTriggers(const Vec3& pos, float radius, float height) {
    float pMinX = pos.x - radius, pMaxX = pos.x + radius;
    float pMinY = pos.y, pMaxY = pos.y + height;
    float pMinZ = pos.z - radius, pMaxZ = pos.z + radius;

    for (const auto& trig : changelevelTriggers) {
        if (pMinX <= trig.bounds.maxX && pMaxX >= trig.bounds.minX &&
            pMinY <= trig.bounds.maxY && pMaxY >= trig.bounds.minY &&
            pMinZ <= trig.bounds.maxZ && pMaxZ >= trig.bounds.minZ)
        {
            return { trig.targetMap, trig.landmark };
        }
    }
    return { "", "" };
}

bool BSP_L::findLandmark(const std::string& name, Vec3& outPos) {
    if (name.empty()) return false;

    size_t pos = 0;
    while ((pos = entityData.find('{', pos)) != std::string::npos) {
        size_t endPos = entityData.find('}', pos);
        if (endPos == std::string::npos) break;

        std::string block = entityData.substr(pos, endPos - pos);
        std::string classname = extractEntityValue(block, "classname");

        if (classname == "info_landmark" && extractEntityValue(block, "targetname") == name) {
            std::string originStr = extractEntityValue(block, "origin");
            if (!originStr.empty()) {
                std::istringstream ss(originStr);
                float x, y, z;
                if (ss >> x >> y >> z) {
                    outPos = { x, z, -y };
                    return true;
                }
            }
        }
        pos = endPos + 1;
    }
    return false;
}

void BSP_L::setupTextures(Texture_L& texL) {
    textureSystem = &texL;
    texL.initPakFile(pakfileData);

    for (size_t f = 0; f < faces.size(); ++f) {
        if (!faceRenderable[f]) continue;

        const auto& face = faces[f];
        int tData = texinfos[face.texinfo].texdata;
        if (tData < 0 || tData >= static_cast<int>(texdatas.size())) continue;
        if (glTextures.count(tData)) continue;

        int stID = texdatas[tData].nameStringTableID;
        if (stID < 0 || stID >= static_cast<int>(texDataStringTable.size())) continue;

        int stringOffset = texDataStringTable[stID];
        if (stringOffset < 0 || stringOffset >= static_cast<int>(texDataStringData.size())) continue;

        std::string texName;
        for (int i = stringOffset; i < static_cast<int>(texDataStringData.size()); ++i) {
            if (texDataStringData[i] == '\0') break;
            texName += texDataStringData[i];
            if (texName.length() > 256) break;
        }

        std::string low = texName;
        std::transform(low.begin(), low.end(), low.begin(), ::tolower);

        if (low.find("tools/") != std::string::npos) {
            if (low.find("trigger") != std::string::npos || low.find("nodraw") != std::string::npos ||
                low.find("clip") != std::string::npos || low.find("skybox") != std::string::npos ||
                low.find("skip") != std::string::npos || low.find("hint") != std::string::npos ||
                low.find("invisible") != std::string::npos || low.find("fog") != std::string::npos ||
                low.find("areaportal") != std::string::npos || low.find("blocklight") != std::string::npos) {
                faceRenderable[f] = false;
                continue;
            }
        }

        glTextures[tData] = texL.getMaterial(texName);
    }

    buildDisplacements();
    buildLightmaps();
}

void BSP_L::buildDisplacements() {
    dispTriangles.clear();
    if (dispinfos.empty()) return;

    for (size_t di = 0; di < dispinfos.size(); ++di) {
        const auto& d = dispinfos[di];
        if (d.mapFace >= faces.size() || d.power < 2 || d.power > 4) continue;

        const BSPFace& f = faces[d.mapFace];
        faceRenderable[d.mapFace] = false;

        if (f.texinfo < 0 || f.texinfo >= static_cast<int>(texinfos.size())) continue;
        if (f.firstedge < 0 || f.numedges < 4 || f.firstedge + f.numedges > static_cast<int>(surfedges.size())) continue;

        std::vector<Vec3> fv;
        std::vector<Vec2> fuv;
        bool faceValid = true;

        for (int i = 0; i < f.numedges; ++i) {
            int seIdx = f.firstedge + i;
            int se = surfedges[seIdx];
            int eIdx = std::abs(se);

            if (eIdx >= static_cast<int>(edges.size())) { faceValid = false; break; }
            int vIdx = edges[eIdx].v[se < 0 ? 1 : 0];
            if (vIdx >= static_cast<int>(vertices.size())) { faceValid = false; break; }

            fv.push_back(vertices[vIdx].position);

            const auto& ti = texinfos[f.texinfo];
            float ox = vertices[vIdx].position.x, oy = -vertices[vIdx].position.z, oz = vertices[vIdx].position.y;
            float u = ox * ti.textureVecs[0][0] + oy * ti.textureVecs[0][1] + oz * ti.textureVecs[0][2] + ti.textureVecs[0][3];
            float vt = ox * ti.textureVecs[1][0] + oy * ti.textureVecs[1][1] + oz * ti.textureVecs[1][2] + ti.textureVecs[1][3];

            if (ti.texdata >= 0 && ti.texdata < static_cast<int>(texdatas.size())) {
                float tw = static_cast<float>(texdatas[ti.texdata].width);
                float th = static_cast<float>(texdatas[ti.texdata].height);
                if (tw > 0 && th > 0) { u /= tw; vt /= th; }
            }
            fuv.push_back({ u, vt });
        }

        if (!faceValid || fv.size() != 4) continue;

        int startIdx = 0;
        float minDist = 1e9f;
        for (int i = 0; i < 4; ++i) {
            float dist = (fv[i] - d.startPosition).length();
            if (dist < minDist) { minDist = dist; startIdx = i; }
        }

        Vec3 c[4]; Vec2 cuv[4];
        for (int i = 0; i < 4; ++i) {
            c[i] = fv[(startIdx + i) % 4];
            cuv[i] = fuv[(startIdx + i) % 4];
        }

        int dim = (1 << d.power) + 1;
        int totalVerts = dim * dim;

        if (d.dispVertStart < 0 || d.dispVertStart + totalVerts > static_cast<int>(dispverts.size())) continue;

        std::vector<Vec3> grid(totalVerts);
        std::vector<Vec2> griduv(totalVerts);

        for (int y = 0; y < dim; ++y) {
            float v = static_cast<float>(y) / (dim - 1);
            Vec3 e0 = c[0] + (c[1] - c[0]) * v;
            Vec3 e1 = c[3] + (c[2] - c[3]) * v;
            Vec2 uv0 = { cuv[0].x + (cuv[1].x - cuv[0].x) * v, cuv[0].y + (cuv[1].y - cuv[0].y) * v };
            Vec2 uv1 = { cuv[3].x + (cuv[2].x - cuv[3].x) * v, cuv[3].y + (cuv[2].y - cuv[3].y) * v };

            for (int x = 0; x < dim; ++x) {
                float u = static_cast<float>(x) / (dim - 1);
                int ptIdx = d.dispVertStart + x + y * dim;

                Vec3 pt = e0 + (e1 - e0) * u;
                Vec2 ptuv = { uv0.x + (uv1.x - uv0.x) * u, uv0.y + (uv1.y - uv0.y) * u };

                const auto& dv = dispverts[ptIdx];
                grid[x + y * dim] = pt + dv.vec * dv.dist;
                griduv[x + y * dim] = ptuv;
            }
        }

        int tex = texinfos[f.texinfo].texdata;
        for (int y = 0; y < dim - 1; ++y) {
            for (int x = 0; x < dim - 1; ++x) {
                int i0 = x + y * dim;
                int i1 = i0 + 1;
                int i2 = x + (y + 1) * dim;
                int i3 = i2 + 1;

                dispTriangles.push_back({ { grid[i0], grid[i2], grid[i1] }, { griduv[i0], griduv[i2], griduv[i1] }, tex });
                dispTriangles.push_back({ { grid[i1], grid[i2], grid[i3] }, { griduv[i1], griduv[i2], griduv[i3] }, tex });
            }
        }
    }
}

void BSP_L::buildLightmaps() {
    faceLightmapTextures.assign(faces.size(), 0);
    faceBloomEligible.assign(faces.size(), 0);
    if (lightingData.empty()) return;

    constexpr int kMaxLightStyles = 4;
    constexpr int kBumpLightmapsPerStyle = 4;                            

    for (size_t fIdx = 0; fIdx < faces.size(); ++fIdx) {
        if (!faceRenderable[fIdx]) continue;
        const auto& f = faces[fIdx];

        if (f.lightofs < 0 || (f.lightofs % static_cast<int>(sizeof(ColorRGBExp32))) != 0) continue;
        if (f.texinfo < 0 || static_cast<size_t>(f.texinfo) >= texinfos.size()) continue;

        const auto& ti = texinfos[f.texinfo];
        int width = f.LightmapTextureSizeInLuxels[0] + 1;
        int height = f.LightmapTextureSizeInLuxels[1] + 1;

                                                                                      
                                                       
        if (width <= 0 || height <= 0 || width > 256 || height > 256) continue;

        const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
        const size_t dataIdx = static_cast<size_t>(f.lightofs) / sizeof(ColorRGBExp32);

        int styleCount = 0;
        while (styleCount < kMaxLightStyles &&
               static_cast<unsigned char>(f.styles[styleCount]) != 255) {
            ++styleCount;
        }
        if (styleCount == 0) continue;

        const bool hasBumpLight = (ti.flags & 0x0800) != 0;                  
        const size_t styleStride = count * (hasBumpLight ? kBumpLightmapsPerStyle : 1u);
        if (styleStride == 0 || styleCount > std::numeric_limits<size_t>::max() / styleStride) continue;

        const size_t required = styleStride * static_cast<size_t>(styleCount);
        if (dataIdx > lightingData.size() || required > lightingData.size() - dataIdx) {
            std::cout << "[Lightmap] Invalid lightofs/style data on face " << fIdx << std::endl;
            continue;
        }

        std::vector<unsigned char> pixels;
        try {
            pixels.resize(count * 3);
        }
        catch (...) {
            continue;
        }

                                                                                      
                                                                                       
                                                                                           
        for (size_t i = 0; i < count; ++i) {
            float linearR = 0.0f;
            float linearG = 0.0f;
            float linearB = 0.0f;

            for (int style = 0; style < styleCount; ++style) {
                const auto& luxel = lightingData[dataIdx + static_cast<size_t>(style) * styleStride + i];
                const float multiplier = std::ldexp(1.0f, static_cast<int>(luxel.exponent));

                linearR += (static_cast<float>(luxel.r) / 255.0f) * multiplier;
                linearG += (static_cast<float>(luxel.g) / 255.0f) * multiplier;
                linearB += (static_cast<float>(luxel.b) / 255.0f) * multiplier;
            }

                                                                                  
                                                                                
                                                                                       
            const float exposure = 7.8f;
            auto toneLinear = [&](float v) {
                v = std::max(0.035f, v);
                v *= exposure;
                v = v / (1.0f + v);
                v = std::pow(v, 0.88f);
                return std::min(255, std::max(0, static_cast<int>(std::lround(v * 255.0f))));
            };

            const int r = toneLinear(linearR);
            const int g = toneLinear(linearG);
            const int b = toneLinear(linearB);

            if (std::max(r, std::max(g, b)) >= 225) faceBloomEligible[fIdx] = 1;

            pixels[i * 3 + 0] = static_cast<unsigned char>(r);
            pixels[i * 3 + 1] = static_cast<unsigned char>(g);
            pixels[i * 3 + 2] = static_cast<unsigned char>(b);
        }

        unsigned int tex = 0;
        glGenTextures(1, &tex);
        if (tex == 0) continue;
        glBindTexture(GL_TEXTURE_2D, tex);

                                                                                    
                                                                       
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB,
                     GL_UNSIGNED_BYTE, pixels.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

                                                                                     
                                                                                       
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);                    
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);

        faceLightmapTextures[fIdx] = tex;
    }
}



void BSP_L::renderLightmaps() {
    if (lightingData.empty()) return;

    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_DST_COLOR, GL_ZERO);
    glDisable(GL_LIGHTING);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    for (size_t fIdx = 0; fIdx < faces.size(); ++fIdx) {
        if (!faceRenderable[fIdx] || faceLightmapTextures[fIdx] == 0) continue;

        const auto& f = faces[fIdx];
        glBindTexture(GL_TEXTURE_2D, faceLightmapTextures[fIdx]);
        const auto& ti = texinfos[f.texinfo];

        glBegin(GL_POLYGON);
        for (int i = 0; i < f.numedges; ++i) {
            int seIdx = f.firstedge + i;
            int se = surfedges[seIdx];
            int vIdx = edges[std::abs(se)].v[se < 0 ? 1 : 0];
            const auto& v = vertices[vIdx];

            float ox = v.position.x, oy = -v.position.z, oz = v.position.y;
            float u = ox * ti.lightmapVecs[0][0] + oy * ti.lightmapVecs[0][1] + oz * ti.lightmapVecs[0][2] + ti.lightmapVecs[0][3];
            float vt = ox * ti.lightmapVecs[1][0] + oy * ti.lightmapVecs[1][1] + oz * ti.lightmapVecs[1][2] + ti.lightmapVecs[1][3];

            u = (u - f.LightmapTextureMinsInLuxels[0]) + 0.5f;
            vt = (vt - f.LightmapTextureMinsInLuxels[1]) + 0.5f;

            u /= (f.LightmapTextureSizeInLuxels[0] + 1);
            vt /= (f.LightmapTextureSizeInLuxels[1] + 1);

            glTexCoord2f(u, vt);
            glVertex3f(v.position.x, v.position.y, v.position.z);
        }
        glEnd();
    }

                                                                          
                                                                            
    glColor4f(1,1,1,1);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDepthFunc(GL_LESS);
}
bool BSP_L::textureHasAlpha(unsigned int texture) const {
    return textureSystem && textureSystem->textureHasAlpha(texture);
}

void BSP_L::renderWorld() {
    glColor4f(1.15f, 1.15f, 1.15f, 1.0f);
    for (size_t fIdx = 0; fIdx < faces.size(); ++fIdx) {
        if (!faceRenderable[fIdx]) continue;
        const auto& f = faces[fIdx];

        const auto& ti = texinfos[f.texinfo];
        if (ti.texdata >= 0) {
            auto it = glTextures.find(ti.texdata);
            if (it != glTextures.end()) glBindTexture(GL_TEXTURE_2D, it->second);
        }

        unsigned int boundTex = 0;
        if (ti.texdata >= 0) {
            auto bt = glTextures.find(ti.texdata);
            if (bt != glTextures.end()) boundTex = bt->second;
        }
        const bool alphaFace = textureHasAlpha(boundTex);
        if (alphaFace) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_ALPHA_TEST);
            glAlphaFunc(GL_GREATER, 0.01f);
        } else {
            glDisable(GL_BLEND);
            glDisable(GL_ALPHA_TEST);
        }

        glBegin(GL_POLYGON);

        if (f.numedges >= 3) {
            int se0 = surfedges[f.firstedge];
            int se1 = surfedges[f.firstedge + 1];
            int se2 = surfedges[f.firstedge + 2];

            Vec3 v0 = vertices[edges[std::abs(se0)].v[se0 < 0 ? 1 : 0]].position;
            Vec3 v1 = vertices[edges[std::abs(se1)].v[se1 < 0 ? 1 : 0]].position;
            Vec3 v2 = vertices[edges[std::abs(se2)].v[se2 < 0 ? 1 : 0]].position;

            Vec3 normal = (v1 - v0).cross(v2 - v0).normalize();
            glNormal3f(normal.x, normal.y, normal.z);
        }

        for (int i = 0; i < f.numedges; ++i) {
            int seIdx = f.firstedge + i;
            int se = surfedges[seIdx];
            int eIdx = std::abs(se);
            int vIdx = edges[eIdx].v[se < 0 ? 1 : 0];
            const auto& v = vertices[vIdx];

            float ox = v.position.x, oy = -v.position.z, oz = v.position.y;
            float u = ox * ti.textureVecs[0][0] + oy * ti.textureVecs[0][1] + oz * ti.textureVecs[0][2] + ti.textureVecs[0][3];
            float vt = ox * ti.textureVecs[1][0] + oy * ti.textureVecs[1][1] + oz * ti.textureVecs[1][2] + ti.textureVecs[1][3];

            if (ti.texdata >= 0 && ti.texdata < static_cast<int>(texdatas.size())) {
                float tw = static_cast<float>(texdatas[ti.texdata].width);
                float th = static_cast<float>(texdatas[ti.texdata].height);
                if (tw > 0 && th > 0) glTexCoord2f(u / tw, vt / th);
            }
            glVertex3f(v.position.x, v.position.y, v.position.z);
        }
        glEnd();
    }

    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);

    if (dispTriangles.empty()) return;

    glBegin(GL_TRIANGLES);
    int curTex = -1;
    for (const auto& tri : dispTriangles) {
        if (tri.texID != curTex) {
            glEnd();
            curTex = tri.texID;
            if (curTex >= 0) {
                auto it = glTextures.find(curTex);
                if (it != glTextures.end()) glBindTexture(GL_TEXTURE_2D, it->second);
            }
            glBegin(GL_TRIANGLES);
        }

        Vec3 normal = (tri.v[1] - tri.v[0]).cross(tri.v[2] - tri.v[0]).normalize();
        glNormal3f(normal.x, normal.y, normal.z);

        for (int i = 0; i < 3; ++i) {
            glTexCoord2f(tri.uv[i].x, tri.uv[i].y);
            glVertex3f(tri.v[i].x, tri.v[i].y, tri.v[i].z);
        }
    }
    glEnd();
}

Vec3 BSP_L::getPlayerSpawn() {
    size_t pos = 0;
    while ((pos = entityData.find('{', pos)) != std::string::npos) {
        size_t endPos = entityData.find('}', pos);
        if (endPos == std::string::npos) break;

        std::string block = entityData.substr(pos, endPos - pos);
        std::string classname = extractEntityValue(block, "classname");

        if (classname.find("info_player") != std::string::npos) {
            std::string originStr = extractEntityValue(block, "origin");
            if (!originStr.empty()) {
                std::istringstream ss(originStr);
                float x, y, z;
                if (ss >> x >> y >> z) {
                    return { x, z, -y };
                }
            }
        }
        pos = endPos + 1;
    }
    return { 0, 64, 0 };
}

bool BSP_L::traceRay(const Vec3& orig, const Vec3& dir, float& outDist, Vec3& outNormal) {
    outDist = 999999.0f;
    bool hit = false;

    float invDirX = (std::abs(dir.x) > 1e-8f) ? (1.0f / dir.x) : 0.0f;
    float invDirY = (std::abs(dir.y) > 1e-8f) ? (1.0f / dir.y) : 0.0f;
    float invDirZ = (std::abs(dir.z) > 1e-8f) ? (1.0f / dir.z) : 0.0f;
    bool dirXZero = (std::abs(dir.x) <= 1e-8f);
    bool dirYZero = (std::abs(dir.y) <= 1e-8f);
    bool dirZZero = (std::abs(dir.z) <= 1e-8f);

    const float pad = 1.0f;

    for (size_t fIdx = 0; fIdx < faces.size(); ++fIdx) {
        if (!faceRenderable[fIdx]) continue;

        const auto& fb = faceBounds[fIdx];
        float tMin = -1e9f, tMax = 1e9f;

        if (dirXZero) { if (orig.x < fb.minX - pad || orig.x > fb.maxX + pad) continue; }
        else {
            float t1 = (fb.minX - pad - orig.x) * invDirX;
            float t2 = (fb.maxX + pad - orig.x) * invDirX;
            if (t1 > t2) std::swap(t1, t2);
            if (t1 > tMin) tMin = t1;
            if (t2 < tMax) tMax = t2;
            if (tMin > tMax) continue;
        }

        if (dirYZero) { if (orig.y < fb.minY - pad || orig.y > fb.maxY + pad) continue; }
        else {
            float t1 = (fb.minY - pad - orig.y) * invDirY;
            float t2 = (fb.maxY + pad - orig.y) * invDirY;
            if (t1 > t2) std::swap(t1, t2);
            if (t1 > tMin) tMin = t1;
            if (t2 < tMax) tMax = t2;
            if (tMin > tMax) continue;
        }

        if (dirZZero) { if (orig.z < fb.minZ - pad || orig.z > fb.maxZ + pad) continue; }
        else {
            float t1 = (fb.minZ - pad - orig.z) * invDirZ;
            float t2 = (fb.maxZ + pad - orig.z) * invDirZ;
            if (t1 > t2) std::swap(t1, t2);
            if (t1 > tMin) tMin = t1;
            if (t2 < tMax) tMax = t2;
            if (tMin > tMax) continue;
        }

        if (tMax < 0.0f || tMin > outDist) continue;

        const auto& f = faces[fIdx];
        int se0 = surfedges[f.firstedge];
        int e0 = std::abs(se0);
        int vi0 = edges[e0].v[se0 < 0 ? 1 : 0];
        Vec3 v0 = vertices[vi0].position;

        for (int i = 1; i < f.numedges - 1; ++i) {
            int se1 = surfedges[f.firstedge + i];
            int se2 = surfedges[f.firstedge + i + 1];
            int e1 = std::abs(se1);
            int e2 = std::abs(se2);
            int vi1 = edges[e1].v[se1 < 0 ? 1 : 0];
            int vi2 = edges[e2].v[se2 < 0 ? 1 : 0];

            Vec3 v1 = vertices[vi1].position;
            Vec3 v2 = vertices[vi2].position;

            float t; Vec3 n;
            if (rayTriangleIntersect(orig, dir, v0, v1, v2, t, n)) {
                if (t < outDist) {
                    outDist = t;
                    outNormal = n;
                    hit = true;
                }
            }
        }
    }

    for (const auto& tri : dispTriangles) {
        float t; Vec3 n;
        if (rayTriangleIntersect(orig, dir, tri.v[0], tri.v[1], tri.v[2], t, n)) {
            if (t < outDist) {
                outDist = t;
                outNormal = n;
                hit = true;
            }
        }
    }

    return hit;
}

std::string BSP_L::getHitTextureName(const Vec3& orig, const Vec3& dir) {
    float outDist = 999999.0f;
    int bestTexData = -1;

    float invDirX = (std::abs(dir.x) > 1e-8f) ? (1.0f / dir.x) : 0.0f;
    float invDirY = (std::abs(dir.y) > 1e-8f) ? (1.0f / dir.y) : 0.0f;
    float invDirZ = (std::abs(dir.z) > 1e-8f) ? (1.0f / dir.z) : 0.0f;
    bool dirXZero = (std::abs(dir.x) <= 1e-8f);
    bool dirYZero = (std::abs(dir.y) <= 1e-8f);
    bool dirZZero = (std::abs(dir.z) <= 1e-8f);

    const float pad = 1.0f;

    for (size_t fIdx = 0; fIdx < faces.size(); ++fIdx) {
        if (!faceRenderable[fIdx]) continue;

        const auto& fb = faceBounds[fIdx];
        float tMin = -1e9f, tMax = 1e9f;

        if (dirXZero) { if (orig.x < fb.minX - pad || orig.x > fb.maxX + pad) continue; }
        else {
            float t1 = (fb.minX - pad - orig.x) * invDirX;
            float t2 = (fb.maxX + pad - orig.x) * invDirX;
            if (t1 > t2) std::swap(t1, t2);
            if (t1 > tMin) tMin = t1;
            if (t2 < tMax) tMax = t2;
            if (tMin > tMax) continue;
        }

        if (dirYZero) { if (orig.y < fb.minY - pad || orig.y > fb.maxY + pad) continue; }
        else {
            float t1 = (fb.minY - pad - orig.y) * invDirY;
            float t2 = (fb.maxY + pad - orig.y) * invDirY;
            if (t1 > t2) std::swap(t1, t2);
            if (t1 > tMin) tMin = t1;
            if (t2 < tMax) tMax = t2;
            if (tMin > tMax) continue;
        }

        if (dirZZero) { if (orig.z < fb.minZ - pad || orig.z > fb.maxZ + pad) continue; }
        else {
            float t1 = (fb.minZ - pad - orig.z) * invDirZ;
            float t2 = (fb.maxZ + pad - orig.z) * invDirZ;
            if (t1 > t2) std::swap(t1, t2);
            if (t1 > tMin) tMin = t1;
            if (t2 < tMax) tMax = t2;
            if (tMin > tMax) continue;
        }

        if (tMax < 0.0f || tMin > outDist) continue;

        const auto& f = faces[fIdx];
        int se0 = surfedges[f.firstedge];
        int e0 = std::abs(se0);
        int vi0 = edges[e0].v[se0 < 0 ? 1 : 0];
        Vec3 v0 = vertices[vi0].position;

        for (int i = 1; i < f.numedges - 1; ++i) {
            int se1 = surfedges[f.firstedge + i];
            int se2 = surfedges[f.firstedge + i + 1];
            int e1 = std::abs(se1);
            int e2 = std::abs(se2);
            int vi1 = edges[e1].v[se1 < 0 ? 1 : 0];
            int vi2 = edges[e2].v[se2 < 0 ? 1 : 0];

            Vec3 v1 = vertices[vi1].position;
            Vec3 v2 = vertices[vi2].position;

            float t; Vec3 n;
            if (rayTriangleIntersect(orig, dir, v0, v1, v2, t, n)) {
                if (t < outDist) {
                    outDist = t;
                    bestTexData = texinfos[f.texinfo].texdata;
                }
            }
        }
    }

    for (const auto& tri : dispTriangles) {
        float t; Vec3 n;
        if (rayTriangleIntersect(orig, dir, tri.v[0], tri.v[1], tri.v[2], t, n)) {
            if (t < outDist) {
                outDist = t;
                bestTexData = tri.texID;
            }
        }
    }

    if (bestTexData >= 0 && bestTexData < static_cast<int>(texdatas.size())) {
        int stID = texdatas[bestTexData].nameStringTableID;
        if (stID >= 0 && stID < static_cast<int>(texDataStringTable.size())) {
            int stringOffset = texDataStringTable[stID];
            if (stringOffset >= 0 && stringOffset < static_cast<int>(texDataStringData.size())) {
                std::string texName;
                for (int i = stringOffset; i < static_cast<int>(texDataStringData.size()); ++i) {
                    if (texDataStringData[i] == '\0') break;
                    texName += texDataStringData[i];
                    if (texName.length() > 256) break;
                }
                return texName;
            }
        }
    }
    return "";
}


static bool bspFindLightmapSample(const BSP_L* map,const Vec3& worldPos,size_t& best,float& lx,float& ly){
    if(!map || map->faces.empty() || map->lightingData.empty() || map->faceBounds.size()!=map->faces.size()) return false;
    Vec3 srcPos{worldPos.x,-worldPos.z,worldPos.y};
    best=map->faces.size();
    float bestScore=1.0e30f;
    for(size_t i=0;i<map->faces.size();++i){
        if(i>=map->faceRenderable.size() || !map->faceRenderable[i]) continue;
        const BSPFace& f=map->faces[i];
        if(f.lightofs<0 || f.texinfo<0 || f.texinfo>=(int)map->texinfos.size() || f.numedges<3) continue;

        const BSPFaceBounds& b=map->faceBounds[i];
        float cx=std::max(b.minX,std::min(srcPos.x,b.maxX));
        float cy=std::max(b.minY,std::min(srcPos.y,b.maxY));
        float cz=std::max(b.minZ,std::min(srcPos.z,b.maxZ));
        const float dx=srcPos.x-cx,dy=srcPos.y-cy,dz=srcPos.z-cz;
        const float boundsD2=dx*dx+dy*dy+dz*dz;

                                                                                       
                                                                                        
        const int se0=map->surfedges[f.firstedge+0];
        const int se1=map->surfedges[f.firstedge+1];
        const int se2=map->surfedges[f.firstedge+2];
        if(std::abs(se0)>=(int)map->edges.size() || std::abs(se1)>=(int)map->edges.size() || std::abs(se2)>=(int)map->edges.size()) continue;
        const int vi0=map->edges[std::abs(se0)].v[se0<0?1:0];
        const int vi1=map->edges[std::abs(se1)].v[se1<0?1:0];
        const int vi2=map->edges[std::abs(se2)].v[se2<0?1:0];
        if(vi0<0||vi1<0||vi2<0 || vi0>=(int)map->vertices.size() || vi1>=(int)map->vertices.size() || vi2>=(int)map->vertices.size()) continue;

        const Vec3 p0=map->vertices[vi0].position, p1=map->vertices[vi1].position, p2=map->vertices[vi2].position;
        Vec3 n=(p1-p0).cross(p2-p0);
        const float nl=n.length();
        if(nl>1e-5f) n=n*(1.0f/nl);
        const float planeDist=nl>1e-5f ? std::abs((srcPos-p0).dot(n)) : 9999.0f;
        if(planeDist>256.0f && boundsD2>64.0f*64.0f) continue;

        const float score=boundsD2 + planeDist*planeDist*1.75f;
        if(score<bestScore){bestScore=score;best=i;}
    }
    if(best==map->faces.size()) return false;
    const BSPFace& f=map->faces[best];
    const auto& ti=map->texinfos[f.texinfo];
    float u=srcPos.x*ti.lightmapVecs[0][0]+srcPos.y*ti.lightmapVecs[0][1]+srcPos.z*ti.lightmapVecs[0][2]+ti.lightmapVecs[0][3];
    float v=srcPos.x*ti.lightmapVecs[1][0]+srcPos.y*ti.lightmapVecs[1][1]+srcPos.z*ti.lightmapVecs[1][2]+ti.lightmapVecs[1][3];
    lx=u-(float)f.LightmapTextureMinsInLuxels[0];
    ly=v-(float)f.LightmapTextureMinsInLuxels[1];
    return true;
}

static Vec3 bspSampleRawLightmap(const BSP_L* map, size_t face, float lx, float ly, float& brightness) {
    const BSPFace& f = map->faces[face];
    if (f.texinfo < 0 || static_cast<size_t>(f.texinfo) >= map->texinfos.size()) {
        brightness = 1.0f;
        return Vec3{1, 1, 1};
    }

    const int w = f.LightmapTextureSizeInLuxels[0] + 1;
    const int h = f.LightmapTextureSizeInLuxels[1] + 1;
    if (w <= 0 || h <= 0 || f.lightofs < 0 ||
        (f.lightofs % static_cast<int>(sizeof(ColorRGBExp32))) != 0) {
        brightness = 1.0f;
        return Vec3{1, 1, 1};
    }

    constexpr int kMaxLightStyles = 4;
    constexpr int kBumpLightmapsPerStyle = 4;
    const size_t count = static_cast<size_t>(w) * static_cast<size_t>(h);

    int styleCount = 0;
    while (styleCount < kMaxLightStyles &&
           static_cast<unsigned char>(f.styles[styleCount]) != 255) {
        ++styleCount;
    }
    if (styleCount == 0) {
        brightness = 1.0f;
        return Vec3{1, 1, 1};
    }

    const bool hasBumpLight = (map->texinfos[f.texinfo].flags & 0x0800) != 0;
    const size_t styleStride = count * (hasBumpLight ? kBumpLightmapsPerStyle : 1u);
    const size_t start = static_cast<size_t>(f.lightofs) / sizeof(ColorRGBExp32);

    if (styleStride == 0 ||
        static_cast<size_t>(styleCount) > std::numeric_limits<size_t>::max() / styleStride ||
        start > map->lightingData.size() ||
        styleStride * static_cast<size_t>(styleCount) > map->lightingData.size() - start) {
        brightness = 1.0f;
        return Vec3{1, 1, 1};
    }

                                                                                       
                                                                                      
    float sx = std::max(0.0f, std::min(static_cast<float>(w - 1), lx));
    float sy = std::max(0.0f, std::min(static_cast<float>(h - 1), ly));
    const int x0 = static_cast<int>(std::floor(sx));
    const int y0 = static_cast<int>(std::floor(sy));
    const int x1 = std::min(w - 1, x0 + 1);
    const int y1 = std::min(h - 1, y0 + 1);
    const float fx = sx - static_cast<float>(x0);
    const float fy = sy - static_cast<float>(y0);

    const int ids[4][2] = {{x0, y0}, {x1, y0}, {x0, y1}, {x1, y1}};
    const float weights[4] = {
        (1.0f - fx) * (1.0f - fy),
        fx * (1.0f - fy),
        (1.0f - fx) * fy,
        fx * fy
    };

    Vec3 sum{0, 0, 0};
    float lum = 0.0f;

    for (int i = 0; i < 4; ++i) {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;

        const size_t sample = static_cast<size_t>(ids[i][1] * w + ids[i][0]);
        for (int style = 0; style < styleCount; ++style) {
            const auto& c = map->lightingData[start + static_cast<size_t>(style) * styleStride + sample];
            const float e = std::ldexp(1.0f, static_cast<int>(c.exponent));
            r += (static_cast<float>(c.r) / 255.0f) * e;
            g += (static_cast<float>(c.g) / 255.0f) * e;
            b += (static_cast<float>(c.b) / 255.0f) * e;
        }

        sum = sum + Vec3{r * weights[i], g * weights[i], b * weights[i]};
        lum += (0.2126f * r + 0.7152f * g + 0.0722f * b) * weights[i];
    }

    if (!std::isfinite(lum) || !std::isfinite(sum.x) ||
        !std::isfinite(sum.y) || !std::isfinite(sum.z)) {
        brightness = 1.0f;
        return Vec3{1, 1, 1};
    }

    brightness = std::max(0.35f,
                         std::min(1.18f, 0.46f + (lum / (1.0f + lum)) * 0.72f));

    const float chromaMax = std::max(sum.x, std::max(sum.y, sum.z));
    if (chromaMax < 0.0001f) return Vec3{1, 1, 1};

    Vec3 chroma = sum * (1.0f / chromaMax);
    chroma.x = 0.55f + 0.45f * std::max(0.0f, std::min(1.20f, chroma.x));
    chroma.y = 0.55f + 0.45f * std::max(0.0f, std::min(1.20f, chroma.y));
    chroma.z = 0.55f + 0.45f * std::max(0.0f, std::min(1.20f, chroma.z));
    return chroma;
}

Vec3 BSP_L::sampleLightmapLighting(const Vec3& worldPos, float* outBrightness) const {
    size_t best=faces.size();
    float lx=0.0f,ly=0.0f;
    if(!bspFindLightmapSample(this,worldPos,best,lx,ly)) {
        if(outBrightness) *outBrightness=1.0f;
        return Vec3{1,1,1};
    }
    float brightness=1.0f;
    Vec3 color=bspSampleRawLightmap(this,best,lx,ly,brightness);
    if(outBrightness) *outBrightness=brightness;
    return color;
}

Vec3 BSP_L::sampleLightmapColor(const Vec3& worldPos) const {
    return sampleLightmapLighting(worldPos,nullptr);
}

float BSP_L::sampleLightmapBrightness(const Vec3& worldPos) const {
    float brightness=1.0f;
    (void)sampleLightmapLighting(worldPos,&brightness);
    return brightness;
}
