/* SPDX-License-Identifier: MIT */
#include <cassert>
#include <cstdio>

#include "nvtypes.h"

#define DP_ASSERT(x) assert(x)

enum PowerState { PowerStateD0, PowerStateD3, PowerStateD3AuxOn };
enum { laneCount_0 = 0 };

struct ListElement
{
    ListElement *next = nullptr;
};

struct List
{
    ListElement *first = nullptr;
    ListElement *begin() { return first; }
    ListElement *end() { return nullptr; }
};

struct GroupImpl : ListElement
{
    bool headAttached = true;
    bool headInFirmware = false;
    bool isHeadAttached() { return headAttached; }
};

struct LinkConfiguration
{
    unsigned lanes = 4;
    bool isValid() const { return lanes != 0; }
};

/* The sink's link status, and the copy the library caches between reads. */
struct LinkStatus
{
    bool symbolLock = true;
    bool clockRecovery = true;
    bool channelEqualization = true;
    bool interlaneAlign = true;
};

struct Hal
{
    LinkStatus sink;
    LinkStatus cache;
    bool dirty = false;
    bool dpcdOffline = false;
    PowerState powerState = PowerStateD0;
    unsigned int statusReads = 0;

    bool isDpInTunnelingSupported() { return false; }
    bool isDpcdOffline() { return dpcdOffline; }
    PowerState getPowerState() { return powerState; }
    void setDirtyLinkStatus(bool value) { dirty = value; }
    void refreshLinkStatus()
    {
        if (dirty)
        {
            cache = sink;
            dirty = false;
            statusReads++;
        }
    }
    bool getInterlaneAlignDone() { return cache.interlaneAlign; }
    bool getLaneStatusSymbolLock(unsigned) { return cache.symbolLock; }
    bool getLaneStatusClockRecoveryDone(unsigned) { return cache.clockRecovery; }
    bool getLaneStatusChannelEqualizationDone(unsigned) { return cache.channelEqualization; }
};

struct MainLink
{
    bool isDpTunnelingHwBugWarEnabled() { return false; }
};

struct ConnectorImpl
{
    Hal halStorage;
    MainLink mainStorage;
    Hal *hal = &halStorage;
    MainLink *main = &mainStorage;
    List activeGroups;
    GroupImpl group;
    LinkConfiguration activeLinkConfig;
    bool multistream = false;
    bool linkAwaitingTransition = false;
    bool isLinkQuiesced = false;
    unsigned rmLaneCount = 4;

    ConnectorImpl() { activeGroups.first = &group; }

    bool linkUseMultistream() { return multistream; }
    void getCurrentLinkConfig(unsigned &laneCount, NvU64 &linkRate)
    {
        laneCount = rmLaneCount;
        linkRate = 0;
    }

    GroupImpl * getActiveGroupForSST();
    bool isLinkActive();
    bool isLinkLost();
    bool isActiveSstLinkStillTrained();
};

/* PRODUCTION_FUNCTIONS */

#define CHECK(name, mutate, expected)                                     \
    do {                                                                  \
        ConnectorImpl c;                                                  \
        mutate;                                                           \
        bool result = c.isActiveSstLinkStillTrained();                    \
        if (result != (expected)) {                                       \
            std::fprintf(stderr, "FAIL: %s\n", name);                     \
            return 1;                                                     \
        }                                                                 \
    } while (0)

int main()
{
    /* The 2026-11-13 trigger with a healthy sink: head lit, link trained. */
    CHECK("trained link with an attached head is kept", (void)0, true);

    {
        ConnectorImpl c;
        assert(c.isActiveSstLinkStillTrained());
        assert(c.hal->statusReads == 1);
    }

    /* A power-cycled sink lost its link, whatever the cache still says. */
    CHECK("lost symbol lock", c.hal->sink.symbolLock = false, false);
    CHECK("lost clock recovery", c.hal->sink.clockRecovery = false, false);
    CHECK("lost channel equalization", c.hal->sink.channelEqualization = false, false);
    CHECK("lost interlane alignment", c.hal->sink.interlaneAlign = false, false);

    CHECK("sink in D3", c.hal->powerState = PowerStateD3, false);
    CHECK("sink in D3 with AUX on", c.hal->powerState = PowerStateD3AuxOn, false);
    CHECK("DPCD offline", c.hal->dpcdOffline = true, false);
    CHECK("RM reports no lanes", c.rmLaneCount = 0, false);
    CHECK("no active link", c.activeLinkConfig.lanes = 0, false);

    CHECK("head not attached", c.group.headAttached = false, false);
    CHECK("head still in firmware", c.group.headInFirmware = true, false);
    CHECK("no active group", c.activeGroups.first = nullptr, false);
    CHECK("multistream link", c.multistream = true, false);
    CHECK("link awaiting transition", c.linkAwaitingTransition = true, false);
    CHECK("link quiesced", c.isLinkQuiesced = true, false);

    std::puts("hpd trained link: 16 scenarios pass");
    return 0;
}
