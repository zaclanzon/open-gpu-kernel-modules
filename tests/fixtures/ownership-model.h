#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "nvtypes.h"
#include "nvmisc.h"
#include "class/clca7d.h"

#define nvDoDebugLogging() 0
#define nvEvoLogDev(...) ((void)0)
#define TRUE 1
#define FALSE 0
#define nvAssert(x) assert(x)
#define nvPopCount32(x) ((NvU32)__builtin_popcount(x))
#define NV_EVO_CHANNEL_MASK_WINDOW_NUMBER(x) (x)

typedef struct {
    NvU32 channelMask;
} NVEvoChannel;

typedef struct {
    NvU32 noCoreInterlockMask;
} NVEvoUpdateState;

typedef struct {
    NvBool windowMappingChanged;
    NVEvoUpdateState updateState;
} NVEvoModesetUpdateState;

typedef struct {
    void *pCoreDma;
} TestSubDevice;

typedef struct {
    NVEvoChannel *core;
    TestSubDevice *pSubDevices[1];
    struct {
        NvU32 numLayers;
        NVEvoChannel *layer[2];
    } head[4];
} NVDevEvoRec;

typedef struct {
    NVDevEvoRec *pDevEvo;
    NvU32 displayOwner;
} NVDispEvoRec;

typedef struct {
    NvU32 tilesMask;
    NvU32 phywinsMask[2];
} NVHwHeadMultiTileConfigRec;

typedef struct {
    int unused;
} NVHwModeTimingsEvo;

typedef struct {
    int unused;
} NVDscInfoEvoRec;

static NvU32 stateCache[65536];
static NvU32 pendingMethods[65536];
static NvU32 currentMethod;

static void nvUpdateUpdateState(NVDevEvoRec *pDevEvo, NVEvoUpdateState *pUpdateState,
                                NVEvoChannel *pChannel)
{
    (void)pDevEvo;
    (void)pUpdateState;
    (void)pChannel;
}

static void nvDisableCoreInterlockUpdateState(NVDevEvoRec *pDevEvo, NVEvoUpdateState *pUpdateState,
                                              const NVEvoChannel *pChannel)
{
    (void)pDevEvo;
    pUpdateState->noCoreInterlockMask |= 1U << pChannel->channelMask;
}

static NvU32 nvDmaLoadPioMethod(const void *pCoreDma, NvU32 method)
{
    (void)pCoreDma;
    assert(method < 65536);
    return stateCache[method];
}

static void nvDmaSetStartEvoMethod(NVEvoChannel *pChannel, NvU32 method, NvU32 count)
{
    (void)pChannel;
    assert(count == 1 && method < 65536);
    currentMethod = method;
}

static void nvDmaSetEvoMethodData(NVEvoChannel *pChannel, NvU32 value)
{
    (void)pChannel;
    pendingMethods[currentMethod] = value;
}

static void SetTileSize(NVEvoChannel *pChannel, const NVHwModeTimingsEvo *pTimings,
                        const NVDscInfoEvoRec *pDscInfo, const NVHwHeadMultiTileConfigRec *pConfig)
{
    (void)pChannel;
    (void)pTimings;
    (void)pDscInfo;
    (void)pConfig;
}

static NvU32 GetPendingWindowOwner(NvU32 window)
{
    return DRF_VAL(CA7D, _WINDOW_SET_CONTROL, _OWNER,
                   pendingMethods[NVCA7D_WINDOW_SET_CONTROL(window)]);
}

/* Model completed method execution; this does not simulate hardware timing. */
static void CommitPendingMethods(void)
{
    memcpy(stateCache, pendingMethods, sizeof(stateCache));
}
