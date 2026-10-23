#include <cmath>
#include <array>
#include "mdl_l.h"
#include "texture_l.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <GLFW/glfw3.h>

static const size_t MDL_HDR_NUMBODYPARTS_OFF = 232;
static const size_t MDL_HDR_BODYPARTIDX_OFF = 236;
static const size_t MDL_HDR_NUMTEXTURES_OFF = 204;
static const size_t MDL_HDR_TEXTUREIDX_OFF = 208;
static const size_t MDL_HDR_NUMCDTEX_OFF = 212;
static const size_t MDL_HDR_CDTEXIDX_OFF = 216;
static const size_t MDL_HDR_HULLMIN_OFF = 104;
static const size_t MDL_HDR_HULLMAX_OFF = 116;
static const size_t MDL_HDR_NUMBONES_OFF = 156;
static const size_t MDL_HDR_BONEIDX_OFF = 160;
static const size_t MDL_BONE_SIZE = 216;
static const size_t MDL_BONE_PARENT_OFF = 4;
static const size_t MDL_BONE_POSETObONE_OFF = 96;
static const size_t MDL_HDR_NUMLOCALANIM_OFF = 180;
static const size_t MDL_HDR_LOCALANIMIDX_OFF = 184;
static const size_t MDL_SEQ_NUMBLENDS_OFF = 56;
static const size_t MDL_SEQ_ANIMINDEXIDX_OFF = 60;
static const size_t MDL_SEQ_GROUPSIZE0_OFF = 68;
static const size_t MDL_SEQ_GROUPSIZE1_OFF = 72;
static const size_t MDL_ANIMDESC_SIZE = 100;

static const size_t MDL_TEXTURE_SIZE = 64;
static const size_t MDL_BODYPART_SIZE = 16;
static const size_t MDL_MODEL_SIZE = 148;
static const size_t MDL_MESH_SIZE = 116;

static const size_t MDL_BP_NUMMODELS = 4;
static const size_t MDL_BP_MODELIDX = 12;

static const size_t MDL_MOD_NUMMESH = 72;
static const size_t MDL_MOD_MESHIDX = 76;
static const size_t MDL_MOD_NUMVERTS = 80;
static const size_t MDL_MOD_VERTIDX = 84;

static const size_t MDL_MESH_MATERIAL = 0;
static const size_t MDL_MESH_NUMVERTS = 8;
static const size_t MDL_MESH_VERTOFF = 12;

static const size_t VVD_VERT_SIZE = 48;
static const size_t VVD_POS_OFF = 16;
static const size_t VVD_NORM_OFF = 28;
static const size_t VVD_UV_OFF = 40;
static const size_t VVD_HDR_LODVERTS = 16;
static const size_t VVD_HDR_FIXUPS = 48;
static const size_t VVD_HDR_FIXSTART = 52;
static const size_t VVD_HDR_VERTSTART = 56;

static const size_t VTX_BODYPART_SIZE = 8;
static const size_t VTX_MODEL_SIZE = 8;
static const size_t VTX_LOD_SIZE = 12;
static const size_t VTX_MESH_SIZE = 12;
static const size_t VTX_STRIPGRP_SIZE = 28;
static const size_t VTX_STRIP_SIZE = 28;
static const size_t VTX_VERT_SIZE = 9;

static const size_t VTX_HDR_NUMBODY = 28;
static const size_t VTX_HDR_BODYOFF = 32;

static const size_t VTX_SG_NUMVERTS = 0;
static const size_t VTX_SG_VERTOFF = 4;
static const size_t VTX_SG_NUMIDX = 8;
static const size_t VTX_SG_IDXOFF = 12;
static const size_t VTX_SG_NUMSTRIPS = 16;
static const size_t VTX_SG_STRIPOFF = 20;

static const size_t VTX_STRIP_NUMIDX = 0;
static const size_t VTX_STRIP_IDXOFF = 4;
static const size_t VTX_STRIP_FLAGS = 18;

static const size_t VTX_VERT_ORIGID = 4;

#define STRIP_IS_TRILIST  0x01
#define STRIP_IS_TRISTRIP 0x02

std::string MDL_L::readStr(const char* b, size_t s, size_t offset, size_t maxLen) {
    std::string result;
    for (size_t i = 0; i < maxLen && offset + i < s; ++i) {
        if (b[offset + i] == '\0') break;
        result += b[offset + i];
    }
    return result;
}

