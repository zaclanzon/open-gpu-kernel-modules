#!/usr/bin/env python3
"""Test production physical-window tracking and modeset/flip ordering."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
evo = (root/'src/nvidia-modeset/src/nvkms-evo4.c').read_text()
a = evo.index('static void EvoTrackPhysicalWindowChangeCA(')
b = evo.index('static void EvoSetMultiTileConfigCA(', a)
modeset = (root/'src/nvidia-modeset/src/nvkms-modeset.c').read_text()
c = modeset.index('static void\nKickoffProposedModeSetHwState(')
d = modeset.index('static void AllocatePostModesetDispBandwidth(', c)
pre = r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef unsigned NvU32;
typedef int NvBool;
#define TRUE 1
#define FALSE 0
#define EVO_LOG_INFO 0
#define nvDoDebugLogging() 0
#define nvEvoLogDev(...) ((void)0)
#define NV_EVO_CHANNEL_MASK_WINDOW_NUMBER(x) (x)
typedef struct { NvU32 channelMask; } NVEvoChannel;
typedef struct { NvU32 noCoreInterlockMask; } NVEvoUpdateState;
typedef struct { NvBool windowMappingChanged; NVEvoUpdateState updateState; } NVEvoModesetUpdateState;
struct Dev;
typedef struct { void (*Update)(struct Dev *, const NVEvoUpdateState *, NvBool); } Hal;
typedef struct Dev { NvU32 numApiHeads; Hal *hal; } NVDevEvoRec;
typedef struct { NVDevEvoRec *pDevEvo; } Disp;
typedef Disp *NVDispEvoPtr;
typedef struct { int dummy; } NVProposedModeSetHwState;
typedef struct { int apiHead[4]; } NVProposedModeSetHwStateOneDisp;
typedef struct { NVEvoModesetUpdateState modesetUpdateState; } NVModeSetWorkArea;
static void nvDisableCoreInterlockUpdateState(NVDevEvoRec *dev, NVEvoUpdateState *u, const NVEvoChannel *ch) {
 (void)dev; u->noCoreInterlockMask |= ch->channelMask;
}
static char events[16]; static int n, changeInPre;
static void event(char c) { events[n++]=c; }
static void update(NVDevEvoRec *dev,const NVEvoUpdateState *u,NvBool release) {
 (void)dev; (void)u; (void)release; event('U');
}
#define ApplyProposedModeSetStateOneApiHeadShutDown(...) ((void)0)
#define ApplyProposedModeSetStateOneApiHeadPreUpdate(d,h,p,w,b) do { if (changeInPre) (w)->modesetUpdateState.windowMappingChanged=TRUE; } while (0)
#define ApplyProposedModeSetStateOneDispFlip(...) event('F')
#define KickoffModesetUpdateState(d,s) do { event('C'); memset(s,0,sizeof(*(s))); } while (0)
#define nvRemoveUnusedHdmiDpAudioDevice(...) ((void)0)
#define ApplyProposedModeSetStateOneApiHeadPostModesetUpdate(...) ((void)0)
#define nvIsUpdateStateEmpty(...) TRUE
'''
post = r'''
int main(void) {
 Hal hal={update}; NVDevEvoRec dev={1,&hal}; Disp disp={&dev};
 NVEvoChannel channel={4}; NVModeSetWorkArea work={0};
 NVProposedModeSetHwState proposed={0}; NVProposedModeSetHwStateOneDisp one={0};
 EvoTrackPhysicalWindowChangeCA(&dev,&channel,1,1,&work.modesetUpdateState);
 assert(!work.modesetUpdateState.windowMappingChanged);
 assert(!work.modesetUpdateState.updateState.noCoreInterlockMask);
 EvoTrackPhysicalWindowChangeCA(&dev,&channel,1,0,&work.modesetUpdateState);
 assert(work.modesetUpdateState.windowMappingChanged);
 assert(work.modesetUpdateState.updateState.noCoreInterlockMask==4);
 puts("PASS: changed physical assignment disables its core interlock; unchanged is untouched");
 memset(&work,0,sizeof(work)); changeInPre=1;
 KickoffProposedModeSetHwState(&disp,&proposed,&one,FALSE,&work);
 assert(n==3 && memcmp(events,"CFU",3)==0);
 puts("PASS: assignment changed during PreUpdate -> core update, then flip update");
 n=0; changeInPre=0; memset(&work,0,sizeof(work));
 KickoffProposedModeSetHwState(&disp,&proposed,&one,FALSE,&work);
 assert(n==2 && memcmp(events,"FC",2)==0);
 puts("PASS: unchanged assignment retains combined flip/core update");
 n=0; work.modesetUpdateState.windowMappingChanged=TRUE;
 KickoffProposedModeSetHwState(&disp,&proposed,&one,FALSE,&work);
 assert(n==3 && memcmp(events,"CFU",3)==0);
 puts("PASS: existing logical mapping change still decouples updates");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    source = Path(tmp)/'test.c'
    exe = Path(tmp)/'test'
    source.write_text(pre+evo[a:b]+modeset[c:d]+post)
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',
                    '-Wno-unused-parameter','-Wno-unused-variable',
                    '-fsanitize=address,undefined',str(source),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
