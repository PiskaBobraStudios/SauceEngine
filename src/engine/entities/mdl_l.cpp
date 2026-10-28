#include <cmath>
#include <array>
#include "mdl_l.h"
#include "texture_l.h"
#include "sound_l.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cctype>
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
static const size_t MDL_HDR_NUMSKINREF_OFF = 220;
static const size_t MDL_HDR_NUMSKINFAMILIES_OFF = 224;
static const size_t MDL_HDR_SKININDEX_OFF = 228;
static const size_t MDL_HDR_NUMINCLUDEMODELS_OFF = 336;
static const size_t MDL_HDR_INCLUDEMODELIDX_OFF = 340;
static const size_t MDL_HDR_NUMANIMBLOCKS_OFF = 352;
static const size_t MDL_HDR_ANIMBLOCKIDX_OFF = 356;
static const size_t MDL_ANIMBLOCK_SIZE = 8;
static const size_t MDL_INCLUDEMODEL_SIZE = 8;
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
static const size_t MDL_BP_BASE = 8;
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

namespace {
thread_local int mdlLoadingDepth = 0;
struct MDLLoadDepthGuard {
    MDLLoadDepthGuard() { ++mdlLoadingDepth; }
    ~MDLLoadDepthGuard() { --mdlLoadingDepth; }
};
static std::string mdlLowerForLoad(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}
}

std::string MDL_L::readStr(const char* b, size_t s, size_t offset, size_t maxLen) {
    std::string result;
    for (size_t i = 0; i < maxLen && offset + i < s; ++i) {
        if (b[offset + i] == '\0') break;
        result += b[offset + i];
    }
    return result;
}