bool MDL_L::load(const std::string& mdlPath, Texture_L& texLoader, bool enableBindPoseSkinning) {
    loaded = false;
    hasRenderBounds = false;
    renderBoundsMin = Vec3{0,0,0};
    renderBoundsMax = Vec3{0,0,0};
    meshes.clear();
    bindMeshVertices.clear();
    bones.clear(); sequences.clear();
    m_sequenceIndex=-1; m_sequenceCycle=0.0f; m_sequenceTime=0.0f; m_sequenceName.clear(); m_sequenceFinished=false;
    textureNames.clear();
    textureDirs.clear();
    if (displayList != 0) {
        glDeleteLists(displayList, 1);
        displayList = 0;
    }

    std::string basePath = mdlPath;
    std::transform(basePath.begin(), basePath.end(), basePath.begin(), ::tolower);
    std::replace(basePath.begin(), basePath.end(), '\\', '/');

    std::string base = basePath;
    if (base.length() > 4 && base.substr(base.length() - 4) == ".mdl") {
        base = base.substr(0, base.length() - 4);
    }

    std::vector<char> mdlData, vvdData, vtxData;

    if (!texLoader.getFileRawData(base + ".mdl", mdlData) || mdlData.size() < 256) return false;
    if (!texLoader.getFileRawData(base + ".vvd", vvdData) || vvdData.size() < 64) return false;

    const char* extList[] = { ".dx90.vtx", ".vtx", ".dx80.vtx", ".sw.vtx" };
    bool gotVtx = false;
    for (const char* ext : extList) {
        if (texLoader.getFileRawData(base + ext, vtxData)) { gotVtx = true; break; }
    }
    if (!gotVtx) return false;

    int32_t mdlMagic = ri(mdlData.data(), mdlData.size(), 0);
    if (mdlMagic != 0x54534449) return false;
    int32_t vvdMagic = ri(vvdData.data(), vvdData.size(), 0);
    if (vvdMagic != 0x56534449) return false;

    modelName = readStr(mdlData.data(), mdlData.size(), 12, 64);
    hullMin = rv(mdlData.data(), mdlData.size(), MDL_HDR_HULLMIN_OFF);
    hullMax = rv(mdlData.data(), mdlData.size(), MDL_HDR_HULLMAX_OFF);

    if (!parseMDL(mdlData, texLoader)) return false;

    std::vector<MDLVertex> allVerts;
    if (!parseVVD(vvdData, allVerts, 0)) return false;
    (void)enableBindPoseSkinning;

    if (!parseVTX(vtxData, mdlData, allVerts, texLoader)) return false;
    bindMeshVertices.reserve(meshes.size()); for(const auto& me:meshes) bindMeshVertices.push_back(me.vertices);
    animated=!bones.empty();

                                                                              
    if (!meshes.empty()) {
        Vec3 mn{1e30f,1e30f,1e30f}, mx{-1e30f,-1e30f,-1e30f};
        bool any=false;
        for (const auto& mesh:meshes) for (const auto& v:mesh.vertices) {
            mn.x=std::min(mn.x,v.position.x); mn.y=std::min(mn.y,v.position.y); mn.z=std::min(mn.z,v.position.z);
            mx.x=std::max(mx.x,v.position.x); mx.y=std::max(mx.y,v.position.y); mx.z=std::max(mx.z,v.position.z); any=true;
        }
        if(any){ renderBoundsMin=mn; renderBoundsMax=mx; hasRenderBounds=true; }
    }

    loaded = true;
    if (animated) { displayList=0; buildBindSkeletonPose(); if(!sequences.empty()) playSequence(sequences.front().label); }
    else {
                                           
    displayList = glGenLists(1);
    glNewList(displayList, GL_COMPILE);
    for (const auto& mesh : meshes) {
        const bool alphaMesh = mesh.glTexture != 0 && texLoader.textureHasAlpha(mesh.glTexture);
        if (mesh.glTexture) glBindTexture(GL_TEXTURE_2D, mesh.glTexture);
        if (alphaMesh) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_ALPHA_TEST);
            glAlphaFunc(GL_GREATER, 0.01f);
        } else {
            glDisable(GL_BLEND);
            glDisable(GL_ALPHA_TEST);
        }
        glBegin(GL_TRIANGLES);
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            for (int t = 0; t < 3; ++t) {
                const auto& v = mesh.vertices[mesh.indices[i + t]];
                glTexCoord2f(v.texCoord.x, v.texCoord.y);
                glNormal3f(v.normal.x, v.normal.y, v.normal.z);
                glVertex3f(v.position.x, v.position.y, v.position.z);
            }
        }
        glEnd();
        if (alphaMesh) {
            glDisable(GL_BLEND);
            glDisable(GL_ALPHA_TEST);
        }
    }
    glEndList();
    }
    return true;
}

bool MDL_L::parseMDL(const std::vector<char>& data, Texture_L& texLoader) {
    const char* d = data.data();
    size_t sz = data.size();

    int numCd = ri(d, sz, MDL_HDR_NUMCDTEX_OFF);
    int cdIdx = ri(d, sz, MDL_HDR_CDTEXIDX_OFF);

    for (int i = 0; i < numCd && i < 32; ++i) {
        int strOffset = ri(d, sz, cdIdx + i * 4);
        if (strOffset > 0 && (size_t)strOffset < sz) {
            std::string dir = readStr(d, sz, strOffset, 256);
            std::transform(dir.begin(), dir.end(), dir.begin(), ::tolower);
            std::replace(dir.begin(), dir.end(), '\\', '/');
            if (!dir.empty() && dir.back() != '/') dir += '/';
            textureDirs.push_back(dir);
        }
    }
    if (textureDirs.empty()) textureDirs.push_back("");

    const int nb=std::min(128,std::max(0,ri(d,sz,MDL_HDR_NUMBONES_OFF)));
    const int bi=ri(d,sz,MDL_HDR_BONEIDX_OFF);
    if(nb>0 && bi>0 && (size_t)bi+(size_t)nb*MDL_BONE_SIZE<=sz){
        bones.resize(nb);
        for(int i=0;i<nb;++i){ size_t o=(size_t)bi+(size_t)i*MDL_BONE_SIZE; bones[i].name=readStr(d,sz,o,64); bones[i].parent=ri(d,sz,o+MDL_BONE_PARENT_OFF); bones[i].pos=rv(d,sz,o+32); for(int q=0;q<4;++q) bones[i].quat[q]=rf(d,sz,o+44+q*4); if(std::fabs(bones[i].quat[3])<1e-6f) bones[i].quat[3]=1.0f; for(int j=0;j<12;++j) bones[i].poseToBone[j]=rf(d,sz,o+MDL_BONE_POSETObONE_OFF+j*4); }
    }
    const int numLocalAnim=std::min(4096,std::max(0,ri(d,sz,MDL_HDR_NUMLOCALANIM_OFF)));
    const int localAnimIdx=ri(d,sz,MDL_HDR_LOCALANIMIDX_OFF);
    const int ns=std::min(2048,std::max(0,ri(d,sz,188)));
    const int si=ri(d,sz,192);
    if(ns>0 && si>0 && (size_t)si+(size_t)ns*212<=sz){
        sequences.reserve(ns);
        for(int i=0;i<ns;++i){
            size_t o=(size_t)si+(size_t)i*212;
            MDLSequenceInfo q;
            int lo=ri(d,sz,o+4), ao=ri(d,sz,o+8);
            if(lo>0 && (size_t)o+(size_t)lo<sz) q.label=readStr(d,sz,o+lo,128);
            if(ao>0 && (size_t)o+(size_t)ao<sz) q.activity=readStr(d,sz,o+ao,128);
            q.flags=ri(d,sz,o+12);
            const int numBlends=std::max(1,ri(d,sz,o+MDL_SEQ_NUMBLENDS_OFF));
            const int blendIdx=ri(d,sz,o+MDL_SEQ_ANIMINDEXIDX_OFF);
            int groups0=std::max(1,ri(d,sz,o+MDL_SEQ_GROUPSIZE0_OFF));
            int groups1=std::max(1,ri(d,sz,o+MDL_SEQ_GROUPSIZE1_OFF));
            (void)groups0; (void)groups1;
            if(blendIdx>0 && (size_t)o+(size_t)blendIdx+2<=sz){
                int animLocal=(int)(int16_t)rs(d,sz,o+(size_t)blendIdx);
                if(animLocal>=0 && animLocal<numLocalAnim && localAnimIdx>0 && (size_t)localAnimIdx+(size_t)(animLocal+1)*MDL_ANIMDESC_SIZE<=sz){
                    size_t ao2=(size_t)localAnimIdx+(size_t)animLocal*MDL_ANIMDESC_SIZE;
                    q.animIndex=animLocal;
                    q.fps=rf(d,sz,ao2+8);
                    q.animFlags=ri(d,sz,ao2+12);
                    q.numFrames=std::max(1,ri(d,sz,ao2+16));
                }
            }
            if(q.numFrames<=1) q.numFrames=1;
            if(q.fps<1 || q.fps>240) q.fps=30;
            if(numBlends<=0) q.numFrames=1;
            sequences.push_back(std::move(q));
        }
    }

    int numTex = ri(d, sz, MDL_HDR_NUMTEXTURES_OFF);
    int texIdx = ri(d, sz, MDL_HDR_TEXTUREIDX_OFF);

    for (int i = 0; i < numTex && i < 128; ++i) {
        size_t texAddr = texIdx + i * MDL_TEXTURE_SIZE;
        if (texAddr + 4 > sz) break;
        int nameOff = ri(d, sz, texAddr);
        if (nameOff > 0) {
            std::string name = readStr(d, sz, texAddr + nameOff, 128);
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            textureNames.push_back(name);
        }
        else {
            textureNames.push_back("error");
        }
    }
    return true;
}

