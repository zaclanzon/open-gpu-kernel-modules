#!/usr/bin/env python3
"""Check generated CA ownership/physical methods against allocation invariants.

This uses the production functions and hardware register definitions, but a
software method recorder. It does not validate real hardware timing or firmware.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
evo = (root / 'src/nvidia-modeset/src/nvkms-evo4.c').read_text()
start = evo.index('static void EvoTrackPhysicalWindowChangeCA(')
end = evo.index('\nNVEvoHAL nvEvoC9 =', start)
pre = r'''
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

static void nvUpdateUpdateState(
    NVDevEvoRec *pDevEvo,
    NVEvoUpdateState *pUpdateState,
    NVEvoChannel *pChannel)
{
    (void)pDevEvo;
    (void)pUpdateState;
    (void)pChannel;
}

static void nvDisableCoreInterlockUpdateState(
    NVDevEvoRec *pDevEvo,
    NVEvoUpdateState *pUpdateState,
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

static void nvDmaSetStartEvoMethod(
    NVEvoChannel *pChannel,
    NvU32 method,
    NvU32 count)
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

static void SetTileSize(
    NVEvoChannel *pChannel,
    const NVHwModeTimingsEvo *pTimings,
    const NVDscInfoEvoRec *pDscInfo,
    const NVHwHeadMultiTileConfigRec *pConfig)
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
'''
post = r'''
int main(void)
{
    NVEvoChannel core = { 0 };
    NVEvoChannel base = { 2 };
    NVEvoChannel overlay = { 3 };
    TestSubDevice subDevice = { stateCache };
    NVDevEvoRec dev = {
        .core = &core,
        .pSubDevices = { &subDevice },
    };
    NVDispEvoRec disp = { &dev, 0 };
    NVEvoModesetUpdateState update = { 0 };
    NVHwModeTimingsEvo timing = { 0 };
    NVDscInfoEvoRec dsc = { 0 };
    NVHwHeadMultiTileConfigRec config = {
        .tilesMask = 3,
        .phywinsMask = { 3, 0 },
    };

    dev.head[1].numLayers = 2;
    dev.head[1].layer[0] = &base;
    dev.head[1].layer[1] = &overlay;

    /* Simulate stock window owners, including the unrelated HIDE control bit. */
    stateCache[NVCA7D_WINDOW_SET_CONTROL(2)] = 1;
    stateCache[NVCA7D_WINDOW_SET_CONTROL(3)] = 0x101;
    stateCache[NVCA7D_WINDOW_SET_PHYSICAL(2)] = 1;
    stateCache[NVCA7D_WINDOW_SET_PHYSICAL(3)] = 2;
    memcpy(pendingMethods, stateCache, sizeof(stateCache));

    EvoSetMultiTileConfigCA(&disp, 1, &timing, &dsc, &config, &update);

    assert(GetPendingWindowOwner(2) == 1);
    assert(GetPendingWindowOwner(3) == NVCA7D_WINDOW_SET_CONTROL_OWNER_NONE);
    assert(pendingMethods[NVCA7D_WINDOW_SET_CONTROL(3)] == 0x10f);
    assert(pendingMethods[NVCA7D_WINDOW_SET_PHYSICAL(2)] == 3);
    assert(pendingMethods[NVCA7D_WINDOW_SET_PHYSICAL(3)] == 0);
    assert(update.windowMappingChanged);
    assert(update.updateState.noCoreInterlockMask == 12);
    puts("PASS: unused overlay is unowned with no physical windows; "
         "base remains bound");

    /* Repeating the same configuration must not introduce an interlock change. */
    CommitPendingMethods();
    memset(&update, 0, sizeof(update));

    EvoSetMultiTileConfigCA(&disp, 1, &timing, &dsc, &config, &update);

    assert(!update.windowMappingChanged);
    assert(!update.updateState.noCoreInterlockMask);
    puts("PASS: unchanged allocation/ownership introduces no extra interlock "
         "changes");

    /* Restoring overlay resources also restores its original hardware owner. */
    config.tilesMask = 1;
    config.phywinsMask[0] = 1;
    config.phywinsMask[1] = 2;

    EvoSetMultiTileConfigCA(&disp, 1, &timing, &dsc, &config, &update);

    assert(GetPendingWindowOwner(3) == 1);
    assert(pendingMethods[NVCA7D_WINDOW_SET_PHYSICAL(3)] == 2);
    assert(update.windowMappingChanged);
    assert(update.updateState.noCoreInterlockMask & 8);
    puts("PASS: re-enabled overlay regains its owner and physical assignment");

    /* Shutdown releases physical windows while leaving owners unchanged. */
    CommitPendingMethods();
    memset(&update, 0, sizeof(update));
    memset(&config, 0, sizeof(config));

    EvoSetMultiTileConfigCA(&disp, 1, NULL, NULL, &config, &update);

    assert(GetPendingWindowOwner(2) == 1);
    assert(GetPendingWindowOwner(3) == 1);
    assert(pendingMethods[NVCA7D_WINDOW_SET_PHYSICAL(2)] == 0);
    assert(pendingMethods[NVCA7D_WINDOW_SET_PHYSICAL(3)] == 0);
    puts("PASS: shutdown clears physical assignments without changing "
         "ownership in that update");

    /* Only the following activation update removes the unused window owner. */
    CommitPendingMethods();
    memset(&update, 0, sizeof(update));
    config.tilesMask = 3;
    config.phywinsMask[0] = 3;

    EvoSetMultiTileConfigCA(&disp, 1, &timing, &dsc, &config, &update);

    assert(GetPendingWindowOwner(2) == 1);
    assert(GetPendingWindowOwner(3) == NVCA7D_WINDOW_SET_CONTROL_OWNER_NONE);
    assert(update.windowMappingChanged);
    assert(update.updateState.noCoreInterlockMask & 8);
    puts("PASS: subsequent activation unbinds the unused overlay with a "
         "non-interlocked update");

    /*
     * Initialization can queue a default binding before this helper runs.
     * PIO still describes the previous unowned state. The final owner write
     * must override the queued binding, even though PIO matches our target.
     */
    stateCache[NVCA7D_WINDOW_SET_CONTROL(2)] = 1;
    stateCache[NVCA7D_WINDOW_SET_CONTROL(3)] =
        NVCA7D_WINDOW_SET_CONTROL_OWNER_NONE;
    stateCache[NVCA7D_WINDOW_SET_PHYSICAL(2)] = 3;
    stateCache[NVCA7D_WINDOW_SET_PHYSICAL(3)] = 0;
    memcpy(pendingMethods, stateCache, sizeof(stateCache));
    pendingMethods[NVCA7D_WINDOW_SET_CONTROL(3)] = 1;
    memset(&update, 0, sizeof(update));
    update.windowMappingChanged = TRUE;
    update.updateState.noCoreInterlockMask = 8;

    EvoSetMultiTileConfigCA(&disp, 1, &timing, &dsc, &config, &update);

    assert(GetPendingWindowOwner(3) == NVCA7D_WINDOW_SET_CONTROL_OWNER_NONE);
    assert(update.windowMappingChanged);
    assert(update.updateState.noCoreInterlockMask & 8);
    puts("PASS: an unused overlay overrides a queued initialization binding");

    return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / 'test.c'
    exe = Path(tmp) / 'test'
    c.write_text(pre + evo[start:end] + post)
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined',
                    '-I' + str(root / 'src/common/sdk/nvidia/inc'),
                    str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