bool MDL_L::load(const std::string& mdlPath, Texture_L& texLoader, bool enableBindPoseSkinning, int bodyValue, int skinValue) {
    MDLLoadDepthGuard loadDepthGuard;
    loaded = false;
    hasRenderBounds = false;
    renderBoundsMin = Vec3{0,0,0};
    renderBoundsMax = Vec3{0,0,0};
    meshes.clear();
    bindMeshVertices.clear();
    bones.clear(); sequences.clear();
    mdlSourceData.clear(); aniSourceData.clear(); animationSources.clear(); mdlBasePath.clear();
    m_sequenceIndex=-1; m_sequenceCycle=0.0f; m_sequenceTime=0.0f; m_sequenceName.clear(); m_sequenceFinished=false;
    m_prevBoneMatrices.clear(); m_poseBlend=1.0f; m_poseSequence=-1;
    m_bodyValue=std::max(0,bodyValue);
    m_skinValue=std::max(0,skinValue);
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
    mdlBasePath = base;

    if (!texLoader.getFileRawData(base + ".mdl", mdlData) || mdlData.size() < 256) return false;
    mdlSourceData = mdlData;


    texLoader.getFileRawData(base + ".ani", aniSourceData);
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


    if (mdlLoadingDepth == 1 && animated && mdlSourceData.size() >= MDL_HDR_INCLUDEMODELIDX_OFF + 4) {
        const char* hdr = mdlSourceData.data();
        const size_t hdrSize = mdlSourceData.size();
        const int includeCount = std::clamp(ri(hdr,hdrSize,MDL_HDR_NUMINCLUDEMODELS_OFF),0,32);
        const int includeIndex = ri(hdr,hdrSize,MDL_HDR_INCLUDEMODELIDX_OFF);
        if (includeCount > 0 && includeIndex > 0 &&
            (size_t)includeIndex + (size_t)includeCount*MDL_INCLUDEMODEL_SIZE <= hdrSize) {
            for (int ii=0; ii<includeCount && animationSources.size()<16; ++ii) {
                const size_t io=(size_t)includeIndex+(size_t)ii*MDL_INCLUDEMODEL_SIZE;
                const int nameRel=ri(hdr,hdrSize,io+4);
                if(nameRel<=0 || io+(size_t)nameRel>=hdrSize) continue;
                std::string includePath=mdlLowerForLoad(readStr(hdr,hdrSize,io+(size_t)nameRel,260));
                if(includePath.empty()) continue;
                if(includePath.size()<4 || includePath.substr(includePath.size()-4)!=".mdl") includePath += ".mdl";
                auto included=std::make_shared<MDL_L>();
                if(!included->load(includePath,texLoader,false) || included->bones.size()!=bones.size()) continue;
                bool sameSkeleton=true;
                for(size_t bi=0;bi<bones.size();++bi) {
                    if(mdlLowerForLoad(bones[bi].name)!=mdlLowerForLoad(included->bones[bi].name)) { sameSkeleton=false; break; }
                }
                if(!sameSkeleton) continue;
                const int sourceIndex=(int)animationSources.size()+1;
                animationSources.push_back(included);
                bool borrowedAny=false;
                for(const auto& incSeq:included->sequences) {
                    if(incSeq.animDescOffset<0 || (incSeq.animDataIndex<=0 && incSeq.sectionIndex<=0)) continue;
                    MDLSequenceInfo borrowed=incSeq;
                    borrowed.sourceModelIndex=sourceIndex;
                    const std::string label=mdlLowerForLoad(borrowed.label);
                    const std::string activity=mdlLowerForLoad(borrowed.activity);
                    auto existing=std::find_if(sequences.begin(),sequences.end(),[&](const MDLSequenceInfo& own){
                        return (!label.empty() && mdlLowerForLoad(own.label)==label) ||
                               (!activity.empty() && mdlLowerForLoad(own.activity)==activity);
                    });
                    if(existing!=sequences.end()) {
                        if(existing->animDescOffset<0 || (existing->animDataIndex<=0 && existing->sectionIndex<=0)) {
                            *existing=std::move(borrowed);
                            borrowedAny=true;
                        }
                    } else if(sequences.size()<8192) {
                        sequences.push_back(std::move(borrowed));
                        borrowedAny=true;
                    }
                }
                if(!borrowedAny) {
                    animationSources.pop_back();
                } else {


                    if(included->displayList) { glDeleteLists(included->displayList,1); included->displayList=0; }
                    included->meshes.clear(); included->bindMeshVertices.clear();
                    included->boneMatrices.clear(); included->textureNames.clear(); included->textureDirs.clear();
                }
            }
        }
    }

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
        for(int i=0;i<nb;++i){
            size_t o=(size_t)bi+(size_t)i*MDL_BONE_SIZE;
            const int nameRel=ri(d,sz,o);
            bones[i].name=(nameRel>0 && (size_t)o+(size_t)nameRel<sz)?readStr(d,sz,o+(size_t)nameRel,64):readStr(d,sz,o,64);
            bones[i].parent=ri(d,sz,o+MDL_BONE_PARENT_OFF);
            bones[i].pos=rv(d,sz,o+32);
            for(int q=0;q<4;++q) bones[i].quat[q]=rf(d,sz,o+44+(size_t)q*4);
            bones[i].rotEuler=rv(d,sz,o+60);
            bones[i].posScale=rv(d,sz,o+72);
            bones[i].rotScale=rv(d,sz,o+84);
            if(std::fabs(bones[i].quat[0])+std::fabs(bones[i].quat[1])+std::fabs(bones[i].quat[2])+std::fabs(bones[i].quat[3])<1e-6f) {
                const Vec3 e=bones[i].rotEuler;
                const float sr=std::sin(e.x*0.5f), cr=std::cos(e.x*0.5f);
                const float sp=std::sin(e.y*0.5f), cp=std::cos(e.y*0.5f);
                const float sy=std::sin(e.z*0.5f), cy=std::cos(e.z*0.5f);
                bones[i].quat[0]=sr*cp*cy-cr*sp*sy; bones[i].quat[1]=cr*sp*cy+sr*cp*sy;
                bones[i].quat[2]=cr*cp*sy-sr*sp*cy; bones[i].quat[3]=cr*cp*cy+sr*sp*sy;
            }
            if(std::fabs(bones[i].quat[0])+std::fabs(bones[i].quat[1])+std::fabs(bones[i].quat[2])+std::fabs(bones[i].quat[3])<1e-6f) bones[i].quat[3]=1.0f;
            if(std::fabs(bones[i].posScale.x)+std::fabs(bones[i].posScale.y)+std::fabs(bones[i].posScale.z)<1e-10f) bones[i].posScale=Vec3{1,1,1};
            if(std::fabs(bones[i].rotScale.x)+std::fabs(bones[i].rotScale.y)+std::fabs(bones[i].rotScale.z)<1e-10f) bones[i].rotScale=Vec3{1,1,1};
            for(int j=0;j<12;++j) bones[i].poseToBone[j]=rf(d,sz,o+MDL_BONE_POSETObONE_OFF+(size_t)j*4);
        }
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
            const int numEvents=std::clamp(ri(d,sz,o+24),0,2048);
            const int eventIndex=ri(d,sz,o+28);
            if(numEvents>0 && eventIndex>0 && (size_t)o+(size_t)eventIndex+(size_t)numEvents*80<=sz) {
                q.events.reserve((size_t)numEvents);
                for(int ei=0;ei<numEvents;++ei) {
                    const size_t eo=o+(size_t)eventIndex+(size_t)ei*80;
                    MDLSequenceEvent ev;
                    ev.cycle=std::clamp(rf(d,sz,eo),0.0f,1.0f);
                    ev.event=ri(d,sz,eo+4); ev.type=ri(d,sz,eo+8);
                    ev.options=readStr(d,sz,eo+12,64);
                    if(!ev.options.empty() || ev.event==4 || ev.event==5) q.events.push_back(std::move(ev));
                }
            }
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
                    q.animDescOffset=(int)ao2;
                    q.fps=rf(d,sz,ao2+8);
                    q.animFlags=ri(d,sz,ao2+12);
                    q.numFrames=std::max(1,ri(d,sz,ao2+16));
                    q.animBlock=ri(d,sz,ao2+52);
                    q.animDataIndex=ri(d,sz,ao2+56);
                    q.sectionIndex=ri(d,sz,ao2+80);
                    q.sectionFrames=ri(d,sz,ao2+84);
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

        const int bodyBase = std::max(1,ri(m,ms,mdlBpAddr + MDL_BP_BASE));
        const int selectedModel = numModels > 0 ? std::clamp((m_bodyValue / bodyBase) % numModels,0,numModels-1) : 0;
        for (int mo = selectedModel; mo < numModels && mo == selectedModel; ++mo) {
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
                const int skinRefCount = std::max(0,ri(m,ms,MDL_HDR_NUMSKINREF_OFF));
                const int skinFamilyCount = std::max(0,ri(m,ms,MDL_HDR_NUMSKINFAMILIES_OFF));
                const int skinTable = ri(m,ms,MDL_HDR_SKININDEX_OFF);
                if (skinRefCount > 0 && skinFamilyCount > 0 && skinTable > 0 && mdlMaterial >= 0 && mdlMaterial < skinRefCount) {
                    const int family = std::clamp(m_skinValue,0,skinFamilyCount-1);
                    const size_t remapOffset = (size_t)skinTable + ((size_t)family * (size_t)skinRefCount + (size_t)mdlMaterial) * 2;
                    if (remapOffset + 2 <= ms) mdlMaterial = (int)(int16_t)rs(m,ms,remapOffset);
                }
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
                                    int vvdIdx = mdlVertexIndex + mdlMeshVertOff + origVertID;
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
                                    int vvdIdx = mdlVertexIndex + mdlMeshVertOff + origVertID;
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

namespace {
static uint16_t mdlReadU16(const char* p) { return (uint16_t)(uint8_t)p[0] | (uint16_t)((uint8_t)p[1] << 8); }
static uint64_t mdlReadU64(const char* p) {
    uint64_t v=0; for(int i=0;i<8;++i) v|=(uint64_t)(uint8_t)p[i]<<(i*8); return v;
}
static float mdlHalfToFloat(uint16_t h) {
    const uint32_t sign=(uint32_t)(h&0x8000u)<<16;
    uint32_t exp=(h>>10)&0x1fu, mant=h&0x03ffu, bits=0;
    if(exp==0) {
        if(mant==0) bits=sign;
        else {
            int e=-14; while((mant&0x0400u)==0){mant<<=1;--e;} mant&=0x03ffu;
            bits=sign|((uint32_t)(e+127)<<23)|(mant<<13);
        }
    } else if(exp==31) bits=sign|0x7f800000u|(mant<<13);
    else bits=sign|((exp+112u)<<23)|(mant<<13);
    float f; std::memcpy(&f,&bits,sizeof(f)); return f;
}
static Vec3 mdlReadVector48(const char* p) {
    return Vec3{mdlHalfToFloat(mdlReadU16(p)),mdlHalfToFloat(mdlReadU16(p+2)),mdlHalfToFloat(mdlReadU16(p+4))};
}
static std::array<float,4> mdlReadQuat48(const char* p) {
    uint16_t x=mdlReadU16(p), y=mdlReadU16(p+2);
    uint16_t zneg=mdlReadU16(p+4); uint16_t z=zneg&0x7fffu; bool neg=(zneg&0x8000u)!=0;
    float qx=((int)x-32768)*(1.0f/32768.0f), qy=((int)y-32768)*(1.0f/32768.0f), qz=((int)z-16384)*(1.0f/16384.0f);
    float qw=std::sqrt(std::max(0.0f,1.0f-qx*qx-qy*qy-qz*qz)); if(neg)qw=-qw;
    return {qx,qy,qz,qw};
}
static std::array<float,4> mdlReadQuat64(const char* p) {
    const uint64_t packed=mdlReadU64(p);
    const uint32_t x=(uint32_t)(packed&0x1fffffu), y=(uint32_t)((packed>>21)&0x1fffffu), z=(uint32_t)((packed>>42)&0x1fffffu);
    const bool neg=(packed>>63)!=0;
    float qx=((int)x-1048576)*(1.0f/1048576.5f), qy=((int)y-1048576)*(1.0f/1048576.5f), qz=((int)z-1048576)*(1.0f/1048576.5f);
    float qw=std::sqrt(std::max(0.0f,1.0f-qx*qx-qy*qy-qz*qz)); if(neg)qw=-qw;
    return {qx,qy,qz,qw};
}
static std::array<float,4> mdlEulerToQuat(const Vec3& e) {

    const float sr=std::sin(e.x*0.5f), cr=std::cos(e.x*0.5f);
    const float sp=std::sin(e.y*0.5f), cp=std::cos(e.y*0.5f);
    const float sy=std::sin(e.z*0.5f), cy=std::cos(e.z*0.5f);
    return {sr*cp*cy-cr*sp*sy, cr*sp*cy+sr*cp*sy,
            cr*cp*sy-sr*sp*cy, cr*cp*cy+sr*sp*sy};
}
static std::array<float,4> mdlQuatMultiply(const std::array<float,4>& a,const std::array<float,4>& b) {
    return {a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1],
            a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0],
            a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3],
            a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2]};
}
static std::array<float,4> mdlQuatSlerp(std::array<float,4> a,std::array<float,4> b,float t) {
    float dot=0; for(int i=0;i<4;++i)dot+=a[i]*b[i];
    if(dot<0){for(float& v:b)v=-v;dot=-dot;}
    dot=std::clamp(dot,-1.0f,1.0f);
    if(dot>0.9995f){std::array<float,4> q{};float n=0;for(int i=0;i<4;++i){q[i]=a[i]+(b[i]-a[i])*t;n+=q[i]*q[i];}n=std::sqrt(n);if(n>1e-8f)for(float& v:q)v/=n;return q;}
    float theta=std::acos(dot), sinTheta=std::sin(theta);
    if(std::fabs(sinTheta)<1e-7f)return a;
    float wa=std::sin((1.0f-t)*theta)/sinTheta, wb=std::sin(t*theta)/sinTheta;
    std::array<float,4> q{};for(int i=0;i<4;++i)q[i]=a[i]*wa+b[i]*wb;return q;
}
}