bool MDL_L::parseVVD(const std::vector<char>& data, std::vector<MDLVertex>& outVerts, int maxVerts) {
    const char* d=data.data();
    const size_t sz=data.size();
    const int vertStart=ri(d,sz,VVD_HDR_VERTSTART);
    if(vertStart<=0 || (size_t)vertStart>=sz) return false;
    int lod0=ri(d,sz,VVD_HDR_LODVERTS);
    const int physical=(int)((sz-(size_t)vertStart)/VVD_VERT_SIZE);
    if(physical<=0) return false;
    if(lod0<=0 || lod0>physical) lod0=physical;
    const int desired=maxVerts>0?std::min(lod0,maxVerts):lod0;

    auto readVertex=[&](int idx,MDLVertex& v)->bool{
        if(idx<0) return false;
        const size_t o=(size_t)vertStart+(size_t)idx*VVD_VERT_SIZE;
        if(o+VVD_VERT_SIZE>sz) return false;
        v.boneWeight[0]=rf(d,sz,o+0);
        v.boneWeight[1]=rf(d,sz,o+4);
        v.boneWeight[2]=rf(d,sz,o+8);
        v.bone[0]=(unsigned char)d[o+12];
        v.bone[1]=(unsigned char)d[o+13];
        v.bone[2]=(unsigned char)d[o+14];
        v.boneCount=(unsigned char)std::min(3,std::max(0,(int)(unsigned char)d[o+15]));
        v.position=rv(d,sz,o+VVD_POS_OFF);
        v.normal=rv(d,sz,o+VVD_NORM_OFF);
        v.texCoord.x=rf(d,sz,o+VVD_UV_OFF);
        v.texCoord.y=rf(d,sz,o+VVD_UV_OFF+4);
        if(v.boneCount==0){
            v.boneCount=1;
            v.bone[0]=0;
            v.boneWeight[0]=1.0f;
        }
        return true;
    };

    outVerts.clear();
    outVerts.reserve(desired);

    const int numFixups=std::max(0,ri(d,sz,VVD_HDR_FIXUPS));
    const int fixupStart=ri(d,sz,VVD_HDR_FIXSTART);
    if(numFixups>0 && fixupStart>0 && (size_t)fixupStart+(size_t)numFixups*12<=sz){
        for(int i=0;i<numFixups && (int)outVerts.size()<desired;++i){
            const size_t o=(size_t)fixupStart+(size_t)i*12;
            const int lod=ri(d,sz,o+0);
            const int source=ri(d,sz,o+4);
            const int count=ri(d,sz,o+8);
            if(lod<0 || source<0 || count<=0 || source>=lod0) continue;
            const int end=std::min(lod0,source+count);
            for(int idx=source;idx<end && (int)outVerts.size()<desired;++idx){
                MDLVertex v;
                if(!readVertex(idx,v)) return false;
                outVerts.push_back(v);
            }
        }
        if((int)outVerts.size()==desired) return true;
        outVerts.clear();
        outVerts.reserve(desired);
    }

    for(int idx=0;idx<desired;++idx){
        MDLVertex v;
        if(!readVertex(idx,v)) break;
        outVerts.push_back(v);
    }
    return !outVerts.empty();
}

static void mdlTransformPoint(const float m[12], const Vec3& p, Vec3& out) {
    out.x=m[0]*p.x+m[1]*p.y+m[2]*p.z+m[3];
    out.y=m[4]*p.x+m[5]*p.y+m[6]*p.z+m[7];
    out.z=m[8]*p.x+m[9]*p.y+m[10]*p.z+m[11];
}

