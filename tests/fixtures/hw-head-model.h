#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t NvU32;
typedef int32_t NvS32;
typedef int NvBool;
#define TRUE 1
#define FALSE 0
#define NVBIT(x) (1U << (x))
#define NV_INVALID_HEAD 0xFFFFFFFF
#define nvAssert(x) assert(x)
#define nvPopCount32(x) ((NvU32)__builtin_popcount(x))
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
} NVConnectorEvoRec;
/* Test-only dpy: the flags stand in for the DP library and mode queries. */
typedef struct {
    const NVConnectorEvoRec *pConnectorEvo;
    NvBool usesDPLib, mst, dscPossible, use2Heads1OR;
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
typedef struct {
    NVProposedModeSetStateOneApiHead apiHead[4];
} NVProposedModeSetHwStateOneDisp;

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
