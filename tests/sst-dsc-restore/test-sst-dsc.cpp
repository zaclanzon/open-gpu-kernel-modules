/* SPDX-License-Identifier: MIT */
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

using NvU32 = uint32_t;
using NvU64 = uint64_t;
using NvBool = bool;

static unsigned int assertionCount;
#define DP_ASSERT(condition) do { if (!(condition)) assertionCount++; } while (0)
#define DP_NOTICE 0
#define DP_WARNING 1
template <typename... Args>
static void testPrint(unsigned int, const char *, Args...) {}
#define DP_PRINTF testPrint

/* The production enum is inserted here so mode values cannot drift. */
/* PRODUCTION_DSC_MODE */

enum ConnectorType
{
    connectorDisplayPort,
    connectorHDMI,
    connectorDVI,
    connectorVGA
};

template <typename T>
struct PointerList
{
    std::vector<T *> entries;

    bool contains(T *item) const
    {
        return std::find(entries.begin(), entries.end(), item) != entries.end();
    }

    void insertFront(T *item)
    {
        entries.insert(entries.begin(), item);
    }

    void remove(T *item)
    {
        auto position = std::find(entries.begin(), entries.end(), item);
        assert(position != entries.end());
        entries.erase(position);
    }

    T *next(T *previous) const
    {
        auto position = std::find(entries.begin(), entries.end(), previous);
        if (position == entries.end())
        {
            return entries.empty() ? nullptr : entries.front();
        }
        return ++position == entries.end() ? nullptr : *position;
    }
};

struct GroupImpl;
struct Device {};

struct DeviceImpl : Device
{
    GroupImpl *activeGroup = nullptr;
    DeviceImpl *parent = nullptr;
    ConnectorType connectorType = connectorDisplayPort;
    bool dscPossible = true;
    bool sinkEnabled = false;
    bool failConfiguration = false;
    unsigned int configurationCount = 0;

    bool isDSCPossible() const { return dscPossible; }
    ConnectorType getConnectorType() const { return connectorType; }

    bool getDscEnable(bool *enabled)
    {
        *enabled = sinkEnabled;
        return dscPossible;
    }

    bool setDscEnable(bool enabled)
    {
        configurationCount++;
        if (failConfiguration)
        {
            return false;
        }
        sinkEnabled = enabled;
        return true;
    }
};

/* HDCP and stream allocation are outside this test; neither is activated. */
struct ListElement { ListElement *next = nullptr; };
struct EmptyGroupList
{
    ListElement *begin() { return nullptr; }
    ListElement *end() { return nullptr; }
};
struct LinkConfiguration { bool bIs128b132bChannelCoding = false; };
struct MainLink
{
    void configureAndTriggerECF(NvU64) { assert(false); }
};

struct ConnectorImpl
{
    bool multistream = false;
    PointerList<Device> dscEnabledDevices;
    EmptyGroupList activeGroups;
    MainLink *main = nullptr;

    bool linkUseMultistream() const { return multistream; }
    LinkConfiguration getActiveLinkConfig() const { return {}; }
    bool setDeviceDscState(Device *dev, bool enable);
};

struct GroupImpl : ListElement
{
    ConnectorImpl *parent;
    PointerList<Device> members;
    bool headInFirmware = false;
    bool headAttached = true;
    bool hdcpEnabled = false;
    DSC_MODE dscModeActive = DSC_SINGLE;
    NvU32 headIndex = 2;
    struct { unsigned int count = 0; unsigned int begin = 0; } timeslot;
    unsigned int updateCount = 0;
    bool enabledAtUpdate = false;

    explicit GroupImpl(ConnectorImpl *connector) : parent(connector) {}
    bool isHeadAttached() const { return headAttached; }
    void insert(Device *dev);
    void remove(Device *dev);

    void update(Device *dev, bool allocated)
    {
        assert(members.contains(dev) == allocated);
        updateCount++;
        enabledAtUpdate = static_cast<DeviceImpl *>(dev)->sinkEnabled;
    }

    void updateVbiosScratchRegister(Device *) {}
};

/* PRODUCTION_FUNCTIONS */

struct Fixture
{
    ConnectorImpl connector;
    GroupImpl group{&connector};
    DeviceImpl device;

    Fixture() { assertionCount = 0; }
};