static bool mdlInvert34(const float m[12], float out[12]) {
    const float a00=m[0],a01=m[1],a02=m[2], a10=m[4],a11=m[5],a12=m[6], a20=m[8],a21=m[9],a22=m[10];
    const float c00=a11*a22-a12*a21, c01=a02*a21-a01*a22, c02=a01*a12-a02*a11;
    const float det=a00*c00+a10*c01+a20*c02;
    if(std::fabs(det)<1e-8f) return false;
    const float id=1.0f/det;
    out[0]=c00*id; out[1]=c01*id; out[2]=c02*id;
    out[4]=(a12*a20-a10*a22)*id; out[5]=(a00*a22-a02*a20)*id; out[6]=(a02*a10-a00*a12)*id;
    out[8]=(a10*a21-a11*a20)*id; out[9]=(a01*a20-a00*a21)*id; out[10]=(a00*a11-a01*a10)*id;
    out[3]=-(out[0]*m[3]+out[1]*m[7]+out[2]*m[11]);
    out[7]=-(out[4]*m[3]+out[5]*m[7]+out[6]*m[11]);
    out[11]=-(out[8]*m[3]+out[9]*m[7]+out[10]*m[11]);
    return true;
}

void MDL_L::skinVerticesToBindPose(const std::vector<char>& mdlData, std::vector<MDLVertex>& verts) {
    const char* d=mdlData.data(); const size_t sz=mdlData.size();
    const int count=ri(d,sz,MDL_HDR_NUMBONES_OFF), index=ri(d,sz,MDL_HDR_BONEIDX_OFF);
    if(count<=0 || index<=0 || (size_t)index+(size_t)count*MDL_BONE_SIZE>sz) return;
    const int n=std::min(count,128);
    float inv[128][12]; bool valid[128]{};
    for(int i=0;i<n;++i){
        const size_t o=(size_t)index+(size_t)i*MDL_BONE_SIZE;
        float poseToBone[12]; for(int j=0;j<12;++j) poseToBone[j]=rf(d,sz,o+MDL_BONE_POSETObONE_OFF+(size_t)j*4);
        valid[i]=mdlInvert34(poseToBone,inv[i]);
    }
    for(auto& v:verts){
        if(v.boneCount==0) continue;
        Vec3 p{0,0,0}, nrm{0,0,0}; float total=0.0f;
        for(int j=0;j<std::min(3,(int)v.boneCount);++j){
            const int b=(int)v.bone[j]; const float w=v.boneWeight[j];
            if(b<0||b>=n||!valid[b]||w<=0.0001f) continue;
            Vec3 tp,tn; mdlTransformPoint(inv[b],v.position,tp);
            tn={inv[b][0]*v.normal.x+inv[b][1]*v.normal.y+inv[b][2]*v.normal.z,
                inv[b][4]*v.normal.x+inv[b][5]*v.normal.y+inv[b][6]*v.normal.z,
                inv[b][8]*v.normal.x+inv[b][9]*v.normal.y+inv[b][10]*v.normal.z};
            p = p + tp*w; nrm = nrm + tn*w; total += w;
        }
        if(total>0.0001f){ v.position=p*(1.0f/total); float nl=nrm.length(); if(nl>0.0001f) v.normal=nrm*(1.0f/nl); }
    }
}

