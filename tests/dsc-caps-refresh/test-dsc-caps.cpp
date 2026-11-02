/* SPDX-License-Identifier: MIT */
#include <cassert>
#include <cstdio>
#include <deque>

#include "nvtypes.h"
#include "nvmisc.h"
#include "displayport/dpcd14.h"
#include "displayport/dpcd20.h"

#define DP_USED(x) ((void)(x))
#define DP_ERROR 0
#define DP_NOTICE 1
template <typename... Args>
static void testPrint(unsigned int, const char *, Args...) {}
#define DP_PRINTF testPrint

enum { NakUndefined, NakBadParam };
enum PeerDevice { None, Dongle };

struct AuxBus
{
    enum status { success, nack, defer };
};

struct Timer
{
    unsigned int sleeps = 0;
    unsigned int sleptMs = 0;
    void sleep(unsigned int ms) { sleeps++; sleptMs += ms; }
};

struct Address
{
    typedef char StringBuffer[64];
    const char *toString(StringBuffer &) const { return "sink"; }
};

struct DscCaps
{
    bool bDSCSupported;
    bool bDSCDecompressionSupported;
    bool bDSCPassThroughSupported;
};

struct ConnectorImpl;

struct DeviceImpl
{
    ConnectorImpl *connector = nullptr;
    Address address;
    DscCaps dscCaps = {};
    PeerDevice peerDevice = None;
    unsigned int dpcdRevisionMajor = 1;
    bool fakedMux = false;
    bool multistream = false;
    bool videoSink = true;
    bool audioSink = false;
    struct { struct { bool bIgnoreDscCap = false; } WARFlags; } processedEdid;
    bool bDSCPossible = false;

    /* Scripted DSC_SUPPORT reads: the byte returned, or -1 for a failed read. */
    std::deque<int> dscSupportReads;
    unsigned int dpcdReads = 0;
    unsigned int capsParses = 0;
    unsigned int branchParses = 0;
    unsigned int decompressionDecisions = 0;
    unsigned int fecReads = 0;
    unsigned int resets = 0;

    AuxBus::status getDpcdData(unsigned int offset, NvU8 *buffer, unsigned int size,
                               unsigned int *done, unsigned int *nakReason)
    {
        assert(offset == NV_DPCD14_DSC_SUPPORT && size == 1);
        dpcdReads++;
        const int reply = dscSupportReads.empty() ? -1 : dscSupportReads.front();
        if (!dscSupportReads.empty())
        {
            dscSupportReads.pop_front();
        }
        if (reply < 0)
        {
            *nakReason = NakUndefined;
            return AuxBus::nack;
        }
        *buffer = static_cast<NvU8>(reply);
        *done = 1;
        return AuxBus::success;
    }

    bool isFakedMuxDevice() const { return fakedMux; }
    NvBool getDSCSupport();
    bool readAndParseDSCCaps() { capsParses++; return true; }
    void readAndParseBranchSpecificDSCCaps() { branchParses++; }
    bool isVideoSink() const { return videoSink; }
    bool isAudioSink() const { return audioSink; }
    bool isMultistream() const { return multistream; }
    NvBool isDSCSupported() const { return dscCaps.bDSCSupported; }
    bool getFECSupport() { fecReads++; return true; }

    void setDscDecompressionDevice(bool)
    {
        decompressionDecisions++;
        bDSCPossible = dscCaps.bDSCDecompressionSupported;
    }

    void resetDscAndFecCaps()
    {
        resets++;
        dscCaps = {};
        bDSCPossible = false;
    }
};

struct MainLink
{
    bool gpuDsc = true;
    void getDscCaps(bool *supported) { *supported = gpuDsc; }
};

struct ConnectorImpl
{
    Timer *timer = nullptr;
    MainLink *main = nullptr;
    bool bDscCapBasedOnParent = false;
    bool linkAwaitingTransition = false;
    bool tbt3 = false;
    bool fec = true;

    bool isInternalDpTunnelTbt3Downstream() const { return tbt3; }
    bool isFECSupported() const { return fec; }
    void readDeviceDscCaps(DeviceImpl *dev);
    void refreshReturningDeviceCaps(DeviceImpl *dev);
};

/* PRODUCTION_FUNCTIONS */

struct Fixture
{
    Timer timer;
    MainLink main;
    ConnectorImpl connector;
    DeviceImpl device;

    Fixture()
    {
        connector.timer = &timer;
        connector.main = &main;
        device.connector = &connector;
    }
};

static void run(const char *name, void (*test)())
{
    std::printf("RUN DSC caps: %s\n", name);
    test();
}

