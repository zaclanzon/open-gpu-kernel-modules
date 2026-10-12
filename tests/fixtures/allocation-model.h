#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t NvU32;
typedef int NvBool;
#define TRUE 1
#define FALSE 0
#define NVKMS_MAX_HEADS_PER_DISP 4
#define NV_YUV420_MODE_HW 1
#define NVBIT(x) (1U << (x))
#define NVBIT64(x) (UINT64_C(1) << (x))
#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))
#define nvAssert(x) assert(x)
#define nvPopCount32(x) ((NvU32)__builtin_popcount(x))
#define FOR_EACH_INDEX_IN_MASK(bits, i, mask)                                                      \
    for ((i) = 0; (i) < (bits); (i)++)                                                             \
        if ((mask) & NVBIT(i))
#define FOR_EACH_INDEX_IN_MASK_END
typedef enum {
    NV_EVO_HW_TILE_TYPE_0,
    NV_EVO_HW_TILE_TYPE_1,
    NV_EVO_HW_TILE_TYPE_FIRST = 0,
    NV_EVO_HW_TILE_TYPE_LAST = 1
} NVEvoHwTileType;
typedef struct {
    NvU32 tilesMask, phywinsMask[2];
} NVHwHeadMultiTileConfigRec;
struct NvKmsUsageBounds {
    struct {
        NvBool usable;
    } layer[2];
};
typedef struct {
    struct {
        struct {
            NvU32 width, height;
        } in, out;
    } viewPort;
    NvU32 yuv420Mode;
} NVHwModeTimingsEvo;
typedef struct {
    struct {
        NVEvoHwTileType type;
    } hwTile[8];
} NVEvoCapabilities;
typedef struct {
    NvU32 numHeads, numHwTiles, numHwPhywins;
    struct {
        NvU32 numLayers;
    } head[4];
    struct {
        NVEvoCapabilities capabilities;
    } gpus[1];
} NVDevEvoRec;
typedef struct {
    NvU32 hwHeadsMask;
} NVDispApiHeadStateEvoRec;
typedef struct {
    NVHwHeadMultiTileConfigRec multiTileConfig;
} TestHead;
typedef struct {
    NVDevEvoRec *pDevEvo;
    NvU32 displayOwner;
    NVDispApiHeadStateEvoRec apiHeadState[4];
    TestHead headState[4];
} NVDispEvoRec;
typedef NVDispEvoRec *NVDispEvoPtr;
typedef struct {
    TestHead head[4];
} NVProposedModeSetHwStateOneDisp;
#define FOR_EACH_EVO_HW_HEAD_IN_MASK(mask, head) FOR_EACH_INDEX_IN_MASK(32, head, mask)
typedef struct {
    struct {
        NVHwModeTimingsEvo *pTimings;
        struct NvKmsUsageBounds *pUsage;
        NvBool modesetRequested;
        NVHwHeadMultiTileConfigRec multiTileConfig;
    } head[4];
} NVEvoIsModePossibleDispInput;
typedef struct {
    struct {
        NVHwHeadMultiTileConfigRec multiTileConfig;
    } head[4];
} NVEvoIsModePossibleDispOutput;