bool MDL_L::parseVTX(const std::vector<char>& vtxData, const std::vector<char>& mdlData, const std::vector<MDLVertex>& vvdVerts, Texture_L& texLoader) {
    const char* v = vtxData.data(); size_t vs = vtxData.size();
    const char* m = mdlData.data(); size_t ms = mdlData.size();

    int vtxNumBodyParts = std::min(std::max(0,ri(v, vs, VTX_HDR_NUMBODY)), std::max(0,ri(m, ms, MDL_HDR_NUMBODYPARTS_OFF)));
    int vtxBodyPartOff = ri(v, vs, VTX_HDR_BODYOFF);
    int mdlBodyPartIdx = ri(m, ms, MDL_HDR_BODYPARTIDX_OFF);

    for (int bp = 0; bp < vtxNumBodyParts && bp < 32; ++bp) {
        size_t vtxBpAddr = vtxBodyPartOff + bp * VTX_BODYPART_SIZE;
        size_t mdlBpAddr = mdlBodyPartIdx + bp * MDL_BODYPART_SIZE;
        if (vtxBpAddr + VTX_BODYPART_SIZE > vs || mdlBpAddr + MDL_BODYPART_SIZE > ms) break;

        int numModels = std::min(std::max(0,ri(v, vs, vtxBpAddr)), std::max(0,ri(m, ms, mdlBpAddr + MDL_BP_NUMMODELS)));
        int vtxModelOff = ri(v, vs, vtxBpAddr + 4);
        int mdlModelIdx = ri(m, ms, mdlBpAddr + MDL_BP_MODELIDX);

        for (int mo = 0; mo < numModels && mo < 1; ++mo) {
            size_t vtxModAddr = vtxBpAddr + vtxModelOff + mo * VTX_MODEL_SIZE;
            size_t mdlModAddr = mdlBpAddr + mdlModelIdx + mo * MDL_MODEL_SIZE;
            if (vtxModAddr + VTX_MODEL_SIZE > vs || mdlModAddr + MDL_MODEL_SIZE > ms) break;

            int vtxNumLODs = std::max(0,ri(v, vs, vtxModAddr));
            int vtxLodOff = ri(v, vs, vtxModAddr + 4);
            int mdlNumMeshes = ri(m, ms, mdlModAddr + MDL_MOD_NUMMESH);
            int mdlMeshIdx = ri(m, ms, mdlModAddr + MDL_MOD_MESHIDX);
            int mdlVertexIndex = ri(m, ms, mdlModAddr + MDL_MOD_VERTIDX);

            if (vtxNumLODs <= 0) continue;
            size_t vtxLodAddr = vtxModAddr + vtxLodOff;
            if (vtxLodAddr + VTX_LOD_SIZE > vs) continue;

            int numMeshes = std::min(std::max(0,ri(v, vs, vtxLodAddr)), std::max(0,mdlNumMeshes));
            int vtxMeshOff = ri(v, vs, vtxLodAddr + 4);

            for (int me = 0; me < numMeshes && me < 128; ++me) {
                size_t vtxMeshAddr = vtxLodAddr + vtxMeshOff + me * VTX_MESH_SIZE;
                size_t mdlMeshAddr = mdlModAddr + mdlMeshIdx + me * MDL_MESH_SIZE;
                if (vtxMeshAddr + VTX_MESH_SIZE > vs || mdlMeshAddr + MDL_MESH_SIZE > ms) break;

                int vtxNumStripGroups = std::max(0,ri(v, vs, vtxMeshAddr));
                int vtxStripGroupOff = ri(v, vs, vtxMeshAddr + 4);
                int mdlMaterial = ri(m, ms, mdlMeshAddr + MDL_MESH_MATERIAL);
                int mdlMeshVertOff = ri(m, ms, mdlMeshAddr + MDL_MESH_VERTOFF);
                if (mdlMeshVertOff < 0 || mdlVertexIndex < 0) continue;

                unsigned int glTex = 0;
                if (mdlMaterial >= 0 && mdlMaterial < (int)textureNames.size()) {
                    for (const auto& dir : textureDirs) {
                        glTex = texLoader.getMaterial(dir + textureNames[mdlMaterial]);
                        if (glTex != 0) break;
                    }
                    if (glTex == 0) glTex = texLoader.getMaterial(textureNames[mdlMaterial]);
                }

                MDLRenderMesh renderMesh;
                renderMesh.glTexture = glTex;

                for (int sg = 0; sg < vtxNumStripGroups && sg < 64; ++sg) {
                    size_t sgAddr = vtxMeshAddr + vtxStripGroupOff + sg * VTX_STRIPGRP_SIZE;
                    if (sgAddr + VTX_STRIPGRP_SIZE > vs) break;

                    int sgNumVerts = std::max(0,ri(v, vs, sgAddr + VTX_SG_NUMVERTS));
                    int sgVertOff = ri(v, vs, sgAddr + VTX_SG_VERTOFF);
                    int sgNumIdx = std::max(0,ri(v, vs, sgAddr + VTX_SG_NUMIDX));
                    int sgIdxOff = ri(v, vs, sgAddr + VTX_SG_IDXOFF);
                    int sgNumStrips = std::max(0,ri(v, vs, sgAddr + VTX_SG_NUMSTRIPS));
                    int sgStripOff = ri(v, vs, sgAddr + VTX_SG_STRIPOFF);

                    if (sgNumVerts <= 0 || sgNumIdx <= 0) continue;

                    size_t vertsBase = sgAddr + sgVertOff;
                    size_t idxBase = sgAddr + sgIdxOff;

                    for (int st = 0; st < sgNumStrips && st < 256; ++st) {
                        size_t stripAddr = sgAddr + sgStripOff + st * VTX_STRIP_SIZE;
                        if (stripAddr + VTX_STRIP_SIZE > vs) break;

                        int stripNumIdx = ri(v, vs, stripAddr + VTX_STRIP_NUMIDX);
                        int stripIdxOff = ri(v, vs, stripAddr + VTX_STRIP_IDXOFF);
                        uint8_t stripFlags = (stripAddr + VTX_STRIP_FLAGS < vs) ? (uint8_t)v[stripAddr + VTX_STRIP_FLAGS] : 0;

                        if (stripNumIdx <= 0) continue;

                        if ((stripFlags & STRIP_IS_TRILIST) || stripFlags == 0) {
                            for (int idx = 0; idx + 2 < stripNumIdx; idx += 3) {
                                for (int t = 0; t < 3; ++t) {
                                    size_t idxAddr = idxBase + ((size_t)stripIdxOff + idx + t) * 2;
                                    if (idxAddr + 2 > vs) goto nextStrip;
                                    uint16_t sgVertIdx = rs(v, vs, idxAddr);
                                    if (sgVertIdx >= sgNumVerts) goto nextStrip;

                                    size_t vtxVertAddr = vertsBase + (size_t)sgVertIdx * VTX_VERT_SIZE;
                                    if (vtxVertAddr + VTX_VERT_SIZE > vs) goto nextStrip;

                                    uint16_t origVertID = rs(v, vs, vtxVertAddr + VTX_VERT_ORIGID);
                                    if (mdlVertexIndex < 0 || mdlMeshVertOff < 0) goto nextStrip;
                                    int vvdIdx = mdlMeshVertOff + origVertID;
                                    if (vvdIdx < 0 || vvdIdx >= (int)vvdVerts.size()) goto nextStrip;

                                    renderMesh.vertices.push_back(vvdVerts[vvdIdx]);
                                    renderMesh.indices.push_back((unsigned int)(renderMesh.vertices.size() - 1));
                                }
                            }
                        }
                        else if (stripFlags & STRIP_IS_TRISTRIP) {
                            for (int idx = 0; idx + 2 < stripNumIdx; ++idx) {
                                size_t a0 = idxBase + ((size_t)stripIdxOff + idx) * 2;
                                size_t a1 = idxBase + ((size_t)stripIdxOff + idx + 1) * 2;
                                size_t a2 = idxBase + ((size_t)stripIdxOff + idx + 2) * 2;
                                if (a2 + 2 > vs) break;

                                uint16_t i0 = rs(v, vs, a0), i1 = rs(v, vs, a1), i2 = rs(v, vs, a2);
                                if (idx % 2 == 1) std::swap(i1, i2);

                                uint16_t sgIdxArr[3] = { i0, i1, i2 };
                                for (int t = 0; t < 3; ++t) {
                                    if (sgIdxArr[t] >= sgNumVerts) goto nextStrip;
                                    size_t vtxVertAddr = vertsBase + (size_t)sgIdxArr[t] * VTX_VERT_SIZE;
                                    if (vtxVertAddr + VTX_VERT_SIZE > vs) goto nextStrip;

                                    uint16_t origVertID = rs(v, vs, vtxVertAddr + VTX_VERT_ORIGID);
                                    if (mdlVertexIndex < 0 || mdlMeshVertOff < 0) goto nextStrip;
                                    int vvdIdx = mdlMeshVertOff + origVertID;
                                    if (vvdIdx < 0 || vvdIdx >= (int)vvdVerts.size()) goto nextStrip;

                                    renderMesh.vertices.push_back(vvdVerts[vvdIdx]);
                                    renderMesh.indices.push_back((unsigned int)(renderMesh.vertices.size() - 1));
                                }
                            }
                        }
                    nextStrip:;
                    }
                }
                if (!renderMesh.vertices.empty()) meshes.push_back(std::move(renderMesh));
            }
        }
    }
    return true;
}

