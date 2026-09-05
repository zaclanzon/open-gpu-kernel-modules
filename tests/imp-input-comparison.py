#!/usr/bin/env python3
"""Exercise the production comparison helper with a fake RM validator."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'src/nvidia-modeset/src/nvkms-evo4.c').read_text()
a=s.index('static NvBool EvoCompareDscImpInputsCA(');b=s.index('static void\nEvoIsModePossibleCA(',a)
pre=r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef unsigned NvU32; typedef int NvBool;
#define TRUE 1
#define FALSE 0
#define NV_OK 0
#define EVO_LOG_INFO 0
#define NV_DSC_INFO_EVO_TYPE_DP 3
#define NV_DSC_INFO_EVO_TYPE_HDMI 1
#define NVC372_CTRL_CMD_IS_MODE_POSSIBLE 1
#define ARRAY_LEN(x) (sizeof(x)/sizeof((x)[0]))
#define nvkms_memcpy memcpy
#define nvFree free
#define nvEvoLogDev(...) ((void)0)
static int debug=1,failAlloc=0,calls=0;
#define nvDoDebugLogging() debug
static void *nvAlloc(size_t n) { return failAlloc?NULL:malloc(n); }
typedef struct { int type; union { struct { NvU32 bitsPerPixelX16; } dp,hdmi; }; } NVDscInfoEvoRec;
typedef struct { int rmCtrlHandle; } Dev; typedef Dev *NVDevEvoPtr;
typedef struct { Dev *pDevEvo; } Disp; typedef Disp *NVDispEvoPtr;
static struct { int clientHandle; } nvEvoGlobal;
typedef struct { struct { const NVDscInfoEvoRec *pDscInfo; } head[4]; } NVEvoIsModePossibleDispInput;
typedef struct { unsigned numHeads,numWindows; struct { unsigned headIndex,bEnableDsc,maxPixelClkKHz,dscTargetBppX16,possibleDscSliceCountMask; } head[4]; unsigned bIsPossible,numTilingAssignments,dispClkKHz; struct { unsigned numTiles; } tilingAssignments[8]; struct { unsigned head,headDscSlices; } tileList[8]; } NVC372_CTRL_IS_MODE_POSSIBLE_PARAMS;
static unsigned targets[2][4];
static unsigned nvRmApiControl(int c,int h,int cmd,NVC372_CTRL_IS_MODE_POSSIBLE_PARAMS *p,size_t size) {
 (void)c;(void)h;(void)cmd;(void)size;
 assert(calls<2);for(unsigned i=0;i<4;i++) targets[calls][i]=p->head[i].dscTargetBppX16;
 calls++;p->bIsPossible=1;p->numTilingAssignments=1;p->tilingAssignments[0].numTiles=6;
 return 0;
}
'''
post=r'''
int main(void) {
 Dev dev={0};Disp disp={&dev};NVEvoIsModePossibleDispInput input={0};
 NVDscInfoEvoRec dsc[4]={{0},{0},{.type=3,.dp={177}},{.type=1,.hdmi={192}}};
 NVC372_CTRL_IS_MODE_POSSIBLE_PARAMS p={0},original;
 p.numHeads=4;p.numWindows=5;
 for(unsigned i=0;i<4;i++){p.head[i].headIndex=i;input.head[i].pDscInfo=&dsc[i];}
 for(unsigned i=2;i<4;i++){p.head[i].bEnableDsc=1;p.head[i].maxPixelClkKHz=2200000;p.head[i].possibleDscSliceCountMask=128;}
 original=p;
 assert(EvoCompareDscImpInputsCA(&disp,&input,&p));
 assert(calls==2 && !memcmp(&p,&original,sizeof(p)));
 assert(targets[0][2]==0 && targets[0][3]==0);
 assert(targets[1][2]==177 && targets[1][3]==192 && targets[1][0]==0);
 puts("PASS: compare zero and negotiated inputs, preserve original, veto even when RM accepts");
 calls=0;debug=0;assert(!EvoCompareDscImpInputsCA(&disp,&input,&p));assert(calls==0);
 debug=1;p.head[2].maxPixelClkKHz=1100000;assert(!EvoCompareDscImpInputsCA(&disp,&input,&p));assert(calls==0);p=original;
 puts("PASS: debugging disabled and baseline clocks leave normal validation untouched");
 failAlloc=1;assert(EvoCompareDscImpInputsCA(&disp,&input,&p));assert(calls==0);failAlloc=0;
 dsc[2].dp.bitsPerPixelX16=0;assert(EvoCompareDscImpInputsCA(&disp,&input,&p));assert(calls==1);
 puts("PASS: allocation failure and missing DSC bitrate both veto the target");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'test.c';exe=Path(tmp)/'test';p.write_text(pre+s[a:b]+post)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