static void testLostStateOnReinsertion()
{
    Fixture f;
    f.device.sinkEnabled = true;
    f.group.insert(&f.device);
    f.group.remove(&f.device);
    assert(!f.connector.dscEnabledDevices.contains(&f.device));
    assert(f.device.activeGroup == nullptr);

    f.device.sinkEnabled = false;
    f.device.configurationCount = 0;
    f.group.insert(&f.device);

    assert(f.device.sinkEnabled);
    assert(f.device.configurationCount == 1);
    assert(f.device.activeGroup == &f.group);
    assert(f.group.members.entries.size() == 1);
    assert(f.connector.dscEnabledDevices.entries.size() == 1);
    assert(f.group.enabledAtUpdate);
    assert(assertionCount == 0);
}

static void testEnabledSinkRegainsTrackingEntry()
{
    Fixture f;
    f.device.sinkEnabled = true;
    f.group.insert(&f.device);
    assert(f.device.sinkEnabled);
    assert(f.connector.dscEnabledDevices.contains(&f.device));
    assert(assertionCount == 0);
}

static void testModesAndConnectorTypes()
{
    for (DSC_MODE mode : {DSC_SINGLE, DSC_DUAL, DSC_DROP, DSC_MODE_NONE})
    {
        for (ConnectorType type : {connectorDisplayPort, connectorHDMI,
                                   connectorDVI, connectorVGA})
        {
            Fixture f;
            f.group.dscModeActive = mode;
            f.device.connectorType = type;
            const bool expected = type == connectorDisplayPort &&
                (mode == DSC_SINGLE || mode == DSC_DUAL);
            f.group.insert(&f.device);
            assert(f.device.sinkEnabled == expected);
            assert(f.device.configurationCount == (expected ? 1U : 0U));
            assert(f.connector.dscEnabledDevices.contains(&f.device) == expected);
            assert(assertionCount == 0);
        }
    }
}

static void testInactiveMstAndIncapableDevices()
{
    Fixture inactive;
    inactive.group.headAttached = false;
    inactive.group.insert(&inactive.device);
    assert(inactive.device.activeGroup == nullptr);
    assert(inactive.device.configurationCount == 0);

    Fixture mst;
    mst.connector.multistream = true;
    mst.group.insert(&mst.device);
    assert(mst.device.configurationCount == 0);

    Fixture incapable;
    incapable.device.dscPossible = false;
    incapable.group.insert(&incapable.device);
    assert(incapable.device.configurationCount == 0);
    assert(assertionCount == 0);
}

static void testConflictingActiveGroup()
{
    Fixture f;
    GroupImpl otherGroup(&f.connector);
    f.device.activeGroup = &otherGroup;
    f.group.insert(&f.device);
    assert(assertionCount == 1);
    assert(f.device.activeGroup == &otherGroup);
    assert(f.device.configurationCount == 0);
    assert(f.group.members.entries.empty());
    assert(f.group.updateCount == 0);
}

static void testFailedConfigurationCanBeRetried()
{
    Fixture f;
    f.device.failConfiguration = true;
    f.group.insert(&f.device);
    assert(!f.device.sinkEnabled);
    assert(!f.connector.dscEnabledDevices.contains(&f.device));
    assert(f.group.members.contains(&f.device));
    assert(assertionCount == 1); // Existing setter reports configuration failure.

    f.group.remove(&f.device);
    f.device.failConfiguration = false;
    f.group.insert(&f.device);
    assert(f.device.sinkEnabled);
    assert(f.connector.dscEnabledDevices.contains(&f.device));
    assert(f.device.configurationCount == 2);
    assert(assertionCount == 1);
}

int main()
{
    const struct { const char *name; void (*run)(); } tests[] = {
        {"lost DSC state on reinsertion", testLostStateOnReinsertion},
        {"enabled sink regains tracking", testEnabledSinkRegainsTrackingEntry},
        {"DSC modes and connector types", testModesAndConnectorTypes},
        {"inactive, MST and incapable devices", testInactiveMstAndIncapableDevices},
        {"conflicting active group", testConflictingActiveGroup},
        {"failed configuration followed by retry", testFailedConfigurationCanBeRetried},
    };
    setvbuf(stdout, nullptr, _IONBF, 0);
    for (const auto &test : tests)
    {
        printf("RUN SST DSC: %s\n", test.name);
        test.run();
    }
    puts("PASS SST DSC restoration scenarios");
}