static std::array<float,12> mdlMul34(const std::array<float,12>& a,const std::array<float,12>& b){
    std::array<float,12> c{};
    c[0]=a[0]*b[0]+a[1]*b[4]+a[2]*b[8]; c[1]=a[0]*b[1]+a[1]*b[5]+a[2]*b[9]; c[2]=a[0]*b[2]+a[1]*b[6]+a[2]*b[10]; c[3]=a[0]*b[3]+a[1]*b[7]+a[2]*b[11]+a[3];
    c[4]=a[4]*b[0]+a[5]*b[4]+a[6]*b[8]; c[5]=a[4]*b[1]+a[5]*b[5]+a[6]*b[9]; c[6]=a[4]*b[2]+a[5]*b[6]+a[6]*b[10]; c[7]=a[4]*b[3]+a[5]*b[7]+a[6]*b[11]+a[7];
    c[8]=a[8]*b[0]+a[9]*b[4]+a[10]*b[8]; c[9]=a[8]*b[1]+a[9]*b[5]+a[10]*b[9]; c[10]=a[8]*b[2]+a[9]*b[6]+a[10]*b[10]; c[11]=a[8]*b[3]+a[9]*b[7]+a[10]*b[11]+a[11]; return c;
}
static void mdlQuatMat(const float q0[4],const Vec3& p,std::array<float,12>& m){float x=q0[0],y=q0[1],z=q0[2],w=q0[3];float n=std::sqrt(x*x+y*y+z*z+w*w);if(n<1e-8f){x=y=z=0;w=1;}else{x/=n;y/=n;z/=n;w/=n;}m={1-2*y*y-2*z*z,2*x*y-2*z*w,2*x*z+2*y*w,p.x,2*x*y+2*z*w,1-2*x*x-2*z*z,2*y*z-2*x*w,p.y,2*x*z-2*y*w,2*y*z+2*x*w,1-2*x*x-2*y*y,p.z};}
void MDL_L::buildBindSkeletonPose(){boneMatrices.resize(bones.size());for(size_t i=0;i<bones.size();++i){std::array<float,12> l;mdlQuatMat(bones[i].quat,bones[i].pos,l);if(bones[i].parent>=0&&bones[i].parent<(int)i)boneMatrices[i]=mdlMul34(boneMatrices[(size_t)bones[i].parent],l);else boneMatrices[i]=l;}}
void MDL_L::applyProceduralViewmodelPose(){std::string n=m_sequenceName;std::transform(n.begin(),n.end(),n.begin(),[](unsigned char c){return (char)std::tolower(c);});float t=m_sequenceCycle,w=std::sin(t*6.2831853f);float p=0,y=0,r=0;if(n.find("reload")!=std::string::npos){p=-0.20f*std::sin(std::min(1.0f,t)*3.14159f);y=0.18f*std::sin(std::min(1.0f,t)*3.14159f);r=.10f*w;}else if(n.find("draw")!=std::string::npos||n.find("deploy")!=std::string::npos){p=.16f*(1-t);r=-.10f*(1-t);}else if(n.find("attack")!=std::string::npos||n.find("fire")!=std::string::npos||n.find("shoot")!=std::string::npos){p=-.11f*std::sin(std::min(1.0f,t)*3.14159f);r=.025f*w;}else{p=.007f*w;r=.005f*w;}for(size_t i=0;i<bones.size();++i){std::string b=bones[i].name;std::transform(b.begin(),b.end(),b.begin(),[](unsigned char c){return (char)std::tolower(c);});if(b.find("hand")!=std::string::npos||b.find("arm")!=std::string::npos||b.find("weapon")!=std::string::npos){std::array<float,12> rot; float iq[4]={0,0,0,1}; mdlQuatMat(iq,Vec3{0,0,0},rot);float cx=std::cos(p),sx=std::sin(p),cy=std::cos(y),sy=std::sin(y),cz=std::cos(r),sz=std::sin(r);rot={cy*cz,-cy*sz,sy,0,sx*sy*cz+cx*sz,-sx*sy*sz+cx*cz,-sx*cy,0,-cx*sy*cz+sx*sz,cx*sy*sz+sx*cz,cx*cy,0};boneMatrices[i]=mdlMul34(boneMatrices[i],rot);}}}
void MDL_L::skinDynamicVertices(){if(bindMeshVertices.size()!=meshes.size())return;for(size_t mi=0;mi<meshes.size();++mi){meshes[mi].vertices=bindMeshVertices[mi];for(auto&v:meshes[mi].vertices){Vec3 p{0,0,0},n{0,0,0};float total=0;for(int j=0;j<std::min(3,(int)v.boneCount);++j){int b=v.bone[j];float w=v.boneWeight[j];if(b<0||b>=(int)bones.size()||w<=.0001f)continue;std::array<float,12> ptb{};for(int k=0;k<12;++k)ptb[k]=bones[b].poseToBone[k];auto sm=mdlMul34(boneMatrices[(size_t)b],ptb);Vec3 tp,tn;mdlTransformPoint(sm.data(),v.position,tp);tn={sm[0]*v.normal.x+sm[1]*v.normal.y+sm[2]*v.normal.z,sm[4]*v.normal.x+sm[5]*v.normal.y+sm[6]*v.normal.z,sm[8]*v.normal.x+sm[9]*v.normal.y+sm[10]*v.normal.z};p=p+tp*w;n=n+tn*w;total+=w;}if(total>.0001f){v.position=p*(1.0f/total);float ln=n.length();if(ln>.0001f)v.normal=n*(1.0f/ln);}}}}
bool MDL_L::playSequence(const std::string& name){std::string q=name;std::transform(q.begin(),q.end(),q.begin(),[](unsigned char c){return (char)std::tolower(c);});for(size_t i=0;i<sequences.size();++i){std::string a=sequences[i].label,b=sequences[i].activity;std::transform(a.begin(),a.end(),a.begin(),[](unsigned char c){return (char)std::tolower(c);});std::transform(b.begin(),b.end(),b.begin(),[](unsigned char c){return (char)std::tolower(c);});if(a==q||b==q||a.find(q)!=std::string::npos||b.find(q)!=std::string::npos){m_sequenceIndex=(int)i;m_sequenceCycle=0;m_sequenceTime=0;m_sequenceFinished=false;m_sequenceName=sequences[i].label.empty()?sequences[i].activity:sequences[i].label;m_sequenceLoop=(sequences[i].flags&1)!=0;return true;}}return false;}
bool MDL_L::playActivity(const std::string& a){return playSequence(a);}bool MDL_L::playAnimationHint(const std::string& h){if(playSequence(h))return true;std::string q=h;std::transform(q.begin(),q.end(),q.begin(),[](unsigned char c){return (char)std::tolower(c);});if(q.find("reload")!=std::string::npos&&playSequence("reload"))return true;if((q.find("attack")!=std::string::npos||q.find("primary")!=std::string::npos)&&playSequence("attack"))return true;if(q.find("draw")!=std::string::npos&&playSequence("draw"))return true;return q.find("idle")!=std::string::npos&&playSequence("idle");}
static std::string mdlLowerName(std::string s){std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return (char)std::tolower(c);});return s;}
static std::array<float,12> mdlIdleRot(float p,float y,float r){const float cx=std::cos(p),sx=std::sin(p),cy=std::cos(y),sy=std::sin(y),cz=std::cos(r),sz=std::sin(r);return {cy*cz,-cy*sz,sy,0,sx*sy*cz+cx*sz,-sx*sy*sz+cx*cz,-sx*cy,0,-cx*sy*cz+sx*sz,cx*sy*sz+sx*cz,cx*cy,0};}
static bool mdlNameHas(const std::string& n,const char* a){return n.find(a)!=std::string::npos;}
void MDL_L::updateAnimation(float dt,bool npcSkeletalIdle){
    if(!loaded||!animated)return;
    if(!sequences.empty()){
        if(m_sequenceIndex<0){
            if(!playAnimationHint("idle"))m_sequenceIndex=0;
        }
        if(m_sequenceIndex<0)m_sequenceIndex=0;
        if(m_sequenceIndex>=(int)sequences.size())m_sequenceIndex=(int)sequences.size()-1;
        auto &q=sequences[(size_t)m_sequenceIndex];
        float fps=std::max(1.0f,q.fps);
        float dur=std::max(1.0f/fps,(float)std::max(1,q.numFrames-1)/fps);
        m_sequenceTime+=std::max(0.0f,dt);
        m_sequenceCycle=dur>0?m_sequenceTime/dur:0;
        if(m_sequenceCycle>=1.0f){
            if(m_sequenceLoop){
                m_sequenceCycle=std::fmod(m_sequenceCycle,1.0f);
                m_sequenceTime=m_sequenceCycle*dur;
            }else{
                m_sequenceCycle=1.0f;
                m_sequenceFinished=true;
                if(mdlLowerName(m_sequenceName).find("idle")==std::string::npos){
                    if(playAnimationHint("idle")){ }
                    else m_sequenceTime=0.0f;
                }
            }
        }
    }else if(!npcSkeletalIdle)return;
    const float t=m_sequenceCycle*6.283185307f;
    for(size_t i=0;i<bones.size();++i){
        std::array<float,12> local;
        mdlQuatMat(bones[i].quat,bones[i].pos,local);
        if(npcSkeletalIdle){
            const std::string n=mdlLowerName(bones[i].name);
            float p=0.0f,y=0.0f,r=0.0f;
            const float phase=(float)(i%11)*0.31f;
            if(mdlNameHas(n,"pelvis")||mdlNameHas(n,"hip")){p=std::sin(t+phase)*0.012f;y=std::sin(t*0.5f+phase)*0.008f;}
            else if(mdlNameHas(n,"spine")||mdlNameHas(n,"chest")){p=std::sin(t+phase)*0.018f;y=std::sin(t*0.5f+phase)*0.012f;}
            else if(mdlNameHas(n,"neck")||mdlNameHas(n,"head")){p=std::sin(t+phase)*0.010f;y=std::sin(t*0.5f+phase)*0.016f;}
            else if(mdlNameHas(n,"upperarm")||mdlNameHas(n,"bicep")||mdlNameHas(n,"clav")){r=std::sin(t+phase)*0.018f;p=std::sin(t*0.5f+phase)*0.010f;}
            else if(mdlNameHas(n,"forearm")||mdlNameHas(n,"ulna")||mdlNameHas(n,"radius")){r=std::sin(t+phase)*0.012f;}
            else if(mdlNameHas(n,"thigh")||mdlNameHas(n,"upleg")||mdlNameHas(n,"upper_leg")){p=std::sin(t+phase)*0.012f;}
            else if(mdlNameHas(n,"calf")||mdlNameHas(n,"lowerleg")||mdlNameHas(n,"shin")){p=std::sin(t+phase+1.0f)*0.010f;}
            else if(mdlNameHas(n,"foot")||mdlNameHas(n,"ankle")){p=std::sin(t+phase+1.4f)*0.008f;}
            if(std::fabs(p)+std::fabs(y)+std::fabs(r)>0.00001f)local=mdlMul34(local,mdlIdleRot(p,y,r));
        }
        if(bones[i].parent>=0&&bones[i].parent<(int)i)boneMatrices[i]=mdlMul34(boneMatrices[(size_t)bones[i].parent],local);else boneMatrices[i]=local;
    }
    if(!npcSkeletalIdle)applyProceduralViewmodelPose();
    skinDynamicVertices();
}
void MDL_L::buildBindPoseMatrices(std::vector<std::array<float,12>>& out) const{
    out.resize(bones.size());
    for(size_t i=0;i<bones.size();++i){
        std::array<float,12> local;
        mdlQuatMat(bones[i].quat,bones[i].pos,local);
        if(bones[i].parent>=0&&bones[i].parent<(int)i)out[i]=mdlMul34(out[(size_t)bones[i].parent],local);else out[i]=local;
    }
}
void MDL_L::renderSkinned(const std::vector<std::array<float,12>>& matrices) const{
    if(!loaded||matrices.size()!=bones.size()||bindMeshVertices.size()!=meshes.size())return;
    for(size_t mi=0;mi<meshes.size();++mi){
        const auto& m=meshes[mi];
        if(m.glTexture)glBindTexture(GL_TEXTURE_2D,m.glTexture);
        glBegin(GL_TRIANGLES);
        for(size_t vi=0;vi+2<m.indices.size();vi+=3){
            for(int t=0;t<3;++t){
                const MDLVertex& v=bindMeshVertices[mi][m.indices[vi+(size_t)t]];
                Vec3 p{0,0,0},n{0,0,0};
                float total=0.0f;
                for(int j=0;j<std::min(3,(int)v.boneCount);++j){
                    int b=(int)v.bone[j];
                    float w=v.boneWeight[j];
                    if(b<0||b>=(int)matrices.size()||w<=0.0001f)continue;
                    std::array<float,12> poseToBone{};
                    for(int k=0;k<12;++k)poseToBone[k]=bones[(size_t)b].poseToBone[k];
                    std::array<float,12> skin=mdlMul34(matrices[(size_t)b],poseToBone);
                    Vec3 tp,tn;
                    mdlTransformPoint(skin.data(),v.position,tp);
                    tn={skin[0]*v.normal.x+skin[1]*v.normal.y+skin[2]*v.normal.z,skin[4]*v.normal.x+skin[5]*v.normal.y+skin[6]*v.normal.z,skin[8]*v.normal.x+skin[9]*v.normal.y+skin[10]*v.normal.z};
                    p=p+tp*w;
                    n=n+tn*w;
                    total+=w;
                }
                if(total<=0.0001f){p=v.position;n=v.normal;}
                else{p=p*(1.0f/total);float ln=n.length();if(ln>0.0001f)n=n*(1.0f/ln);}
                glTexCoord2f(v.texCoord.x,v.texCoord.y);
                glNormal3f(n.x,n.y,n.z);
                glVertex3f(p.x,p.y,p.z);
            }
        }
        glEnd();
    }
}

