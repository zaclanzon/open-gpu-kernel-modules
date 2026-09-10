/* SPDX-License-Identifier: MIT */
#ifndef __NVIDIA_DRM_LINK_RECOVERY_POLICY_H__
#define __NVIDIA_DRM_LINK_RECOVERY_POLICY_H__

#include "nvtypes.h"

#define NV_DRM_LINK_RECOVERY_MAX_ATTEMPTS 3
#define NV_DRM_LINK_RECOVERY_QUIET_MS 10000
#define NV_DRM_LINK_RECOVERY_DETECT_GRACE_MS 2000

/* Accessed only by the recovery worker, with connection_mutex held. */
struct nv_drm_link_recovery_policy {
    NvU64 last_event_ms;
    NvU64 detection_deadline_ms;
    unsigned int attempts;
    NvBool blocked_reported;
};

static inline NvBool nv_drm_link_recovery_allow_attempt(
    struct nv_drm_link_recovery_policy *policy, NvU64 now_ms)
{
    if (now_ms - policy->last_event_ms >= NV_DRM_LINK_RECOVERY_QUIET_MS) {
        policy->attempts = 0;
        policy->blocked_reported = NV_FALSE;
    }
    /* Failed/limited requests extend the burst; they do not rearm it. */
    policy->last_event_ms = now_ms;
    if (policy->attempts >= NV_DRM_LINK_RECOVERY_MAX_ATTEMPTS) {
        return NV_FALSE;
    }
    policy->attempts++;
    return NV_TRUE;
}

#endif
