#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t NvU32;
typedef int32_t NvS32;
typedef int NvBool;
#define TRUE 1
#define FALSE 0
/* Stock AssignProposedHwHeadsForDPSerializer() takes NVBIT(NV_INVALID_HEAD) when no head is free. */
#define NVBIT(x) (((NvU32)(x) < 32U) ? (1U << (x)) : 0U)
#define NV_INVALID_HEAD 0xFFFFFFFF
#define NV_MAX_HEADS 4
#define nvAssert(x) assert(x)
#define nvPopCount32(x) ((NvU32)__builtin_popcount(x))
#define nvkms_memset memset
enum {
    NVKMS_CONNECTOR_SIGNAL_FORMAT_TMDS,
    NVKMS_CONNECTOR_SIGNAL_FORMAT_DP,
    NVKMS_CONNECTOR_SIGNAL_FORMAT_DSI,
};
enum {
    NVKMS_CONNECTOR_TYPE_DP,
    NVKMS_CONNECTOR_TYPE_HDMI,
    NVKMS_CONNECTOR_TYPE_DP_SERIALIZER,
};
typedef struct {
    int signalFormat, type;
    struct {
        NvBool supportsMST;
    } dpSerializerCaps;
} NVConnectorEvoRec;
/* Test-only dpy: the flags stand in for the DP library and mode queries. */
typedef struct {
    const NVConnectorEvoRec *pConnectorEvo;
    NvBool usesDPLib, mst, dscPossible, use2Heads1OR;
    struct {
        NvU32 serializerStreamIndex;
    } dp;
} NVDpyEvoRec;
typedef struct {
    void (*InitHwHeadMultiTileConfig)(void *pDevEvo);
} NVEvoHalRec;
typedef struct {
    const NVEvoHalRec *hal;
    NvU32 numHeads, numApiHeads;
    struct {
        NvU32 numLayers;
    } head[4], apiHead[4];
} NVDevEvoRec;
typedef struct {
    const NVDevEvoRec *pDevEvo;
    NvU32 displayOwner;
    NVDpyEvoRec dpys[4];
} NVDispEvoRec;
/* A dpy id list holds at most one dpy here: its index, or -1 when empty. */
typedef int NVDpyIdList;
typedef struct {
    int unused;
} NVHwModeTimingsEvo;
struct NvKmsModeValidationParams {
    int unused;
};
typedef struct {
    NvU32 hwHeadsMask;
    struct {
        NvU32 numberOfHardwareHeadsUsed;
    } attributes;
    NVDpyIdList dpyIdList;
    NVHwModeTimingsEvo timings;
    struct NvKmsModeValidationParams modeValidationParams;
    NvBool changed;
} NVProposedModeSetStateOneApiHead;
/* Test-only hardware-head state: 1 + the api-head it was programmed for. */
typedef struct {
    int owner;
} NVProposedModeSetHwStateOneHead;
typedef struct {
    NVProposedModeSetStateOneApiHead apiHead[4];
    NVProposedModeSetHwStateOneHead head[4];
} NVProposedModeSetHwStateOneDisp;
typedef struct {
    NVProposedModeSetHwStateOneDisp disp[1];
    struct {
        struct {
            struct {
                int owner;
            } flip;
        } head[4];
    } sd[1];
} NVProposedModeSetHwState;

static inline NvU32 GetNextHwHead(NvU32 hwHeadsMask, const NvU32 prevHwHead)
{
    if (prevHwHead != NV_INVALID_HEAD) {
        hwHeadsMask &= ~(NVBIT(prevHwHead + 1) - 1);
    }
    return (hwHeadsMask == 0) ? NV_INVALID_HEAD : (NvU32)__builtin_ctz(hwHeadsMask);
}
#define FOR_EACH_EVO_HW_HEAD_IN_MASK(__hwHeadsMask, __hwHead)           \
    for ((__hwHead)  = GetNextHwHead((__hwHeadsMask), NV_INVALID_HEAD); \
         (__hwHead) != NV_INVALID_HEAD;                                 \
         (__hwHead)  = GetNextHwHead((__hwHeadsMask), (__hwHead)))

static inline void nvAssignHwHeadsMaskProposedApiHead(
    NVProposedModeSetStateOneApiHead *pProposedApiHead, const NvU32 hwHeadsMask)
{
    pProposedApiHead->hwHeadsMask = hwHeadsMask;
    pProposedApiHead->attributes.numberOfHardwareHeadsUsed = nvPopCount32(hwHeadsMask);
}
static NvBool nvDpyIdListIsEmpty(NVDpyIdList dpyIdList) { return dpyIdList < 0; }
static const NVDpyEvoRec *nvGetOneArbitraryDpyEvo(NVDpyIdList dpyIdList,
                                                  const NVDispEvoRec *pDispEvo)
{
    return (dpyIdList < 0) ? NULL : &pDispEvo->dpys[dpyIdList];
}
static NvBool nvConnectorIsDPSerializer(const NVConnectorEvoRec *pConnectorEvo)
{
    return pConnectorEvo->type == NVKMS_CONNECTOR_TYPE_DP_SERIALIZER;
}
static NvBool nvDpyUsesDPLib(const NVDpyEvoRec *pDpyEvo) { return pDpyEvo->usesDPLib; }
static NvBool nvDpyEvoIsDPMST(const NVDpyEvoRec *pDpyEvo) { return pDpyEvo->mst; }
static NvBool nvDPDpyIsDscPossible(const NVDpyEvoRec *pDpyEvo)
{
    return pDpyEvo->usesDPLib && pDpyEvo->dscPossible;
}
static NvBool nvEvoUse2Heads1OR(const NVDpyEvoRec *pDpyEvo, const NVHwModeTimingsEvo *pTimings,
                                const struct NvKmsModeValidationParams *pParams)
{
    (void)pTimings;
    (void)pParams;
    return pDpyEvo->use2Heads1OR;
}