static bool mdlRayBounds(const Vec3& o,const Vec3& d,const Vec3& mn,const Vec3& mx,float& t,Vec3& n){
    float enter=0.0f,exit=1e30f;int enterAxis=-1;bool enterPositive=false;
    for(int axis=0;axis<3;++axis){
        const float ro=(&o.x)[axis],rd=(&d.x)[axis],lo=(&mn.x)[axis],hi=(&mx.x)[axis];
        if(std::fabs(rd)<1e-7f){if(ro<lo||ro>hi)return false;continue;}
        float t0=(lo-ro)/rd,t1=(hi-ro)/rd;bool p0=true,p1=false;
        if(t0>t1){std::swap(t0,t1);p0=false;p1=true;}
        if(t0>enter){enter=t0;enterAxis=axis;enterPositive=p0;}
        exit=std::min(exit,t1);if(enter>exit)return false;
    }
    if(exit<0.0f)return false;
    t=enter>0.0f?enter:0.0f;
    if(enterAxis<0){
        float best=1e30f;int axis=0;bool pos=false;
        for(int a=0;a<3;++a){float da=std::fabs((&d.x)[a]);if(da<1e-7f)continue;float dist=std::fabs((((&o.x)[a])<((&mn.x)[a])?((&mn.x)[a])-((&o.x)[a]):((&mx.x)[a])-((&o.x)[a])))/da;if(dist<best){best=dist;axis=a;pos=((&o.x)[a])<((&mn.x)[a]);}}
        enterAxis=axis;enterPositive=pos;
    }
    n={0,0,0};if(enterAxis>=0)(&n.x)[enterAxis]=enterPositive?1.0f:-1.0f;
    return true;
}

