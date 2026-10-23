#include "bsp_l.h"
#include "cubemap_l.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <GLFW/glfw3.h>

#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_TEXTURE1
#define GL_TEXTURE1 0x84C1
#endif
#ifndef GL_ACTIVE_TEXTURE
#define GL_ACTIVE_TEXTURE 0x84E0
#endif
#ifndef GL_COMBINE
#define GL_COMBINE 0x8570
#endif
#ifndef GL_RGB_SCALE
#define GL_RGB_SCALE 0x8573
#endif
#ifndef GL_SOURCE0_RGB
#define GL_SOURCE0_RGB 0x8580
#endif
#ifndef GL_SOURCE1_RGB
#define GL_SOURCE1_RGB 0x8581
#endif
#ifndef GL_OPERAND0_RGB
#define GL_OPERAND0_RGB 0x8590
#endif
#ifndef GL_OPERAND1_RGB
#define GL_OPERAND1_RGB 0x8591
#endif
#ifndef GL_SOURCE0_ALPHA
#define GL_SOURCE0_ALPHA 0x8588
#endif
#ifndef GL_SOURCE1_ALPHA
#define GL_SOURCE1_ALPHA 0x8589
#endif
#ifndef GL_OPERAND0_ALPHA
#define GL_OPERAND0_ALPHA 0x8598
#endif
#ifndef GL_OPERAND1_ALPHA
#define GL_OPERAND1_ALPHA 0x8599
#endif
#ifndef GL_PREVIOUS
#define GL_PREVIOUS 0x8578
#endif
#ifndef GL_PRIMARY_COLOR
#define GL_PRIMARY_COLOR 0x8577
#endif
#ifndef GL_TEXTURE_ENV_MODE
#define GL_TEXTURE_ENV_MODE 0x2200
#endif
#ifndef GL_COMBINE_RGB
#define GL_COMBINE_RGB 0x8571
#endif
#ifndef GL_COMBINE_ALPHA
#define GL_COMBINE_ALPHA 0x8572
#endif
#ifndef GL_MODULATE
#define GL_MODULATE 0x2100
#endif
#ifndef GL_REPLACE
#define GL_REPLACE 0x1E01
#endif
typedef void (*PFNGLACTIVETEXTUREPROC_BSP)(GLenum);
typedef void (*PFNGLMULTITEXCOORD2FPROC_BSP)(GLenum, GLfloat, GLfloat);
typedef void (*PFNGLMULTITEXCOORD3FPROC_BSP)(GLenum, GLfloat, GLfloat, GLfloat);
static PFNGLACTIVETEXTUREPROC_BSP glActiveTexture_BSP = nullptr;
static PFNGLMULTITEXCOORD2FPROC_BSP glMultiTexCoord2f_BSP = nullptr;
static PFNGLMULTITEXCOORD3FPROC_BSP glMultiTexCoord3f_BSP = nullptr;

static void EnsureBSPMultitexture() {
    if (!glActiveTexture_BSP) glActiveTexture_BSP = reinterpret_cast<PFNGLACTIVETEXTUREPROC_BSP>(glfwGetProcAddress("glActiveTexture"));
    if (!glMultiTexCoord2f_BSP) glMultiTexCoord2f_BSP = reinterpret_cast<PFNGLMULTITEXCOORD2FPROC_BSP>(glfwGetProcAddress("glMultiTexCoord2f"));
    if (!glMultiTexCoord3f_BSP) glMultiTexCoord3f_BSP = reinterpret_cast<PFNGLMULTITEXCOORD3FPROC_BSP>(glfwGetProcAddress("glMultiTexCoord3f"));
}

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

void BSP_L::releaseRenderResources() {
    if(opaqueWorldList){glDeleteLists(opaqueWorldList,1);opaqueWorldList=0;}
    if(displacementList){glDeleteLists(displacementList,1);displacementList=0;}
    if(!faceLightmapTextures.empty()){std::vector<unsigned int> ids;for(unsigned int id:faceLightmapTextures)if(id)ids.push_back(id);if(!ids.empty())glDeleteTextures((GLsizei)ids.size(),ids.data());}
    faceLightmapTextures.clear();
}

