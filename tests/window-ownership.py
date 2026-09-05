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
typedef struct { NvU32 channelMask; } NVEvoChannel;
typedef struct { NvU32 noCoreInterlockMask; } NVEvoUpdateState;
typedef struct { NvBool windowMappingChanged; NVEvoUpdateState updateState; } NVEvoModesetUpdateState;
typedef struct { void *pCoreDma; } Sub;
typedef struct {
 NVEvoChannel *core;
 Sub *pSubDevices[1];
 struct { NvU32 numLayers; NVEvoChannel *layer[2]; } head[4];
} NVDevEvoRec;
typedef struct { NVDevEvoRec *pDevEvo; NvU32 displayOwner; } NVDispEvoRec;
typedef struct { NvU32 tilesMask, phywinsMask[2]; } NVHwHeadMultiTileConfigRec;
typedef struct { int unused; } NVHwModeTimingsEvo;
typedef struct { int unused; } NVDscInfoEvoRec;
static NvU32 cache[65536], pending[65536], currentMethod;
static void nvUpdateUpdateState(NVDevEvoRec *d,NVEvoUpdateState *u,NVEvoChannel *c) {(void)d;(void)u;(void)c;}
static void nvDisableCoreInterlockUpdateState(NVDevEvoRec *d,NVEvoUpdateState *u,const NVEvoChannel *c) {
 (void)d;u->noCoreInterlockMask |= 1U<<c->channelMask;
}
static NvU32 nvDmaLoadPioMethod(const void *p,NvU32 method) {
 (void)p; assert(method<65536);return cache[method];
}
static void nvDmaSetStartEvoMethod(NVEvoChannel *c,NvU32 method,NvU32 n) {
 (void)c;assert(n==1 && method<65536);currentMethod=method;
}
static void nvDmaSetEvoMethodData(NVEvoChannel *c,NvU32 value) {
 (void)c;pending[currentMethod]=value;
}
static void SetTileSize(NVEvoChannel *c,const NVHwModeTimingsEvo *t,
 const NVDscInfoEvoRec *d,const NVHwHeadMultiTileConfigRec *m) {(void)c;(void)t;(void)d;(void)m;}
static NvU32 owner(NvU32 win) {
 return DRF_VAL(CA7D,_WINDOW_SET_CONTROL,_OWNER,pending[NVCA7D_WINDOW_SET_CONTROL(win)]);
}
static void commit(void) {memcpy(cache,pending,sizeof(cache));}
'''
post = r'''
int main(void) {
 NVEvoChannel core={0},base={2},overlay={3};Sub sub={cache};
 NVDevEvoRec dev={.core=&core,.pSubDevices={&sub}};
 dev.head[1].numLayers=2;dev.head[1].layer[0]=&base;dev.head[1].layer[1]=&overlay;
 NVDispEvoRec disp={&dev,0};NVEvoModesetUpdateState update={0};
 NVHwModeTimingsEvo timing={0};NVDscInfoEvoRec dsc={0};
 NVHwHeadMultiTileConfigRec config={.tilesMask=3,.phywinsMask={3,0}};
 // Simulate stock window owners, preserving the unrelated HIDE control bit.
 cache[NVCA7D_WINDOW_SET_CONTROL(2)]=1;
 cache[NVCA7D_WINDOW_SET_CONTROL(3)]=0x101;
 cache[NVCA7D_WINDOW_SET_PHYSICAL(2)]=1;
 cache[NVCA7D_WINDOW_SET_PHYSICAL(3)]=2;
 memcpy(pending,cache,sizeof(cache));
 EvoSetMultiTileConfigCA(&disp,1,&timing,&dsc,&config,&update);
 assert(owner(2)==1 && owner(3)==NVCA7D_WINDOW_SET_CONTROL_OWNER_NONE);
 assert(pending[NVCA7D_WINDOW_SET_CONTROL(3)]==0x10f);
 assert(pending[NVCA7D_WINDOW_SET_PHYSICAL(2)]==3);
 assert(pending[NVCA7D_WINDOW_SET_PHYSICAL(3)]==0);
 assert(update.windowMappingChanged && update.updateState.noCoreInterlockMask==12);
 puts("PASS: unused overlay is unowned with no physical windows; base remains bound");
 commit();memset(&update,0,sizeof(update));
 EvoSetMultiTileConfigCA(&disp,1,&timing,&dsc,&config,&update);
 assert(!update.windowMappingChanged && !update.updateState.noCoreInterlockMask);
 puts("PASS: unchanged allocation/ownership introduces no extra interlock changes");
 config.tilesMask=1;config.phywinsMask[0]=1;config.phywinsMask[1]=2;
 EvoSetMultiTileConfigCA(&disp,1,&timing,&dsc,&config,&update);
 assert(owner(3)==1 && pending[NVCA7D_WINDOW_SET_PHYSICAL(3)]==2);
 assert(update.windowMappingChanged && (update.updateState.noCoreInterlockMask & 8));
 puts("PASS: re-enabled overlay regains its owner and physical assignment");
 commit();memset(&update,0,sizeof(update));
 memset(&config,0,sizeof(config));
 EvoSetMultiTileConfigCA(&disp,1,NULL,NULL,&config,&update);
 assert(owner(2)==1 && owner(3)==1);
 assert(pending[NVCA7D_WINDOW_SET_PHYSICAL(2)]==0 && pending[NVCA7D_WINDOW_SET_PHYSICAL(3)]==0);
 puts("PASS: shutdown clears physical assignments without changing ownership in that update");
 commit();memset(&update,0,sizeof(update));
 config.tilesMask=3;config.phywinsMask[0]=3;
 EvoSetMultiTileConfigCA(&disp,1,&timing,&dsc,&config,&update);
 assert(owner(2)==1 && owner(3)==NVCA7D_WINDOW_SET_CONTROL_OWNER_NONE);
 assert(update.windowMappingChanged && (update.updateState.noCoreInterlockMask & 8));
 puts("PASS: subsequent activation unbinds the unused overlay with a non-interlocked update");
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