bool MDL_L::traceRayLocal(const Vec3& origin,const Vec3& direction,float& outDist,Vec3& outNormal) const{
    Vec3 mn=renderBoundsMin,mx=renderBoundsMax;
    if(!hasRenderBounds||mn.x>mx.x||mn.y>mx.y||mn.z>mx.z){mn=hullMin;mx=hullMax;}
    if(mn.x>mx.x||mn.y>mx.y||mn.z>mx.z)return false;
    return mdlRayBounds(origin,direction,mn,mx,outDist,outNormal);
}

void MDL_L::releaseCPUData(){
    meshes.clear();
    meshes.shrink_to_fit();
    bindMeshVertices.clear();
    bindMeshVertices.shrink_to_fit();
    loaded=false;
    animated=false;
    bones.clear();
    bones.shrink_to_fit();
    sequences.clear();
    sequences.shrink_to_fit();
    boneMatrices.clear();
    boneMatrices.shrink_to_fit();
    m_sequenceIndex=-1;
    m_sequenceCycle=0.0f;
    m_sequenceTime=0.0f;
    m_sequenceName.clear();
    m_sequenceFinished=false;
}

void MDL_L::render(){
    if(!loaded) return;
    if(animated){
        for(const auto&m:meshes){
            const bool alphaMesh = m.glTexture != 0 && false;
            (void)alphaMesh;
            if(m.glTexture) glBindTexture(GL_TEXTURE_2D,m.glTexture);
            glBegin(GL_TRIANGLES);
            for(size_t i=0;i+2<m.indices.size();i+=3){
                for(int t=0;t<3;++t){
                    const auto&v=m.vertices[m.indices[i+t]];
                    glTexCoord2f(v.texCoord.x,v.texCoord.y);
                    glNormal3f(v.normal.x,v.normal.y,v.normal.z);
                    glVertex3f(v.position.x,v.position.y,v.position.z);
                }
            }
            glEnd();
        }
    }else if(displayList) glCallList(displayList);
}
