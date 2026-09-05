#!/usr/bin/env python3
"""Exercise the production CA allocator with a small standalone hardware model."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] /
          'src/nvidia-modeset/src/nvkms-evo4.c').read_text()
start = source.index('static NvBool RequiredScalerTiles(')
end = source.index('static void\nEvoIsModePossibleCA(', start)
modeset = (Path(__file__).resolve().parents[1] /
           'src/nvidia-modeset/src/nvkms-modeset.c').read_text()
move_start = modeset.index('static NvBool IsCurrentMultiTileConfigOneApiHeadIncompatible(')
move_end = modeset.index('\n/*!', move_start)
preamble = r'''
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
#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))
#define nvAssert(x) assert(x)
#define nvPopCount32(x) ((NvU32)__builtin_popcount(x))
#define FOR_EACH_INDEX_IN_MASK(bits, i, mask) for ((i)=0; (i)<(bits); (i)++) if ((mask) & NVBIT(i))
#define FOR_EACH_INDEX_IN_MASK_END
typedef enum { NV_EVO_HW_TILE_TYPE_0, NV_EVO_HW_TILE_TYPE_1,
 NV_EVO_HW_TILE_TYPE_FIRST=0, NV_EVO_HW_TILE_TYPE_LAST=1 } NVEvoHwTileType;
typedef struct { NvU32 tilesMask, phywinsMask[2]; } NVHwHeadMultiTileConfigRec;
struct NvKmsUsageBounds { struct { NvBool usable; } layer[2]; };
typedef struct { struct { struct { NvU32 width,height; } in,out; } viewPort;
 NvU32 yuv420Mode; } NVHwModeTimingsEvo;
typedef struct { struct { NVEvoHwTileType type; } hwTile[8]; } NVEvoCapabilities;
typedef struct { NvU32 numHeads, numHwTiles, numHwPhywins;
 struct { NvU32 numLayers; } head[4];
 struct { NVEvoCapabilities capabilities; } gpus[1]; } NVDevEvoRec;
typedef struct { NvU32 hwHeadsMask; } NVDispApiHeadStateEvoRec;
typedef struct { NVHwHeadMultiTileConfigRec multiTileConfig; } TestHead;
typedef struct { NVDevEvoRec *pDevEvo; NvU32 displayOwner;
 NVDispApiHeadStateEvoRec apiHeadState[4]; TestHead headState[4]; } NVDispEvoRec;
typedef NVDispEvoRec *NVDispEvoPtr;
typedef struct { TestHead head[4]; } NVProposedModeSetHwStateOneDisp;
#define FOR_EACH_EVO_HW_HEAD_IN_MASK(mask, head) FOR_EACH_INDEX_IN_MASK(32, head, mask)
typedef struct { struct { NVHwModeTimingsEvo *pTimings;
 struct NvKmsUsageBounds *pUsage; NvBool modesetRequested;
 NVHwHeadMultiTileConfigRec multiTileConfig; } head[4]; } NVEvoIsModePossibleDispInput;
typedef struct { struct { NVHwHeadMultiTileConfigRec multiTileConfig; } head[4]; } NVEvoIsModePossibleDispOutput;
'''
tests = r'''
static NVDevEvoRec dev;
static NVDispEvoRec disp;
static NVEvoIsModePossibleDispInput input;
static NVHwModeTimingsEvo timings[4];
static struct NvKmsUsageBounds usage[4];
static void setup(void) {
 memset(&dev,0,sizeof(dev)); memset(&input,0,sizeof(input));
 memset(usage,0,sizeof(usage));
 dev.numHeads=4; dev.numHwTiles=8; dev.numHwPhywins=8;
 disp.pDevEvo=&dev; disp.displayOwner=0;
 for (NvU32 h=0;h<4;h++) {
  dev.head[h].numLayers=2;
  input.head[h].pTimings=&timings[h]; input.head[h].pUsage=&usage[h];
  input.head[h].modesetRequested=TRUE;
  input.head[h].multiTileConfig.tilesMask=NVBIT(7-h);
  input.head[h].multiTileConfig.phywinsMask[0]=NVBIT(2*h);
  input.head[h].multiTileConfig.phywinsMask[1]=NVBIT(2*h+1);
  usage[h].layer[0].usable=TRUE;
 }
}
static void checkUnique(NVEvoIsModePossibleDispOutput *out) {
 NvU32 tiles=0, windows=0;
 for (NvU32 h=0;h<4;h++) {
  NVHwHeadMultiTileConfigRec *c=&out->head[h].multiTileConfig;
  assert(!(tiles & c->tilesMask)); tiles |= c->tilesMask;
  for (NvU32 l=0;l<2;l++) {
   assert(!(windows & c->phywinsMask[l])); windows |= c->phywinsMask[l];
   if (usage[h].layer[l].usable)
    assert(nvPopCount32(c->phywinsMask[l])==nvPopCount32(c->tilesMask));
   else assert(c->phywinsMask[l]==0);
  }
 }
}
int main(void) {
 NvU32 required[4]={1,1,2,2};
 NVEvoIsModePossibleDispOutput out={0};
 setup();
 assert(EvoAssignHwHeadMultiTileConfigDispOutputCA(&disp,&input,required,&out));
 checkUnique(&out);
 puts("PASS: four heads, six tiles, unused overlays released");
 setup();
 for (NvU32 h=0;h<4;h++) usage[h].layer[1].usable=TRUE;
 assert(!EvoAssignHwHeadMultiTileConfigDispOutputCA(&disp,&input,required,&out));
 puts("PASS: twelve required physical windows still rejected");
 setup();
 for (NvU32 h=0;h<4;h++) {
  required[h]=1; input.head[h].multiTileConfig.phywinsMask[1]=0;
 }
 usage[0].layer[1].usable=TRUE;
 assert(EvoAssignHwHeadMultiTileConfigDispOutputCA(&disp,&input,required,&out));
 checkUnique(&out);
 puts("PASS: enabling a layer allocates windows without changing tile count");
 setup();
 usage[0].layer[1].usable=TRUE; input.head[0].modesetRequested=FALSE;
 required[2]=2; required[3]=2;
 assert(EvoAssignHwHeadMultiTileConfigDispOutputCA(&disp,&input,required,&out));
 assert(!memcmp(&out.head[0].multiTileConfig,&input.head[0].multiTileConfig,
                sizeof(NVHwHeadMultiTileConfigRec)));
 checkUnique(&out);
 puts("PASS: unchanged head retains its assignments");
 NVProposedModeSetHwStateOneDisp proposed={0};
 memset(disp.headState,0,sizeof(disp.headState));
 disp.apiHeadState[0].hwHeadsMask=1;
 disp.headState[0].multiTileConfig.phywinsMask[1]=2;
 proposed.head[0].multiTileConfig.phywinsMask[1]=2;
 assert(!IsCurrentMultiTileConfigOneApiHeadIncompatible(&disp,0,&proposed));
 proposed.head[0].multiTileConfig.phywinsMask[1]=0;
 proposed.head[0].multiTileConfig.phywinsMask[0]=2;
 assert(IsCurrentMultiTileConfigOneApiHeadIncompatible(&disp,0,&proposed));
 proposed.head[0].multiTileConfig.phywinsMask[0]=0;
 proposed.head[1].multiTileConfig.phywinsMask[0]=2;
 assert(IsCurrentMultiTileConfigOneApiHeadIncompatible(&disp,0,&proposed));
 puts("PASS: window ownership changes require shutdown, retained ownership does not");
 return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / 'test.c'
    exe = Path(tmp) / 'test'
    c.write_text(preamble + source[start:end] + modeset[move_start:move_end] + tests)
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=undefined,address', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