bool BSP_L::loadMapFromData(const std::vector<char>& data, const std::string& sourceName) {
    releaseRenderResources();
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
    std::vector<ColorRGBExp32> lightingHDR;
    readLumpSafe(data, h.lumps[LUMP_LIGHTING_HDR], lightingHDR, fileSize, "Lighting HDR");
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
    std::vector<BSPFace> facesHDR;
    readLumpSafe(data, h.lumps[LUMP_FACES_HDR], facesHDR, fileSize, "Faces HDR");
    if (faces.empty() && !facesHDR.empty()) faces = facesHDR;
    if (!lightingHDR.empty()) {
        lightingData.swap(lightingHDR);
        if (facesHDR.size() == faces.size()) faces = facesHDR;
    }

                                                                               
                                                       
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
    opaqueWorldList=glGenLists(1);
    if(opaqueWorldList){glNewList(opaqueWorldList,GL_COMPILE);glDisable(GL_BLEND);glDisable(GL_ALPHA_TEST);glColor4f(1,1,1,1);for(size_t fIdx=0;fIdx<faces.size();++fIdx){if(!faceRenderable[fIdx])continue;const auto& f=faces[fIdx];const auto& ti=texinfos[f.texinfo];unsigned int tex=0;if(ti.texdata>=0){auto it=glTextures.find(ti.texdata);if(it!=glTextures.end())tex=it->second;}if(textureSystem && textureSystem->materialUsesTransparency(tex))continue;if(textureSystem && textureSystem->materialUsesAlphaTest(tex)){glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,0.01f);}else{glDisable(GL_ALPHA_TEST);}glBindTexture(GL_TEXTURE_2D,tex);glBegin(GL_POLYGON);if(f.numedges>=3){const int a=surfedges[f.firstedge],b=surfedges[f.firstedge+1],c=surfedges[f.firstedge+2];const Vec3 v0=vertices[edges[std::abs(a)].v[a<0?1:0]].position;const Vec3 v1=vertices[edges[std::abs(b)].v[b<0?1:0]].position;const Vec3 v2=vertices[edges[std::abs(c)].v[c<0?1:0]].position;const Vec3 n=(v1-v0).cross(v2-v0).normalize();glNormal3f(n.x,n.y,n.z);}for(int i=0;i<f.numedges;++i){const int se=surfedges[f.firstedge+i];const auto& v=vertices[edges[std::abs(se)].v[se<0?1:0]];float u=v.position.x*ti.textureVecs[0][0]+(-v.position.z)*ti.textureVecs[0][1]+v.position.y*ti.textureVecs[0][2]+ti.textureVecs[0][3];float vt=v.position.x*ti.textureVecs[1][0]+(-v.position.z)*ti.textureVecs[1][1]+v.position.y*ti.textureVecs[1][2]+ti.textureVecs[1][3];if(ti.texdata>=0&&ti.texdata<(int)texdatas.size()){float tw=(float)texdatas[ti.texdata].width,th=(float)texdatas[ti.texdata].height;if(tw>0&&th>0){u/=tw;vt/=th;}}glTexCoord2f(u,vt);glVertex3f(v.position.x,v.position.y,v.position.z);}glEnd();}glEndList();}
    if(!dispTriangles.empty()){displacementList=glGenLists(1);if(displacementList){glNewList(displacementList,GL_COMPILE);glDisable(GL_BLEND);glDisable(GL_ALPHA_TEST);glColor4f(1,1,1,1);glBegin(GL_TRIANGLES);int curTex=-1;for(const auto& tri:dispTriangles){if(tri.texID!=curTex){glEnd();curTex=tri.texID;auto it=glTextures.find(curTex);glBindTexture(GL_TEXTURE_2D,it==glTextures.end()?0:it->second);glBegin(GL_TRIANGLES);}const Vec3 n=(tri.v[1]-tri.v[0]).cross(tri.v[2]-tri.v[0]).normalize();glNormal3f(n.x,n.y,n.z);for(int i=0;i<3;++i){glTexCoord2f(tri.uv[i].x,tri.uv[i].y);glVertex3f(tri.v[i].x,tri.v[i].y,tri.v[i].z);}}glEnd();glEndList();}}
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
        std::vector<Vec2> gridluv(totalVerts);
        const auto& lti = texinfos[f.texinfo];

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
                Vec3 lp = grid[x + y * dim];
                float lu = lp.x * lti.lightmapVecs[0][0] + (-lp.z) * lti.lightmapVecs[0][1] + lp.y * lti.lightmapVecs[0][2] + lti.lightmapVecs[0][3];
                float lv = lp.x * lti.lightmapVecs[1][0] + (-lp.z) * lti.lightmapVecs[1][1] + lp.y * lti.lightmapVecs[1][2] + lti.lightmapVecs[1][3];
                lu = (lu - f.LightmapTextureMinsInLuxels[0]) + 0.5f;
                lv = (lv - f.LightmapTextureMinsInLuxels[1]) + 0.5f;
                lu /= (f.LightmapTextureSizeInLuxels[0] + 1);
                lv /= (f.LightmapTextureSizeInLuxels[1] + 1);
                gridluv[x + y * dim] = {lu,lv};
            }
        }

        int tex = texinfos[f.texinfo].texdata;
        for (int y = 0; y < dim - 1; ++y) {
            for (int x = 0; x < dim - 1; ++x) {
                int i0 = x + y * dim;
                int i1 = i0 + 1;
                int i2 = x + (y + 1) * dim;
                int i3 = i2 + 1;

                dispTriangles.push_back({ { grid[i0], grid[i2], grid[i1] }, { griduv[i0], griduv[i2], griduv[i1] }, { gridluv[i0], gridluv[i2], gridluv[i1] }, tex, static_cast<int>(d.mapFace) });
                dispTriangles.push_back({ { grid[i1], grid[i2], grid[i3] }, { griduv[i1], griduv[i2], griduv[i3] }, { gridluv[i1], gridluv[i2], gridluv[i3] }, tex, static_cast<int>(d.mapFace) });
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
        const auto& f = faces[fIdx];
        if (!faceRenderable[fIdx] && f.dispinfo < 0) continue;

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
                const size_t base = dataIdx + static_cast<size_t>(style) * styleStride + i;
                if (hasBumpLight) {
                    float br = 0.0f, bg = 0.0f, bb = 0.0f;
                    for (size_t basis = 0; basis < kBumpLightmapsPerStyle; ++basis) {
                        const auto& luxel = lightingData[base + basis * count];
                        const float multiplier = std::ldexp(1.0f, static_cast<int>(luxel.exponent));
                        br += (static_cast<float>(luxel.r) / 255.0f) * multiplier;
                        bg += (static_cast<float>(luxel.g) / 255.0f) * multiplier;
                        bb += (static_cast<float>(luxel.b) / 255.0f) * multiplier;
                    }
                    linearR += br * 0.25f;
                    linearG += bg * 0.25f;
                    linearB += bb * 0.25f;
                } else {
                    const auto& luxel = lightingData[base];
                    const float multiplier = std::ldexp(1.0f, static_cast<int>(luxel.exponent));
                    linearR += (static_cast<float>(luxel.r) / 255.0f) * multiplier;
                    linearG += (static_cast<float>(luxel.g) / 255.0f) * multiplier;
                    linearB += (static_cast<float>(luxel.b) / 255.0f) * multiplier;
                }
            }

                                                                                  
                                                                                
                                                                                       
            const auto encode=[](float v){ return std::pow(std::clamp(v,0.0f,1.0f),0.45454547f); };
            const int r = std::min(255, std::max(0, static_cast<int>(std::lround(encode(linearR) * 255.0f))));
            const int g = std::min(255, std::max(0, static_cast<int>(std::lround(encode(linearG) * 255.0f))));
            const int b = std::min(255, std::max(0, static_cast<int>(std::lround(encode(linearB) * 255.0f))));

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



void BSP_L::renderFlashlight(const Vec3& origin, const Vec3& direction, float range, float coneDegrees) {
    const Vec3 lightDir = direction.normalize();
    const float safeRange = std::max(1.0f, range);
    const float outer = std::cos(std::max(1.0f, std::min(89.0f, coneDegrees)) * 3.14159265358979323846f / 180.0f);
    const float inner = std::cos(std::max(0.5f, std::min(88.0f, coneDegrees * 0.72f)) * 3.14159265358979323846f / 180.0f);

    glEnable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    auto vertexIntensity = [&](const Vec3& point, const Vec3& normal) {
        const Vec3 toPoint = point - origin;
        const float dist = toPoint.length();
        if (dist <= 0.001f || dist >= safeRange) return 0.0f;
        const Vec3 toPointDir = toPoint * (1.0f / dist);
        const float coneDot = toPointDir.x * lightDir.x + toPointDir.y * lightDir.y + toPointDir.z * lightDir.z;
        if (coneDot <= outer) return 0.0f;
        float coneFade = (coneDot - outer) / std::max(0.001f, inner - outer);
        coneFade = std::max(0.0f, std::min(1.0f, coneFade));
        coneFade = coneFade * coneFade * (3.0f - 2.0f * coneFade);
        float distanceFade = 1.0f - dist / safeRange;
        distanceFade *= distanceFade;
        const Vec3 toLight = (origin - point).normalize();
        const float facing = std::abs(normal.x * toLight.x + normal.y * toLight.y + normal.z * toLight.z);
        const float surfaceFade = 0.25f + 0.75f * facing;
        return std::min(1.0f, coneFade * distanceFade * surfaceFade * 1.35f);
    };

    for (size_t fIdx = 0; fIdx < faces.size(); ++fIdx) {
        if (!faceRenderable[fIdx]) continue;
        const auto& f = faces[fIdx];
        if (f.numedges < 3 || f.texinfo < 0 || static_cast<size_t>(f.texinfo) >= texinfos.size()) continue;
        unsigned int tex = 0;
        const auto& ti = texinfos[f.texinfo];
        if (ti.texdata >= 0) {
            auto it = glTextures.find(ti.texdata);
            if (it != glTextures.end()) tex = it->second;
        }
        if (textureSystem && textureSystem->materialUsesTransparency(tex)) continue;

        const int se0 = surfedges[f.firstedge];
        const int se1 = surfedges[f.firstedge + 1];
        const int se2 = surfedges[f.firstedge + 2];
        const Vec3 v0 = vertices[edges[std::abs(se0)].v[se0 < 0 ? 1 : 0]].position;
        const Vec3 v1 = vertices[edges[std::abs(se1)].v[se1 < 0 ? 1 : 0]].position;
        const Vec3 v2 = vertices[edges[std::abs(se2)].v[se2 < 0 ? 1 : 0]].position;
        Vec3 normal = (v1 - v0).cross(v2 - v0);
        const float nl = normal.length();
        if (nl <= 0.0001f) continue;
        normal = normal * (1.0f / nl);

        glBindTexture(GL_TEXTURE_2D, tex);
        glBegin(GL_POLYGON);
        for (int i = 0; i < f.numedges; ++i) {
            const int se = surfedges[f.firstedge + i];
            const auto& v = vertices[edges[std::abs(se)].v[se < 0 ? 1 : 0]];
            float u = v.position.x * ti.textureVecs[0][0] + (-v.position.z) * ti.textureVecs[0][1] + v.position.y * ti.textureVecs[0][2] + ti.textureVecs[0][3];
            float vt = v.position.x * ti.textureVecs[1][0] + (-v.position.z) * ti.textureVecs[1][1] + v.position.y * ti.textureVecs[1][2] + ti.textureVecs[1][3];
            if (ti.texdata >= 0 && ti.texdata < static_cast<int>(texdatas.size())) {
                const float tw = static_cast<float>(texdatas[ti.texdata].width);
                const float th = static_cast<float>(texdatas[ti.texdata].height);
                if (tw > 0.0f && th > 0.0f) {
                    u /= tw;
                    vt /= th;
                }
            }
            const float intensity = vertexIntensity(v.position, normal);
            glColor3f(intensity, intensity * 0.98f, intensity * 0.90f);
            glTexCoord2f(u, vt);
            glVertex3f(v.position.x, v.position.y, v.position.z);
        }
        glEnd();
    }

    for (const auto& tri : dispTriangles) {
        if (tri.texID >= 0) {
            auto it = glTextures.find(tri.texID);
            glBindTexture(GL_TEXTURE_2D, it == glTextures.end() ? 0 : it->second);
        } else {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        Vec3 normal = (tri.v[1] - tri.v[0]).cross(tri.v[2] - tri.v[0]);
        const float nl = normal.length();
        if (nl <= 0.0001f) continue;
        normal = normal * (1.0f / nl);
        glBegin(GL_TRIANGLES);
        for (int i = 0; i < 3; ++i) {
            const float intensity = vertexIntensity(tri.v[i], normal);
            glColor3f(intensity, intensity * 0.98f, intensity * 0.90f);
            glTexCoord2f(tri.uv[i].x, tri.uv[i].y);
            glVertex3f(tri.v[i].x, tri.v[i].y, tri.v[i].z);
        }
        glEnd();
    }

    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

void BSP_L::renderCubemapReflections(const Vec3& viewerPosition, const Cubemap_L& cubemaps) {
    if (!textureSystem || !cubemaps.hasCubemaps()) return;
    const unsigned int cubeTexture = cubemaps.nearestTexture(viewerPosition);
    if (!cubeTexture) return;

    EnsureBSPMultitexture();
    const bool multitextureAvailable = glActiveTexture_BSP && glMultiTexCoord2f_BSP && glMultiTexCoord3f_BSP;
    if (!multitextureAvailable) return;

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_EQUAL);
    glDepthMask(GL_FALSE);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);

    auto setupMaskUnit = [&](unsigned int maskTexture, bool alphaMask) {
        glDisable(GL_TEXTURE_CUBE_MAP);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, maskTexture);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, alphaMask ? GL_SRC_ALPHA : GL_SRC_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PRIMARY_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PRIMARY_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
    };

    auto setupCubeUnit = [&](bool masked) {
        glDisable(GL_TEXTURE_2D);
        glEnable(GL_TEXTURE_CUBE_MAP);
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubeTexture);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, masked ? GL_PREVIOUS : GL_PRIMARY_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PRIMARY_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
    };

    float curTint[3] = { 0.5f, 0.5f, 0.5f };

    auto beginMaterial = [&](unsigned int tex) -> bool {
        if (!textureSystem->materialUsesEnvmap(tex)) return false;
        const unsigned int maskTexture = textureSystem->getEnvmapMaskTexture(tex);
        const bool baseAlphaMask = textureSystem->materialUsesBaseAlphaEnvmapMask(tex);
        const bool hasMask = baseAlphaMask || maskTexture != 0;
        const auto tint = textureSystem->getEnvmapTint(tex);
        const float strength = 0.5f;
        curTint[0] = std::max(0.0f, std::min(1.0f, tint[0])) * strength;
        curTint[1] = std::max(0.0f, std::min(1.0f, tint[1])) * strength;
        curTint[2] = std::max(0.0f, std::min(1.0f, tint[2])) * strength;
        const unsigned int normalAlphaMask = textureSystem->getEnvmapNormalAlphaMaskTexture(tex);
        const bool useNormalAlphaMask = !baseAlphaMask && maskTexture == 0 && normalAlphaMask != 0;
        const bool actualMask = hasMask || useNormalAlphaMask;
        if (multitextureAvailable) {
            glActiveTexture_BSP(GL_TEXTURE0);
            if (actualMask) setupMaskUnit(baseAlphaMask ? tex : (maskTexture ? maskTexture : normalAlphaMask), baseAlphaMask || useNormalAlphaMask);
            else {
                glDisable(GL_TEXTURE_2D);
                glDisable(GL_TEXTURE_CUBE_MAP);
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            glActiveTexture_BSP(GL_TEXTURE1);
            setupCubeUnit(actualMask);
            glActiveTexture_BSP(GL_TEXTURE0);
        } else {
            if (actualMask) return false;
            glDisable(GL_TEXTURE_2D);
            glEnable(GL_TEXTURE_CUBE_MAP);
            glBindTexture(GL_TEXTURE_CUBE_MAP, cubeTexture);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
            glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
            glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PRIMARY_COLOR);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
        }
        return true;
    };

    auto endMaterial = [&]() {
        if (multitextureAvailable) {
            glActiveTexture_BSP(GL_TEXTURE1);
            glDisable(GL_TEXTURE_CUBE_MAP);
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
            glActiveTexture_BSP(GL_TEXTURE0);
            glDisable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, 0);
        } else {
            glDisable(GL_TEXTURE_CUBE_MAP);
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        }
    };

    auto reflectionVector = [&](const Vec3& point, const Vec3& normal) {
        Vec3 toViewer = (viewerPosition - point).normalize();
        const float d = normal.dot(toViewer);
        Vec3 r = normal * (2.0f * d) - toViewer;
        const float len = r.length();
        if (len > 0.0001f) r = r * (1.0f / len);
        return r;
    };

    auto emitTexCoords = [&](float u, float v, const Vec3& r) {
        glMultiTexCoord2f_BSP(GL_TEXTURE0, u, v);
        glMultiTexCoord3f_BSP(GL_TEXTURE1, r.x, r.y, r.z);
    };

    for (size_t i = 0; i < faces.size(); ++i) {
        if (!faceRenderable[i]) continue;
        const auto& f = faces[i];
        if (f.texinfo < 0 || f.texinfo >= static_cast<int>(texinfos.size())) continue;
        const auto& ti = texinfos[f.texinfo];
        auto texIt = glTextures.find(ti.texdata);
        if (texIt == glTextures.end()) continue;
        const unsigned int tex = texIt->second;
        if (!beginMaterial(tex)) continue;

        Vec3 normal{0.0f, 0.0f, 1.0f};
        if (f.numedges >= 3) {
            const int a = surfedges[f.firstedge];
            const int b = surfedges[f.firstedge + 1];
            const int c = surfedges[f.firstedge + 2];
            const Vec3 v0 = vertices[edges[std::abs(a)].v[a < 0 ? 1 : 0]].position;
            const Vec3 v1 = vertices[edges[std::abs(b)].v[b < 0 ? 1 : 0]].position;
            const Vec3 v2 = vertices[edges[std::abs(c)].v[c < 0 ? 1 : 0]].position;
            normal = (v1 - v0).cross(v2 - v0).normalize();
        }

        glBegin(GL_POLYGON);
        for (int vIndex = 0; vIndex < f.numedges; ++vIndex) {
            const int se = surfedges[f.firstedge + vIndex];
            const auto& v = vertices[edges[std::abs(se)].v[se < 0 ? 1 : 0]];
            const float u = (v.position.x * ti.textureVecs[0][0] + (-v.position.z) * ti.textureVecs[0][1] + v.position.y * ti.textureVecs[0][2] + ti.textureVecs[0][3]) / std::max(1.0f, static_cast<float>(texdatas[ti.texdata].width));
            const float vt = (v.position.x * ti.textureVecs[1][0] + (-v.position.z) * ti.textureVecs[1][1] + v.position.y * ti.textureVecs[1][2] + ti.textureVecs[1][3]) / std::max(1.0f, static_cast<float>(texdatas[ti.texdata].height));
            const Vec3 toViewer = (viewerPosition - v.position).normalize();
            const float ndv = std::abs(normal.dot(toViewer));
            const float rim = 1.0f - ndv;
            const float fres = 0.25f + 0.75f * rim * rim * rim;
            glColor4f(curTint[0] * fres, curTint[1] * fres, curTint[2] * fres, 1.0f);
            emitTexCoords(u, vt, reflectionVector(v.position, normal));
            glVertex3f(v.position.x, v.position.y, v.position.z);
        }
        glEnd();
        endMaterial();
    }

    for (const auto& tri : dispTriangles) {
        if (tri.faceIndex < 0 || tri.faceIndex >= static_cast<int>(faces.size()) || !faceRenderable[static_cast<size_t>(tri.faceIndex)]) continue;
        auto texIt = glTextures.find(tri.texID);
        if (texIt == glTextures.end()) continue;
        const unsigned int tex = texIt->second;
        if (!beginMaterial(tex)) continue;
        const Vec3 normal = (tri.v[1] - tri.v[0]).cross(tri.v[2] - tri.v[0]).normalize();
        glBegin(GL_TRIANGLES);
        for (int i = 0; i < 3; ++i) {
            const Vec3 toViewer = (viewerPosition - tri.v[i]).normalize();
            const float ndv = std::abs(normal.dot(toViewer));
            const float rim = 1.0f - ndv;
            const float fres = 0.25f + 0.75f * rim * rim * rim;
            glColor4f(curTint[0] * fres, curTint[1] * fres, curTint[2] * fres, 1.0f);
            emitTexCoords(tri.uv[i].x, tri.uv[i].y, reflectionVector(tri.v[i], normal));
            glVertex3f(tri.v[i].x, tri.v[i].y, tri.v[i].z);
        }
        glEnd();
        endMaterial();
    }

    if (multitextureAvailable) glActiveTexture_BSP(GL_TEXTURE0);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glPopAttrib();
}
void BSP_L::renderLightmaps() {
    if (lightingData.empty()) return;

    EnsureBSPMultitexture();
    const bool multitextureAvailable = glActiveTexture_BSP && glMultiTexCoord2f_BSP;

    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_DST_COLOR, GL_ZERO);
    glDisable(GL_LIGHTING);
    glDisable(GL_ALPHA_TEST);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    if (multitextureAvailable) glActiveTexture_BSP(GL_TEXTURE0);

    for (size_t fIdx = 0; fIdx < faces.size(); ++fIdx) {
        if (!faceRenderable[fIdx] || faceLightmapTextures[fIdx] == 0) continue;

        const auto& f = faces[fIdx];
        const auto& ti = texinfos[f.texinfo];
        unsigned int baseTex = 0;
        if (ti.texdata >= 0) {
            auto it = glTextures.find(ti.texdata);
            if (it != glTextures.end()) baseTex = it->second;
        }
        if (textureSystem && textureSystem->materialUsesTransparency(baseTex)) continue;
        const bool masked = multitextureAvailable && textureSystem && baseTex && textureSystem->materialUsesAlphaTest(baseTex);
        if (masked) {
            glActiveTexture_BSP(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, baseTex);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
            glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_REPLACE);
            glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PRIMARY_COLOR);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
            glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
            glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_TEXTURE);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
            glActiveTexture_BSP(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, faceLightmapTextures[fIdx]);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            glActiveTexture_BSP(GL_TEXTURE0);
            glEnable(GL_ALPHA_TEST);
            glAlphaFunc(GL_GREATER, 0.01f);
        } else {
            if (multitextureAvailable) glActiveTexture_BSP(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, faceLightmapTextures[fIdx]);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        }

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

            if (masked) {
                float bu = v.position.x * ti.textureVecs[0][0] + (-v.position.z) * ti.textureVecs[0][1] + v.position.y * ti.textureVecs[0][2] + ti.textureVecs[0][3];
                float bv = v.position.x * ti.textureVecs[1][0] + (-v.position.z) * ti.textureVecs[1][1] + v.position.y * ti.textureVecs[1][2] + ti.textureVecs[1][3];
                if (ti.texdata >= 0 && ti.texdata < (int)texdatas.size()) {
                    float tw = (float)texdatas[ti.texdata].width, th = (float)texdatas[ti.texdata].height;
                    if (tw > 0.0f && th > 0.0f) { bu /= tw; bv /= th; }
                }
                glMultiTexCoord2f_BSP(GL_TEXTURE0, bu, bv);
                glMultiTexCoord2f_BSP(GL_TEXTURE1, u, vt);
            } else {
                glTexCoord2f(u, vt);
            }
            glVertex3f(v.position.x, v.position.y, v.position.z);
        }
        glEnd();
        if (masked) {
            glDisable(GL_ALPHA_TEST);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        }
    }

    for (const auto& tri : dispTriangles) {
        if (tri.faceIndex < 0 || static_cast<size_t>(tri.faceIndex) >= faceLightmapTextures.size()) continue;
        const unsigned int lm = faceLightmapTextures[static_cast<size_t>(tri.faceIndex)];
        if (!lm) continue;
        glBindTexture(GL_TEXTURE_2D, lm);
        glBegin(GL_TRIANGLES);
        for (int i = 0; i < 3; ++i) {
            glTexCoord2f(tri.luv[i].x, tri.luv[i].y);
            glVertex3f(tri.v[i].x, tri.v[i].y, tri.v[i].z);
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
    glColor4f(1,1,1,1);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    if(opaqueWorldList) glCallList(opaqueWorldList);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    if(displacementList) glCallList(displacementList);
}

void BSP_L::renderTransparentWorld() {
    glColor4f(1,1,1,1);
    glDisable(GL_ALPHA_TEST);
    EnsureBSPMultitexture();
    const bool multitextureAvailable = glActiveTexture_BSP && glMultiTexCoord2f_BSP;
    if(multitextureAvailable) glActiveTexture_BSP(GL_TEXTURE0);

    for(size_t fIdx=0;fIdx<faces.size();++fIdx){
        if(!faceRenderable[fIdx]) continue;
        const auto& f=faces[fIdx];
        const auto& ti=texinfos[f.texinfo];
        unsigned int tex=0;
        if(ti.texdata>=0){auto it=glTextures.find(ti.texdata);if(it!=glTextures.end())tex=it->second;}
        if(!textureSystem || !textureSystem->materialUsesTransparency(tex)) continue;
        const unsigned int lm = fIdx < faceLightmapTextures.size() ? faceLightmapTextures[fIdx] : 0;
        if(multitextureAvailable){
            glActiveTexture_BSP(GL_TEXTURE1);
            if(lm){glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,lm);}
            else{glDisable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,0);}
            glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
            glActiveTexture_BSP(GL_TEXTURE0);
        }
        glBindTexture(GL_TEXTURE_2D,tex);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        if(textureSystem->materialUsesAlphaTest(tex)){glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,0.01f);}else{glDisable(GL_ALPHA_TEST);}
        glBegin(GL_POLYGON);
        if(f.numedges>=3){
            const int a=surfedges[f.firstedge],b=surfedges[f.firstedge+1],c=surfedges[f.firstedge+2];
            const Vec3 v0=vertices[edges[std::abs(a)].v[a<0?1:0]].position;
            const Vec3 v1=vertices[edges[std::abs(b)].v[b<0?1:0]].position;
            const Vec3 v2=vertices[edges[std::abs(c)].v[c<0?1:0]].position;
            const Vec3 n=(v1-v0).cross(v2-v0).normalize();
            glNormal3f(n.x,n.y,n.z);
        }
        for(int i=0;i<f.numedges;++i){
            const int se=surfedges[f.firstedge+i];
            const auto& v=vertices[edges[std::abs(se)].v[se<0?1:0]];
            float u=v.position.x*ti.textureVecs[0][0]+(-v.position.z)*ti.textureVecs[0][1]+v.position.y*ti.textureVecs[0][2]+ti.textureVecs[0][3];
            float vt=v.position.x*ti.textureVecs[1][0]+(-v.position.z)*ti.textureVecs[1][1]+v.position.y*ti.textureVecs[1][2]+ti.textureVecs[1][3];
            if(ti.texdata>=0&&ti.texdata<(int)texdatas.size()){float tw=(float)texdatas[ti.texdata].width,th=(float)texdatas[ti.texdata].height;if(tw>0&&th>0){u/=tw;vt/=th;}}
            if(multitextureAvailable&&lm){
                float ox=v.position.x,oy=-v.position.z,oz=v.position.y;
                float lu=ox*ti.lightmapVecs[0][0]+oy*ti.lightmapVecs[0][1]+oz*ti.lightmapVecs[0][2]+ti.lightmapVecs[0][3];
                float lv=ox*ti.lightmapVecs[1][0]+oy*ti.lightmapVecs[1][1]+oz*ti.lightmapVecs[1][2]+ti.lightmapVecs[1][3];
                lu=(lu-f.LightmapTextureMinsInLuxels[0])+0.5f;
                lv=(lv-f.LightmapTextureMinsInLuxels[1])+0.5f;
                lu/=(f.LightmapTextureSizeInLuxels[0]+1);
                lv/=(f.LightmapTextureSizeInLuxels[1]+1);
                glMultiTexCoord2f_BSP(GL_TEXTURE0,u,vt);
                glMultiTexCoord2f_BSP(GL_TEXTURE1,lu,lv);
            }
            else if(multitextureAvailable) glMultiTexCoord2f_BSP(GL_TEXTURE0,u,vt);
            else glTexCoord2f(u,vt);
            glVertex3f(v.position.x,v.position.y,v.position.z);
        }
        glEnd();
    }
    if(multitextureAvailable){
        glActiveTexture_BSP(GL_TEXTURE1);
        glDisable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D,0);
        glActiveTexture_BSP(GL_TEXTURE0);
    }
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
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
        if(i>=map->faceRenderable.size()) continue;
        const BSPFace& f=map->faces[i];
        if(!map->faceRenderable[i] && f.dispinfo<0) continue;
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
            const size_t base = start + static_cast<size_t>(style) * styleStride + sample;
            if (hasBumpLight) {
                float br = 0.0f, bg = 0.0f, bb = 0.0f;
                for (size_t basis = 0; basis < kBumpLightmapsPerStyle; ++basis) {
                    const auto& c = map->lightingData[base + basis * count];
                    const float e = std::ldexp(1.0f, static_cast<int>(c.exponent));
                    br += (static_cast<float>(c.r) / 255.0f) * e;
                    bg += (static_cast<float>(c.g) / 255.0f) * e;
                    bb += (static_cast<float>(c.b) / 255.0f) * e;
                }
                r += br * 0.25f;
                g += bg * 0.25f;
                b += bb * 0.25f;
            } else {
                const auto& c = map->lightingData[base];
                const float e = std::ldexp(1.0f, static_cast<int>(c.exponent));
                r += (static_cast<float>(c.r) / 255.0f) * e;
                g += (static_cast<float>(c.g) / 255.0f) * e;
                b += (static_cast<float>(c.b) / 255.0f) * e;
            }
        }

        sum = sum + Vec3{r * weights[i], g * weights[i], b * weights[i]};
        lum += (0.2126f * r + 0.7152f * g + 0.0722f * b) * weights[i];
    }

    if (!std::isfinite(lum) || !std::isfinite(sum.x) ||
        !std::isfinite(sum.y) || !std::isfinite(sum.z)) {
        brightness = 1.0f;
        return Vec3{1, 1, 1};
    }

    brightness = std::max(0.0f, std::min(1.0f, lum));
    const auto encode=[](float v){ return std::pow(std::clamp(v,0.0f,1.0f),0.45454547f); };
    return {encode(sum.x),encode(sum.y),encode(sum.z)};
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
