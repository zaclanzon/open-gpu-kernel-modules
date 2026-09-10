/* SPDX-License-Identifier: MIT */
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstdio>

using NvU32 = uint32_t;
using NvBool = bool;

#define NVBIT(n) (1U << (n))
#define NV_MAX_HEADS 4
#define DP_ASSERT(condition) assert(condition)
#define ARRAY_LEN(array) (sizeof(array) / sizeof((array)[0]))

/* Minimal DP objects required by the production pre/post modeset methods. */
struct Group
{
    NvU32 headIndex;
};

struct DpModesetParams
{
    NvU32 headIndex;
};

struct DpPreModesetParams
{
    NvU32 headMask;
    struct
    {
        Group *pTarget;
        DpModesetParams *pModesetParams;
    } head[NV_MAX_HEADS];
};

struct ConnectorImpl
{
    bool bFECEnable = false;
    NvU32 inTransitionHeadMask = 0;
    Group *perHeadAttachedGroup[NV_MAX_HEADS] = {};

    NvU32 attachFailureMask = 0;
    NvU32 attachBeginMask = 0;
    NvU32 detachBeginMask = 0;
    NvU32 attachEndCount = 0;
    NvU32 detachEndCount = 0;

    bool needToEnableFEC(const DpPreModesetParams &)
    {
        return true;
    }

    bool notifyAttachBegin(Group *pGroup, const DpModesetParams &params)
    {
        assert(pGroup->headIndex == params.headIndex);
        attachBeginMask |= NVBIT(params.headIndex);
        return (attachFailureMask & NVBIT(params.headIndex)) == 0;
    }

    void notifyDetachBegin(Group *pGroup)
    {
        detachBeginMask |= NVBIT(pGroup->headIndex);
    }

    void notifyAttachEnd(bool force)
    {
        assert(!force);
        attachEndCount++;
    }

    void notifyDetachEnd()
    {
        detachEndCount++;
    }

    NvU32 dpPreModeset(const DpPreModesetParams &params);
    void dpPostModeset(void);
};

struct NVDispEvoRec
{
    struct
    {
        NvU32 activeDpys;
    } apiHeadState[NV_MAX_HEADS];
};

struct NVDpyEvoRec
{
    NVDispEvoRec *pDispEvo;
    NvU32 apiHead;
    NvU32 id;
};

#define NVKMS_EVENT_TYPE_DPY_LINK_RECOVERY 6

static unsigned int recoveryEventCount;

static void nvSendDpyEventEvo(const NVDpyEvoRec *pDpyEvo, NvU32 eventType)
{
    assert(pDpyEvo != NULL);
    assert(eventType == NVKMS_EVENT_TYPE_DPY_LINK_RECOVERY);
    recoveryEventCount++;
}

/* PRODUCTION_FUNCTIONS */

static void testPrePostModesetNotifications()
{
    const NvU32 maskCount = NVBIT(NV_MAX_HEADS);
    Group groups[NV_MAX_HEADS];
    DpModesetParams modesetParams[NV_MAX_HEADS];
    unsigned int combinationCount = 0;

    for (NvU32 head = 0; head < NV_MAX_HEADS; head++)
    {
        groups[head].headIndex = head;
        modesetParams[head].headIndex = head;
    }

    for (NvU32 selectedHeads = 0; selectedHeads < maskCount; selectedHeads++)
    {
        for (NvU32 attachingHeads = 0; attachingHeads < maskCount;
             attachingHeads++)
        {
            for (NvU32 failedHeads = 0; failedHeads < maskCount; failedHeads++)
            {
                ConnectorImpl connector;
                DpPreModesetParams params = {};
                const NvU32 expectedAttachMask = selectedHeads & attachingHeads;
                const NvU32 expectedDetachMask =
                    selectedHeads & ~attachingHeads;
                const NvU32 expectedFailureMask =
                    expectedAttachMask & failedHeads;

                params.headMask = selectedHeads;
                connector.attachFailureMask = failedHeads;
                for (NvU32 head = 0; head < NV_MAX_HEADS; head++)
                {
                    connector.perHeadAttachedGroup[head] = &groups[head];
                    params.head[head].pTarget =
                        (attachingHeads & NVBIT(head)) ? &groups[head] : NULL;
                    params.head[head].pModesetParams = &modesetParams[head];
                }

                assert(connector.dpPreModeset(params) == expectedFailureMask);
                assert(connector.attachBeginMask == expectedAttachMask);
                assert(connector.detachBeginMask == expectedDetachMask);
                assert(connector.inTransitionHeadMask == selectedHeads);
                assert(connector.bFECEnable);

                // Failed attach preparation still needs its matching end call.
                connector.dpPostModeset();
                assert(connector.inTransitionHeadMask == 0);
                assert(connector.attachEndCount ==
                       (NvU32)__builtin_popcount(expectedAttachMask));
                assert(connector.detachEndCount ==
                       (NvU32)__builtin_popcount(expectedDetachMask));
                combinationCount++;
            }
        }
    }

    assert(combinationCount == 4096);
    puts("PASS all 4096 DP head combinations preserve pre/post notifications");
}

static void testRecoveryEventWithTransientHeadAssignment()
{
    NVDispEvoRec disp = {};
    NVDpyEvoRec dpy = { &disp, UINT_MAX, 2 };

    recoveryEventCount = 0;
    nvSendDpyLinkRecoveryEventEvo(NULL);
    assert(recoveryEventCount == 0);

    // A sink can return before it has a valid hardware head assignment.
    nvSendDpyLinkRecoveryEventEvo(&dpy);
    assert(recoveryEventCount == 1);

    dpy.apiHead = 1;
    nvSendDpyLinkRecoveryEventEvo(&dpy);
    assert(recoveryEventCount == 2);

    // A head assigned to another display must not drop the recovery event.
    disp.apiHeadState[1].activeDpys = NVBIT(1);
    nvSendDpyLinkRecoveryEventEvo(&dpy);
    assert(recoveryEventCount == 3);

    disp.apiHeadState[1].activeDpys |= NVBIT(dpy.id);
    nvSendDpyLinkRecoveryEventEvo(&dpy);
    assert(recoveryEventCount == 4);
    puts("PASS recovery events survive transient hardware head assignments");
}

int main()
{
    testPrePostModesetNotifications();
    testRecoveryEventWithTransientHeadAssignment();
    return 0;
}