bool MDL_L::decodeAnimationFrame(const MDLSequenceInfo& seq, int frame,
                                  std::vector<Vec3>& positions,
                                  std::vector<std::array<float,4>>& rotations) const {
    if(seq.animDescOffset<0 || mdlSourceData.empty() || bones.empty() || seq.numFrames<=0) return false;
    if(seq.sectionIndex<=0 && seq.animDataIndex<=0 && seq.animBlock==0) return false;
    const MDL_L* animationSource=this;
    if(seq.sourceModelIndex>0) {
        const size_t sourceIndex=(size_t)(seq.sourceModelIndex-1);
        if(sourceIndex>=animationSources.size() || !animationSources[sourceIndex]) return false;
        animationSource=animationSources[sourceIndex].get();
    }
    const auto& sourceMdlData=animationSource->mdlSourceData;
    const auto& sourceAniData=animationSource->aniSourceData;
    if(sourceMdlData.empty()) return false;
    const char* mdl=sourceMdlData.data(); const size_t mdlSize=sourceMdlData.size();
    const size_t desc=(size_t)seq.animDescOffset;
    if(desc+100>mdlSize) return false;
    frame=std::clamp(frame,0,std::max(0,seq.numFrames-1));
    int block=seq.animBlock, animIndex=seq.animDataIndex, localFrame=frame;
    if(seq.sectionIndex>0 && seq.sectionFrames>0) {
        const int sectionCount=std::max(1,seq.numFrames/seq.sectionFrames+2);
        int section=std::clamp(frame/seq.sectionFrames,0,sectionCount-1);
        const size_t sectionEntry=desc+(size_t)seq.sectionIndex+(size_t)section*8;
        if(sectionEntry+8>mdlSize) return false;
        block=ri(mdl,mdlSize,sectionEntry);
        animIndex=ri(mdl,mdlSize,sectionEntry+4);
        localFrame=frame%seq.sectionFrames;
        if(section==sectionCount-1) localFrame=std::max(0,frame-section*seq.sectionFrames);
    }
    const char* animBytes=nullptr; size_t animSize=0; size_t streamOffset=0;
    if(block==0) {
        if(animIndex<=0) return false;
        animBytes=mdl; animSize=mdlSize; streamOffset=desc+(size_t)animIndex;
    } else {
        if(sourceAniData.empty()) return false;
        const int nBlocks=ri(mdl,mdlSize,MDL_HDR_NUMANIMBLOCKS_OFF);
        const int blocksIndex=ri(mdl,mdlSize,MDL_HDR_ANIMBLOCKIDX_OFF);
        if(block<0 || block>=nBlocks || blocksIndex<=0 || (size_t)blocksIndex+(size_t)(block+1)*MDL_ANIMBLOCK_SIZE>mdlSize) return false;
        const size_t bo=(size_t)blocksIndex+(size_t)block*MDL_ANIMBLOCK_SIZE;
        const int dataStart=ri(mdl,mdlSize,bo), dataEnd=ri(mdl,mdlSize,bo+4);
        if(dataStart<0 || dataEnd<dataStart) return false;
        animBytes=sourceAniData.data(); animSize=sourceAniData.size();
        streamOffset=(size_t)dataStart+(size_t)std::max(0,animIndex);
        if(streamOffset>=animSize || (dataEnd>0 && streamOffset>=(size_t)dataStart+(size_t)(dataEnd-dataStart))) return false;
    }
    if(!animBytes || streamOffset>=animSize) return false;

    positions.resize(bones.size()); rotations.resize(bones.size());
    for(size_t i=0;i<bones.size();++i) {
        positions[i]=bones[i].pos;
        rotations[i]=std::array<float,4>{bones[i].quat[0],bones[i].quat[1],bones[i].quat[2],bones[i].quat[3]};
        if(std::fabs(rotations[i][0])+std::fabs(rotations[i][1])+std::fabs(rotations[i][2])+std::fabs(rotations[i][3])<1e-7f) rotations[i]={0,0,0,1};
    }
    auto sampleRLE=[&](size_t valuePtr,int axis,float scale)->float {
        if(valuePtr+6>animSize || axis<0 || axis>2) return 0.0f;
        const int16_t rel=(int16_t)mdlReadU16(animBytes+valuePtr+(size_t)axis*2);
        if(rel<=0) return 0.0f;
        size_t cursor=valuePtr+(size_t)rel; int f=localFrame;
        for(int guard=0;guard<512 && cursor+2<=animSize;++guard) {
            const unsigned char valid=(unsigned char)animBytes[cursor];
            const unsigned char total=(unsigned char)animBytes[cursor+1];
            if(total==0) return 0.0f;
            if(f<total) {
                if(valid==0) return 0.0f;
                int sample=(f<valid)?f:(int)valid-1;
                const size_t v=cursor+2+(size_t)sample*2;
                if(v+2>animSize) return 0.0f;
                return (float)(int16_t)mdlReadU16(animBytes+v)*scale;
            }
            f-=total;
            const size_t next=cursor+2+(size_t)valid*2;
            if(next<=cursor || next+2>animSize) return 0.0f;
            cursor=next;
        }
        return 0.0f;
    };

    size_t cur=streamOffset; bool anyBone=false;
    for(size_t guard=0;guard<bones.size()+8 && cur+4<=animSize;++guard) {
        const unsigned char bone=(unsigned char)animBytes[cur];
        if(bone==255) break;
        const unsigned char flags=(unsigned char)animBytes[cur+1];
        const int16_t next=(int16_t)mdlReadU16(animBytes+cur+2);
        if(bone<bones.size()) {
            anyBone=true;
            const MDLBoneInfo& b=bones[bone];
            Vec3 pos=b.pos;
            std::array<float,4> quat={b.quat[0],b.quat[1],b.quat[2],b.quat[3]};
            const size_t payload=cur+4;
            const bool animDelta=(flags&0x10)!=0;
            if(flags&0x02) {
                if(payload+6<=animSize) quat=mdlReadQuat48(animBytes+payload);
            } else if(flags&0x20) {
                if(payload+8<=animSize) quat=mdlReadQuat64(animBytes+payload);
            } else if(flags&0x08) {
                Vec3 ang{sampleRLE(payload,0,b.rotScale.x),sampleRLE(payload,1,b.rotScale.y),sampleRLE(payload,2,b.rotScale.z)};
                if(!animDelta){ang.x+=b.rotEuler.x;ang.y+=b.rotEuler.y;ang.z+=b.rotEuler.z;}
                quat=mdlEulerToQuat(ang);
            } else if(animDelta) quat={0,0,0,1};
            const size_t rotBytes=((flags&0x02)?6u:0u)+((flags&0x20)?8u:0u)+((flags&0x08)?6u:0u);
            if(flags&0x01) {
                const size_t po=payload+rotBytes;
                if(po+6<=animSize) pos=mdlReadVector48(animBytes+po);
            } else if(flags&0x04) {
                const size_t pptr=payload+rotBytes;
                Vec3 v{sampleRLE(pptr,0,b.posScale.x),sampleRLE(pptr,1,b.posScale.y),sampleRLE(pptr,2,b.posScale.z)};
                if(!animDelta) v=v+b.pos;
                pos=v;
            } else if(animDelta) pos=Vec3{0,0,0};
            positions[bone]=pos; rotations[bone]=quat;
        }
        if(next<=0) break;
        if((size_t)next>animSize-cur) break;
        cur+=(size_t)next;
    }
    return anyBone;
}

