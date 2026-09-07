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
struct Dev;
typedef struct {
    void (*Update)(struct Dev *, const NVEvoUpdateState *, NvBool);
} Hal;
typedef struct Dev {
    NvU32 numApiHeads;
    Hal *hal;
} NVDevEvoRec;
typedef struct {
    NVDevEvoRec *pDevEvo;
} Disp;
typedef Disp *NVDispEvoPtr;
typedef struct {
    int dummy;
} NVProposedModeSetHwState;
typedef struct {
    int apiHead[4];
} NVProposedModeSetHwStateOneDisp;
typedef struct {
    NVEvoModesetUpdateState modesetUpdateState;
} NVModeSetWorkArea;
static void nvDisableCoreInterlockUpdateState(NVDevEvoRec *dev, NVEvoUpdateState *u,
                                              const NVEvoChannel *ch)
{
    (void)dev;
    u->noCoreInterlockMask |= ch->channelMask;
}
static char events[16];
static int n, changeInPre;
static void event(char c)
{
    events[n++] = c;
}
static void update(NVDevEvoRec *dev, const NVEvoUpdateState *u, NvBool release)
{
    (void)dev;
    (void)u;
    (void)release;
    event('U');
}
#define ApplyProposedModeSetStateOneApiHeadShutDown(...) ((void)0)
#define ApplyProposedModeSetStateOneApiHeadPreUpdate(d, h, p, w, b)                                \
    do {                                                                                           \
        if (changeInPre)                                                                           \
            (w)->modesetUpdateState.windowMappingChanged = TRUE;                                   \
    } while (0)
#define ApplyProposedModeSetStateOneDispFlip(...) event('F')
#define KickoffModesetUpdateState(d, s)                                                            \
    do {                                                                                           \
        event('C');                                                                                \
        memset(s, 0, sizeof(*(s)));                                                                \
    } while (0)
#define nvRemoveUnusedHdmiDpAudioDevice(...) ((void)0)
#define ApplyProposedModeSetStateOneApiHeadPostModesetUpdate(...) ((void)0)
#define nvIsUpdateStateEmpty(...) TRUE
