#pragma once
#include "math.h"                                                                           
#include <cstdint>

                                                               
                                           
                                                               

struct BSPLump {
    int offset;
    int length;
    int version;
    int uncompLength;
};
static_assert(sizeof(BSPLump) == 16, "BSPLump must be 16 bytes");

struct BSPHeader {
    int magic;                                                 
    int version;                     
    BSPLump lumps[64];
    int mapRevision;
};
static_assert(sizeof(BSPHeader) == 1036, "BSPHeader must be 1036 bytes");

struct BSPVertex {
    Vec3 position;
};
static_assert(sizeof(BSPVertex) == 12, "BSPVertex must be 12 bytes");

struct BSPEdge {
    unsigned short v[2];
};
static_assert(sizeof(BSPEdge) == 4, "BSPEdge must be 4 bytes");

struct BSPFace {
    unsigned short planenum;
    char side;
    char onNode;
    int firstedge;
    short numedges;
    short texinfo;
    short dispinfo;
    short surfaceFogVolumeID;
    char styles[4];
    int lightofs;
    float area;
    int LightmapTextureMinsInLuxels[2];
    int LightmapTextureSizeInLuxels[2];
    int origFace;
    unsigned short numPrims;
    unsigned short firstPrimID;
    unsigned int smoothingGroups;
};
static_assert(sizeof(BSPFace) == 56, "BSPFace must be 56 bytes");

struct BSPTexInfo {
    float textureVecs[2][4];
    float lightmapVecs[2][4];
    int flags;
    int texdata;
};
static_assert(sizeof(BSPTexInfo) == 72, "BSPTexInfo must be 72 bytes");

struct BSPTexData {
    Vec3 reflectivity;
    int nameStringTableID;
    int width, height;
    int view_width, view_height;
};
static_assert(sizeof(BSPTexData) == 32, "BSPTexData must be 32 bytes");

struct BSPNode {
    int planenum;
    int children[2];
    short mins[3];
    short maxs[3];
    unsigned short firstface;
    unsigned short numfaces;
    short area;
    short paddding;
};
static_assert(sizeof(BSPNode) == 32, "BSPNode must be 32 bytes");

struct BSPLeaf {
    int contents;
    short cluster;
    short area_flags;
    short mins[3];
    short maxs[3];
    unsigned short firstleafface;
    unsigned short numleaffaces;
    unsigned short firstleafbrush;
    unsigned short numleafbrushes;
    short leafWaterDataID;
};
static_assert(sizeof(BSPLeaf) == 30 || sizeof(BSPLeaf) == 32, "BSPLeaf size unexpected");

struct BSPPlane {
    Vec3 normal;
    float dist;
    int type;
};
static_assert(sizeof(BSPPlane) == 20, "BSPPlane must be 20 bytes");

struct BSPModel {
    Vec3 mins, maxs;
    Vec3 origin;
    int headnode;
    int firstface, numfaces;
};
static_assert(sizeof(BSPModel) == 48, "BSPModel must be 48 bytes");

struct BSPDispInfo {
    Vec3 startPosition;
    int dispVertStart;
    int dispTriStart;
    int power;
    int minTess;
    float smoothingAngle;
    int contents;
    unsigned short mapFace;
    unsigned short _pad0;
    int lightmapAlphaStart;
    int lightmapSamplePositionStart;
    char edgeNeighbors[48];
    char cornerNeighbors[40];
    unsigned int allowedVerts[10];
};
static_assert(sizeof(BSPDispInfo) == 176, "BSPDispInfo MUST be 176 bytes!");

struct BSPDispVert {
    Vec3 vec;
    float dist;
    float alpha;
};
static_assert(sizeof(BSPDispVert) == 20, "BSPDispVert must be 20 bytes");

                                                            
struct ColorRGBExp32 {
    unsigned char r, g, b;
    signed char exponent;
};

                                                                
struct GeneratedDispTri {
    Vec3 v[3];
    Vec2 uv[3];
    int texID;
};

                                         
enum BSPLumpIndex : int {
    LUMP_ENTITIES = 0,
    LUMP_TEXDATA = 2,
    LUMP_VERTICES = 3,
    LUMP_TEXINFO = 6,
    LUMP_FACES = 7,
    LUMP_LIGHTING = 8,
    LUMP_EDGES = 12,
    LUMP_SURFEDGES = 13,
    LUMP_MODELS = 14,
    LUMP_DISPINFO = 26,
    LUMP_ORIGINALFACES = 27,
    LUMP_DISPVERTS = 33,
    LUMP_PAKFILE = 40,
    LUMP_TEXDATA_STRING_DATA = 43,
    LUMP_TEXDATA_STRING_TABLE = 44,
    LUMP_FACES_HDR = 58
};