static std::array<float,12> mdlMul34(const std::array<float,12>& a,const std::array<float,12>& b){
    std::array<float,12> c{};
    c[0]=a[0]*b[0]+a[1]*b[4]+a[2]*b[8]; c[1]=a[0]*b[1]+a[1]*b[5]+a[2]*b[9]; c[2]=a[0]*b[2]+a[1]*b[6]+a[2]*b[10]; c[3]=a[0]*b[3]+a[1]*b[7]+a[2]*b[11]+a[3];
    c[4]=a[4]*b[0]+a[5]*b[4]+a[6]*b[8]; c[5]=a[4]*b[1]+a[5]*b[5]+a[6]*b[9]; c[6]=a[4]*b[2]+a[5]*b[6]+a[6]*b[10]; c[7]=a[4]*b[3]+a[5]*b[7]+a[6]*b[11]+a[7];
    c[8]=a[8]*b[0]+a[9]*b[4]+a[10]*b[8]; c[9]=a[8]*b[1]+a[9]*b[5]+a[10]*b[9]; c[10]=a[8]*b[2]+a[9]*b[6]+a[10]*b[10]; c[11]=a[8]*b[3]+a[9]*b[7]+a[10]*b[11]+a[11]; return c;
}
static void mdlQuatMat(const float q0[4],const Vec3& p,std::array<float,12>& m){float x=q0[0],y=q0[1],z=q0[2],w=q0[3];float n=std::sqrt(x*x+y*y+z*z+w*w);if(n<1e-8f){x=y=z=0;w=1;}else{x/=n;y/=n;z/=n;w/=n;}m={1-2*y*y-2*z*z,2*x*y-2*z*w,2*x*z+2*y*w,p.x,2*x*y+2*z*w,1-2*x*x-2*z*z,2*y*z-2*x*w,p.y,2*x*z-2*y*w,2*y*z+2*x*w,1-2*x*x-2*y*y,p.z};}
static void mdlBuildHierarchy(const std::vector<MDLBoneInfo>& bones,const std::vector<std::array<float,12>>& local,std::vector<std::array<float,12>>& world){
    world.resize(local.size());
    std::vector<unsigned char> state(local.size(),0);
    auto resolve=[&](auto&& self,size_t i)->void{
        if(state[i]==2 || state[i]==1) return;
        state[i]=1;
        const int parent=i<bones.size()?bones[i].parent:-1;
        if(parent>=0 && parent<(int)local.size() && (size_t)parent!=i){
            if(state[(size_t)parent]!=1) self(self,(size_t)parent);
            if(state[(size_t)parent]==2) world[i]=mdlMul34(world[(size_t)parent],local[i]);
            else world[i]=local[i];
        } else world[i]=local[i];
        state[i]=2;
    };
    for(size_t i=0;i<local.size();++i) resolve(resolve,i);
}
void MDL_L::buildBindSkeletonPose(){std::vector<std::array<float,12>> local(bones.size());for(size_t i=0;i<bones.size();++i)mdlQuatMat(bones[i].quat,bones[i].pos,local[i]);mdlBuildHierarchy(bones,local,boneMatrices);}
void MDL_L::applyProceduralViewmodelPose(){std::string n=m_sequenceName;std::transform(n.begin(),n.end(),n.begin(),[](unsigned char c){return (char)std::tolower(c);});float t=m_sequenceCycle,w=std::sin(t*6.2831853f);float p=0,y=0,r=0;if(n.find("reload")!=std::string::npos){p=-0.20f*std::sin(std::min(1.0f,t)*3.14159f);y=0.18f*std::sin(std::min(1.0f,t)*3.14159f);r=.10f*w;}else if(n.find("draw")!=std::string::npos||n.find("deploy")!=std::string::npos){p=.16f*(1-t);r=-.10f*(1-t);}else if(n.find("attack")!=std::string::npos||n.find("fire")!=std::string::npos||n.find("shoot")!=std::string::npos){p=-.11f*std::sin(std::min(1.0f,t)*3.14159f);r=.025f*w;}else{p=.007f*w;r=.005f*w;}for(size_t i=0;i<bones.size();++i){std::string b=bones[i].name;std::transform(b.begin(),b.end(),b.begin(),[](unsigned char c){return (char)std::tolower(c);});if(b.find("hand")!=std::string::npos||b.find("arm")!=std::string::npos||b.find("weapon")!=std::string::npos){std::array<float,12> rot; float iq[4]={0,0,0,1}; mdlQuatMat(iq,Vec3{0,0,0},rot);float cx=std::cos(p),sx=std::sin(p),cy=std::cos(y),sy=std::sin(y),cz=std::cos(r),sz=std::sin(r);rot={cy*cz,-cy*sz,sy,0,sx*sy*cz+cx*sz,-sx*sy*sz+cx*cz,-sx*cy,0,-cx*sy*cz+sx*sz,cx*sy*sz+sx*cz,cx*cy,0};boneMatrices[i]=mdlMul34(boneMatrices[i],rot);}}}
void MDL_L::skinDynamicVertices(){if(bindMeshVertices.size()!=meshes.size())return;for(size_t mi=0;mi<meshes.size();++mi){meshes[mi].vertices=bindMeshVertices[mi];for(auto&v:meshes[mi].vertices){Vec3 p{0,0,0},n{0,0,0};float total=0;for(int j=0;j<std::min(3,(int)v.boneCount);++j){int b=v.bone[j];float w=v.boneWeight[j];if(b<0||b>=(int)bones.size()||w<=.0001f)continue;std::array<float,12> ptb{};for(int k=0;k<12;++k)ptb[k]=bones[b].poseToBone[k];auto sm=mdlMul34(boneMatrices[(size_t)b],ptb);Vec3 tp,tn;mdlTransformPoint(sm.data(),v.position,tp);tn={sm[0]*v.normal.x+sm[1]*v.normal.y+sm[2]*v.normal.z,sm[4]*v.normal.x+sm[5]*v.normal.y+sm[6]*v.normal.z,sm[8]*v.normal.x+sm[9]*v.normal.y+sm[10]*v.normal.z};p=p+tp*w;n=n+tn*w;total+=w;}if(total>.0001f){v.position=p*(1.0f/total);float ln=n.length();if(ln>.0001f)v.normal=n*(1.0f/ln);}}}}
static std::string mdlLowerName(std::string s){std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return (char)std::tolower(c);});return s;}
bool MDL_L::playSequence(const std::string& name){
    const std::string query=mdlLowerName(name);
    if(query.empty()) return false;
    for(int pass=0;pass<2;++pass){
        for(size_t i=0;i<sequences.size();++i){
            const std::string label=mdlLowerName(sequences[i].label);
            const std::string activity=mdlLowerName(sequences[i].activity);
            const bool exact=label==query||activity==query;
            const bool partial=(!label.empty()&&label.find(query)!=std::string::npos)||(!activity.empty()&&activity.find(query)!=std::string::npos);
            if((pass==0&&exact)||(pass==1&&!exact&&partial)){
                m_sequenceIndex=(int)i;
                m_sequenceCycle=0.0f;
                m_sequenceTime=0.0f;
                m_sequenceFinished=false;
                m_sequenceName=sequences[i].label.empty()?sequences[i].activity:sequences[i].label;
                m_sequenceLoop=(sequences[i].flags&1)!=0;
                return true;
            }
        }
    }
    return false;
}
float MDL_L::currentSequenceDuration() const {
    if(m_sequenceIndex<0 || m_sequenceIndex>=(int)sequences.size()) return 0.0f;
    const auto& q=sequences[(size_t)m_sequenceIndex];
    return (float)std::max(1,q.numFrames-1)/std::max(1.0f,q.fps);
}
bool MDL_L::currentSequenceLoops() const {
    return m_sequenceIndex>=0 && m_sequenceIndex<(int)sequences.size() && (sequences[(size_t)m_sequenceIndex].flags&1)!=0;
}
bool MDL_L::playActivity(const std::string& a){return playAnimationHint(a);}
bool MDL_L::playAnimationHint(const std::string& h){
    if(playSequence(h))return true;
    const std::string q=mdlLowerName(h);
    auto tryNames=[&](std::initializer_list<const char*> names){for(const char* name:names)if(playSequence(name))return true;return false;};
    if(q.find("walk")!=std::string::npos||q.find("run")!=std::string::npos||q.find("move")!=std::string::npos||q.find("locomotion")!=std::string::npos)
        return tryNames({"walk_all","walk_lower","walk","run_all","run_lower","run","locomotion","move","moving","act_walk","act_run"});
    if(q.find("reload")!=std::string::npos)
        return tryNames({"reload","reload_npc","gesture_reload","act_reload"});
    if(q.find("attack")!=std::string::npos||q.find("primary")!=std::string::npos||q.find("melee")!=std::string::npos||q.find("shoot")!=std::string::npos||q.find("fire")!=std::string::npos)
        return tryNames({"attack","range_attack","melee","shoot","fire","act_range_attack1","act_melee_attack1"});
    if(q.find("draw")!=std::string::npos||q.find("deploy")!=std::string::npos)
        return tryNames({"draw","deploy","draw_first","act_weapon_draw"});
    if(q.find("die")!=std::string::npos||q.find("death")!=std::string::npos)
        return tryNames({"death","die","dead","act_die"});
    if(q.find("idle")!=std::string::npos||q.find("stand")!=std::string::npos)
        return tryNames({"idle_all","idle_stand","idle","stand","crouch_idle","act_idle"});
    return false;
}
static std::array<float,12> mdlIdleRot(float p,float y,float r){const float cx=std::cos(p),sx=std::sin(p),cy=std::cos(y),sy=std::sin(y),cz=std::cos(r),sz=std::sin(r);return {cy*cz,-cy*sz,sy,0,sx*sy*cz+cx*sz,-sx*sy*sz+cx*cz,-sx*cy,0,-cx*sy*cz+sx*sz,cx*sy*sz+sx*cz,cx*cy,0};}
static bool mdlNameHas(const std::string& n,const char* a){return n.find(a)!=std::string::npos;}
void MDL_L::updateAnimation(float dt,bool npcSkeletalIdle){
    if(!loaded||!animated)return;
    const int entrySequenceIndex=m_sequenceIndex;
    const float entryCycle=m_sequenceCycle;
    if(!sequences.empty()){
        if(m_sequenceIndex<0){ if(!playAnimationHint("idle"))m_sequenceIndex=0; }
        if(m_sequenceIndex<0)m_sequenceIndex=0;
        if(m_sequenceIndex>=(int)sequences.size())m_sequenceIndex=(int)sequences.size()-1;
        auto &q=sequences[(size_t)m_sequenceIndex];
        float fps=std::max(1.0f,q.fps);
        float dur=std::max(1.0f/fps,(float)std::max(1,q.numFrames)/fps);
        m_sequenceTime+=std::clamp(dt,0.0f,0.25f);
        m_sequenceCycle=dur>0?m_sequenceTime/dur:0;
        if(m_sequenceCycle>=1.0f){
            if(m_sequenceLoop){m_sequenceCycle=std::fmod(m_sequenceCycle,1.0f);m_sequenceTime=m_sequenceCycle*dur;}
            else{
                m_sequenceCycle=1.0f;m_sequenceFinished=true;
                if(mdlLowerName(m_sequenceName).find("idle")==std::string::npos){
                    if(playAnimationHint("idle")) { }
                    else m_sequenceTime=0.0f;
                }
            }
        }
    } else if(!npcSkeletalIdle) return;

    if(m_sequenceIndex>=0 && m_sequenceIndex<(int)sequences.size() && m_sequenceIndex==entrySequenceIndex) {
        const auto& eventSeq=sequences[(size_t)m_sequenceIndex];
        const float nowCycle=std::clamp(m_sequenceCycle,0.0f,1.0f);
        const float oldCycle=std::clamp(entryCycle,0.0f,1.0f);
        for(const auto& ev:eventSeq.events) {
            const bool crossed=(nowCycle>=oldCycle)?(ev.cycle>oldCycle && ev.cycle<=nowCycle): (ev.cycle>oldCycle || ev.cycle<=nowCycle);
            if(!crossed) continue;


            if(ev.event==5004 || ev.event==5005 || ev.event==5007 || ev.event==5008) {
                std::string sound=ev.options;
                if(ev.event==5007 || ev.event==5008) {
                    const size_t space=sound.find_first_of(" \t");
                    if(space!=std::string::npos) sound=sound.substr(0,space);
                }
                if(!sound.empty()) Sound_L::Get().play(sound,animationEventPosition,1.0f,animationEventSpatial,true);
            } else if((ev.event==4 || ev.event==5) && !animationEventSpatial) {

                if(!ev.options.empty()) Sound_L::Get().play(ev.options,animationEventPosition,0.8f,false,true);
            }
        }
    }

    bool decoded=false;
    std::vector<Vec3> pos0,pos1;
    std::vector<std::array<float,4>> rot0,rot1;
    if(m_sequenceIndex>=0 && m_sequenceIndex<(int)sequences.size()) {
        const auto& q=sequences[(size_t)m_sequenceIndex];
        const float f=std::clamp(m_sequenceCycle,0.0f,1.0f)*(float)std::max(0,q.numFrames-1);
        const int frame0=(int)std::floor(f), frame1=std::min(std::max(0,q.numFrames-1),frame0+1);
        const float alpha=f-(float)frame0;
        if(decodeAnimationFrame(q,frame0,pos0,rot0) && decodeAnimationFrame(q,frame1,pos1,rot1) &&
           pos0.size()==bones.size() && pos1.size()==bones.size()) {
            decoded=true;
            std::vector<std::array<float,12>> local(bones.size());
            for(size_t i=0;i<bones.size();++i) {
                const Vec3 p=pos0[i]+(pos1[i]-pos0[i])*alpha;
                const auto qrot=mdlQuatSlerp(rot0[i],rot1[i],alpha);
                mdlQuatMat(qrot.data(),p,local[i]);
            }
            mdlBuildHierarchy(bones,local,boneMatrices);
        }
    }
    if(!decoded) {
        const float t=m_sequenceCycle*6.283185307f;
        std::vector<std::array<float,12>> local(bones.size());
        for(size_t i=0;i<bones.size();++i){
            mdlQuatMat(bones[i].quat,bones[i].pos,local[i]);
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
                if(std::fabs(p)+std::fabs(y)+std::fabs(r)>0.00001f)local[i]=mdlMul34(local[i],mdlIdleRot(p,y,r));
            }
        }
        mdlBuildHierarchy(bones,local,boneMatrices);
        if(!npcSkeletalIdle)applyProceduralViewmodelPose();
    }
    if(!boneMatrices.empty()){
        const bool seqChanged=(m_poseSequence!=m_sequenceIndex || m_prevBoneMatrices.size()!=boneMatrices.size());
        m_poseSequence=m_sequenceIndex;
        if(seqChanged){
            m_poseBlend=0.0f;
            if(m_prevBoneMatrices.size()!=boneMatrices.size()) m_prevBoneMatrices=boneMatrices;
        }
        if(m_prevBoneMatrices.size()==boneMatrices.size()){
            m_poseBlend=std::min(1.0f,m_poseBlend+std::clamp(dt,0.0f,0.1f)*6.0f);
            const float a=m_poseBlend*m_poseBlend*(3.0f-2.0f*m_poseBlend);
            for(size_t i=0;i<boneMatrices.size();++i){
                for(int k=0;k<12;++k) boneMatrices[i][k]=m_prevBoneMatrices[i][k]+(boneMatrices[i][k]-m_prevBoneMatrices[i][k])*a;
                for(int r=0;r<3;++r){
                    float* row=boneMatrices[i].data()+(size_t)r*4;
                    const float len=std::sqrt(row[0]*row[0]+row[1]*row[1]+row[2]*row[2]);
                    if(len>1e-6f){row[0]/=len;row[1]/=len;row[2]/=len;}
                }
            }
        }
        m_prevBoneMatrices=boneMatrices;
    }
    skinDynamicVertices();
}

void MDL_L::buildBindPoseMatrices(std::vector<std::array<float,12>>& out) const{
    std::vector<std::array<float,12>> local(bones.size());
    for(size_t i=0;i<bones.size();++i)mdlQuatMat(bones[i].quat,bones[i].pos,local[i]);
    mdlBuildHierarchy(bones,local,out);
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
