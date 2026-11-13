/*
 * SPDX-FileCopyrightText: Copyright (c) 2011 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

// This file implements the EVO RM interface used by the DisplayPort library.

#include "nvkms-utils.h"

#include "nvdp-evo-interface.hpp"

#include "nvkms-rmapi.h"

#include "nvdp-connector-event-sink.hpp"

#include <ctrl/ctrlc370/ctrlc370or.h>

namespace nvkmsDisplayPort {

EvoInterface::EvoInterface(NVConnectorEvoPtr pConnectorEvo)
    : pConnectorEvo(pConnectorEvo)
{
}

//
// z-trace: the DP library calls RM with the nvkms lock held, and RM holds its
// API lock until GSP answers. Log every call that is slow enough to stall the
// desktop, and every flush mode change, with its status.
//
static NvU32 TimedRmControl(const NVDispEvoRec *pDispEvo, NvU32 hObject,
                            NvU32 command, void *params, NvU32 paramSize)
{
    const NvU64 slowUsec = 100000;
    const NvU64 startUsec = nvkms_get_usec();
    const NvU32 status = nvRmApiControl(nvEvoGlobal.clientHandle, hObject,
                                        command, params, paramSize);
    const NvU64 usec = nvkms_get_usec() - startUsec;

    if (usec >= slowUsec || command == NVC370_CTRL_CMD_SET_SOR_FLUSH_MODE ||
        command == NV5070_CTRL_CMD_SET_SOR_FLUSH_MODE) {
        nvEvoLogDisp(pDispEvo, EVO_LOG_INFO,
                     "z-trace: DP rm control 0x%08x took %u ms status 0x%x",
                     command, (NvU32)(usec / 1000), status);
    }

    return status;
}

NvU32 EvoInterface::rmControl0073(NvU32 command, void * params,
                                  NvU32 paramSize)
{
    const NVDispEvoRec *pDispEvo = pConnectorEvo->pDispEvo;

    return TimedRmControl(pDispEvo, pDispEvo->pDevEvo->displayCommonHandle,
                          command, params, paramSize);
}

NvU32 EvoInterface::rmControl5070(NvU32 command, void * params,
                                  NvU32 paramSize)
{
    const NVDispEvoRec *pDispEvo = pConnectorEvo->pDispEvo;

    return TimedRmControl(pDispEvo, pDispEvo->pDevEvo->displayHandle,
                          command, params, paramSize);
}

/*!
 * Look up the value of a particular key in the DisplayPort-specific registry
 * corresponding to this connector.  These values are provided at device
 * allocation time, copied from the client request during nvAllocDevEvo().
 *
 * \param[in]     key       The name of the key to look up.
 *
 * \return        The unsigned 32-bit value set for the key, or 0 if the key is
 *                not set.
 */
NvU32 EvoInterface::getRegkeyValue(const char *key)
{
    NVDevEvoPtr pDevEvo = pConnectorEvo->pDispEvo->pDevEvo;
    NvU32 val;
    NvBool found = nvGetRegkeyValue(pDevEvo, key, &val);

    if (found) {
        return val;
    } else {
        return 0;
    }
}

bool EvoInterface::isInbandStereoSignalingSupported()
{
    NVDispEvoPtr pDispEvo = pConnectorEvo->pDispEvo;
    NVDpyEvoPtr pDpyEvo;

    FOR_ALL_EVO_DPYS(pDpyEvo, pDispEvo->validDisplays, pDispEvo) {
        if ((pDpyEvo->pConnectorEvo == pConnectorEvo) &&
            pDpyEvo->dp.inbandStereoSignaling) {
            return TRUE;
        }
    }

    return FALSE;
}

NvU32 EvoInterface::getSubdeviceIndex()
{
    return pConnectorEvo->pDispEvo->displayOwner;
}

NvU32 EvoInterface::getDisplayId()
{
    return nvDpyIdToNvU32(pConnectorEvo->displayId);
}

NvU32 EvoInterface::getSorIndex()
{
    if (pConnectorEvo->pDpLibConnector) {
        if (pConnectorEvo->pDpLibConnector->linkHandoffEnabled) {
            return DP_INVALID_SOR_INDEX;
        } else {
            return pConnectorEvo->or.primary;
        }
    } else {
        return pConnectorEvo->or.primary;
    }
}

NvU32 EvoInterface::getLinkIndex()
{
    switch (pConnectorEvo->or.protocol) {
    case NV0073_CTRL_SPECIFIC_OR_PROTOCOL_SOR_DP_A:
        return 0;
    case NV0073_CTRL_SPECIFIC_OR_PROTOCOL_SOR_DP_B:
        return 1;
    }

    nvAssert(!"Unrecognized DP protocol");
    return -1;
}

NvU32 EvoInterface::monitorDenylistInfo(
    NvU32 manufId, NvU32 productId,
    DisplayPort::DpMonitorDenylistData *pDenylistData)
{
    //
    // WAR for Toshiba/Dell internal(eDP) panel Sharp , overriding
    // optimal link configuration to HBR2.
    //
    // HBR2 is required to drive 4K resolution, which is supported on DP1.2
    // onward specifications. Panel advertises itself as DP1.2 capable, but
    // does not have ESI address space, this is violation the specification
    // and hence inside DP library we downgrade the DPCD revision to 1.1.
    // With this downgrade in DPCD version, link rate also gets downgraded
    // to HBR.
    //
    if (manufId == 0x104d &&
        (productId == 0x1414 || productId == 0x1430)) {

        NvU32 warFlags = DisplayPort::DP_MONITOR_CAPABILITY_DP_OVERRIDE_OPTIMAL_LINK_CONFIG;

        pDenylistData->dpOverrideOptimalLinkConfig.linkRate  = 0x14; // HBR2
        pDenylistData->dpOverrideOptimalLinkConfig.laneCount = laneCount_4; // 4 lanes

        return warFlags;
    }

    return 0;
}

}; // namespace nvkmsDisplayPort