int main()
{
    run("a failed read is retried", [] {
        Fixture f;
        f.device.dscSupportReads = {-1, 0x01};
        assert(f.device.getDSCSupport());
        assert(f.device.dpcdReads == 2);
        assert(f.timer.sleeps == 1 && f.timer.sleptMs == DSC_SUPPORT_READ_RETRY_DELAY_MS);
    });

    run("persistent failures stop after the retry budget", [] {
        Fixture f;
        assert(!f.device.getDSCSupport());
        assert(f.device.dpcdReads == DSC_SUPPORT_READ_RETRIES + 1);
        assert(f.timer.sleeps == DSC_SUPPORT_READ_RETRIES);
    });

    run("devices without reachable DPCD are not retried", [] {
        Fixture mux;
        mux.device.fakedMux = true;
        assert(!mux.device.getDSCSupport());
        assert(mux.device.dpcdReads == 1 && mux.timer.sleeps == 0);

        Fixture dongle;
        dongle.device.peerDevice = Dongle;
        dongle.device.dpcdRevisionMajor = 0;
        assert(!dongle.device.getDSCSupport());
        assert(dongle.device.dpcdReads == 1 && dongle.timer.sleeps == 0);
    });

    run("a successful read is not retried", [] {
        Fixture none;
        none.device.dscSupportReads = {0x00};
        assert(!none.device.getDSCSupport());
        assert(none.device.dpcdReads == 1 && none.timer.sleeps == 0);

        Fixture passThrough;
        passThrough.device.dscSupportReads = {0x02};
        assert(passThrough.device.getDSCSupport());
        assert(passThrough.device.dscCaps.bDSCPassThroughSupported);
    });

    run("reading caps parses and decides DSC", [] {
        Fixture sink;
        sink.device.dscSupportReads = {0x01};
        sink.connector.readDeviceDscCaps(&sink.device);
        assert(sink.device.capsParses == 1 && sink.device.branchParses == 0);
        assert(sink.device.decompressionDecisions == 1 && sink.device.bDSCPossible);

        Fixture branch;
        branch.device.videoSink = false;
        branch.device.dscSupportReads = {0x01};
        branch.connector.readDeviceDscCaps(&branch.device);
        assert(branch.device.branchParses == 1);
    });

    run("GPU without DSC, TBT3 and EDID override paths", [] {
        Fixture gpu;
        gpu.main.gpuDsc = false;
        gpu.connector.readDeviceDscCaps(&gpu.device);
        assert(gpu.device.dpcdReads == 0 && gpu.device.decompressionDecisions == 0);

        Fixture tbt3;
        tbt3.connector.tbt3 = true;
        tbt3.connector.readDeviceDscCaps(&tbt3.device);
        assert(tbt3.device.resets == 1 && tbt3.device.dpcdReads == 0);

        Fixture ignore;
        ignore.device.processedEdid.WARFlags.bIgnoreDscCap = true;
        ignore.device.dscSupportReads = {0x01};
        ignore.connector.readDeviceDscCaps(&ignore.device);
        assert(ignore.device.decompressionDecisions == 0 && !ignore.device.bDSCPossible);
    });

    run("a returning SST sink with stale caps regains DSC", [] {
        Fixture f;
        /* Discovery while the sink is still waking: every read fails. */
        f.connector.readDeviceDscCaps(&f.device);
        assert(!f.device.isDSCSupported() && !f.device.bDSCPossible);

        /* The same sink returns with an identical EDID (complete match). */
        f.device.dscSupportReads = {0x01};
        f.connector.refreshReturningDeviceCaps(&f.device);
        assert(f.device.isDSCSupported() && f.device.bDSCPossible);
        assert(f.device.fecReads == 1);
    });

    run("returning devices that must keep their caps", [] {
        Fixture dsc;
        dsc.device.dscCaps.bDSCSupported = true;
        dsc.device.dscCaps.bDSCDecompressionSupported = true;
        dsc.device.bDSCPossible = true;
        dsc.connector.refreshReturningDeviceCaps(&dsc.device);
        assert(dsc.device.dpcdReads == 0 && dsc.device.fecReads == 0 && dsc.device.bDSCPossible);

        Fixture mst;
        mst.device.multistream = true;
        mst.connector.refreshReturningDeviceCaps(&mst.device);
        assert(mst.device.dpcdReads == 0);

        Fixture transition;
        transition.connector.linkAwaitingTransition = true;
        transition.connector.refreshReturningDeviceCaps(&transition.device);
        assert(transition.device.dpcdReads == 0);

        Fixture tbt3;
        tbt3.connector.tbt3 = true;
        tbt3.connector.refreshReturningDeviceCaps(&tbt3.device);
        assert(tbt3.device.resets == 1 && tbt3.device.fecReads == 0);
    });

    std::printf("PASS DSC caps refresh scenarios\n");
    return 0;
}
