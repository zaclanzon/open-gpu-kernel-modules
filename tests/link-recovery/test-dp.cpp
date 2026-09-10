/* SPDX-License-Identifier: MIT */
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <climits>
using NvU32 = uint32_t;
using NvBool = bool;
#define NVBIT(n) (1U << (n))
#define NV_MAX_HEADS 4
#define DP_ASSERT(x) assert(x)
#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))
struct Group { NvU32 index; };
struct DpModesetParams { NvU32 headIndex; };
struct DpPreModesetParams {
    NvU32 headMask;
    struct { Group *pTarget; DpModesetParams *pModesetParams; } head[NV_MAX_HEADS];
};
struct ConnectorImpl {
    bool bFECEnable = false;
    NvU32 inTransitionHeadMask = 0, failures = 0, attached = 0, detached = 0;
    NvU32 attachEnds = 0, detachEnds = 0;
    Group *perHeadAttachedGroup[NV_MAX_HEADS] = {};
    bool needToEnableFEC(const DpPreModesetParams &) { return true; }
    bool notifyAttachBegin(Group *g, const DpModesetParams &p) {
        assert(g->index == p.headIndex);
        attached |= NVBIT(p.headIndex);
        return !(failures & NVBIT(p.headIndex));
    }
    void notifyDetachBegin(Group *g) { detached |= NVBIT(g->index); }
    void notifyAttachEnd(bool value) { assert(!value); attachEnds++; }
    void notifyDetachEnd() { detachEnds++; }
    NvU32 dpPreModeset(const DpPreModesetParams &);
    void dpPostModeset(void);
};
struct NVDispEvoRec { struct { NvU32 activeDpys; } apiHeadState[4]; };
struct NVDpyEvoRec { NVDispEvoRec *pDispEvo; NvU32 apiHead, id; };
#define NVKMS_EVENT_TYPE_DPY_LINK_RECOVERY 6
static unsigned int events;
static void nvSendDpyEventEvo(const NVDpyEvoRec *dpy, NvU32 type) {
    assert(dpy && type == NVKMS_EVENT_TYPE_DPY_LINK_RECOVERY); events++;
}
/* PRODUCTION_FUNCTIONS */
int main()
{
    Group groups[4] = {{0}, {1}, {2}, {3}};
    DpModesetParams modes[4] = {{0}, {1}, {2}, {3}};
    for (NvU32 selected = 0; selected < 16; selected++) {
        for (NvU32 attach = 0; attach < 16; attach++) {
            for (NvU32 failed = 0; failed < 16; failed++) {
                ConnectorImpl c;
                DpPreModesetParams params = {};
                params.headMask = selected;
                c.failures = failed;
                for (NvU32 i = 0; i < 4; i++) {
                    c.perHeadAttachedGroup[i] = &groups[i];
                    params.head[i].pTarget = (attach & NVBIT(i)) ? &groups[i] : nullptr;
                    params.head[i].pModesetParams = &modes[i];
                }
                assert(c.dpPreModeset(params) == (selected & attach & failed));
                assert(c.attached == (selected & attach));
                assert(c.detached == (selected & ~attach));
                assert(c.inTransitionHeadMask == selected && c.bFECEnable);
                c.dpPostModeset();
                assert(c.inTransitionHeadMask == 0);
                assert(c.attachEnds == (NvU32)__builtin_popcount(selected & attach));
                assert(c.detachEnds == (NvU32)__builtin_popcount(selected & ~attach));
            }
        }
    }
    puts("PASS all 4096 DP head/attach/failure combinations preserve pre/post bracketing");
    NVDispEvoRec disp = {};
    NVDpyEvoRec dpy = {&disp, UINT_MAX, 2};
    nvSendDpyLinkRecoveryEventEvo(nullptr);
    nvSendDpyLinkRecoveryEventEvo(&dpy);
    assert(events == 1); /* Recovery intent must survive a temporarily invalid head. */
    dpy.apiHead = 1;
    nvSendDpyLinkRecoveryEventEvo(&dpy);
    disp.apiHeadState[1].activeDpys = NVBIT(1);
    nvSendDpyLinkRecoveryEventEvo(&dpy);
    assert(events == 3);
    disp.apiHeadState[1].activeDpys |= NVBIT(2);
    nvSendDpyLinkRecoveryEventEvo(&dpy);
    assert(events == 4);
    puts("PASS recovery events survive invalid, empty and changing hardware head assignments");
}
