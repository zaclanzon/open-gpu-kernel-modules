/* SPDX-License-Identifier: MIT */
#include "nvidia-drm-conftest.h"

#if defined(NV_DRM_AVAILABLE)

#include "nvidia-drm-priv.h"
#include "nvidia-drm-connector.h"
#include "nvidia-drm-helper.h"
#include "nvidia-drm-link-recovery.h"
#include "nvidia-drm-modeset.h"

#include <linux/module.h>
#include <linux/workqueue.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#if defined(NV_DRM_DRM_PROBE_HELPER_H_PRESENT)
#include <drm/drm_probe_helper.h>
#endif

/* This can be disabled at runtime without unloading the display driver. */
static bool nv_drm_link_recovery_enabled = true;
module_param_named(link_recovery, nv_drm_link_recovery_enabled, bool, 0600);
MODULE_PARM_DESC(link_recovery, "Reapply active display state after link recovery");

static bool nv_drm_can_recover(struct nv_drm_device *nv_dev)
{
    return READ_ONCE(nv_drm_link_recovery_enabled) &&
           !READ_ONCE(nv_dev->link_recovery_paused) &&
           atomic_read(&nv_dev->enable_event_handling);
}

void nv_drm_queue_link_recovery(struct nv_drm_connector *nv_connector)
{
    struct nv_drm_device *nv_dev = to_nv_device(nv_connector->base.dev);

    if (!nv_drm_can_recover(nv_dev)) {
        return;
    }

    atomic_inc(&nv_connector->link_recovery_generation);
    /* Coalesce events without postponing recovery for every new event. */
    schedule_delayed_work(&nv_dev->link_recovery_work, msecs_to_jiffies(100));
}

/*
 * The connector iterator holds a reference for this entire call. No NvKms
 * event-dispatch or nv_dev->lock may be held here: commit waits for flip events.
 * Only DRM modeset locks protect the state we duplicate. In particular, do not
 * hold mode_config.mutex, which the NvKms display-event callback needs.
 */
static bool nv_drm_recover_connector(struct nv_drm_connector *nv_connector)
{
    struct drm_connector *connector = &nv_connector->base;
    struct drm_device *dev = connector->dev;
    struct nv_drm_device *nv_dev = to_nv_device(dev);
    struct drm_modeset_acquire_ctx ctx;
    nv_drm_atomic_state_base_t *state = NULL;
    struct drm_connector_state *connector_state;
    struct drm_crtc_state *crtc_state;
    struct nv_drm_link_recovery_policy saved_policy;
    unsigned int generation = 0;
    NvU64 now_ms;
    bool attempted = false;
    bool notify = false;
    int ret;

    drm_modeset_acquire_init(&ctx, 0);

retry:
    ret = drm_modeset_lock(&dev->mode_config.connection_mutex, &ctx);
    if (ret != 0) {
        goto out;
    }

    generation = (unsigned int)atomic_read(&nv_connector->link_recovery_generation);
    if (!nv_drm_can_recover(nv_dev) ||
        generation == nv_connector->link_recovery_handled_generation) {
        goto out;
    }

    if (connector->state == NULL || connector->state->crtc == NULL) {
        nv_connector->link_recovery_handled_generation = generation;
        memset(&nv_connector->link_recovery_policy, 0,
               sizeof(nv_connector->link_recovery_policy));
        goto out;
    }

    ret = drm_modeset_lock(&connector->state->crtc->mutex, &ctx);
    if (ret != 0) {
        goto out;
    }
    if (!connector->state->crtc->state->active) {
        nv_connector->link_recovery_handled_generation = generation;
        memset(&nv_connector->link_recovery_policy, 0,
               sizeof(nv_connector->link_recovery_policy));
        goto out;
    }

    now_ms = nv_drm_get_time_since_boot_ms();
    if (connector->status != connector_status_connected) {
        /*
         * The event callback requests a fresh connector probe. Its cached
         * status can lag the NvKms event while another display is modesetting.
         * Retain this generation without spending a modeset attempt. Recheck
         * current state on each pass; never reactivate an output turned off.
         */
        if (nv_connector->link_recovery_policy.detection_deadline_ms == 0) {
            nv_connector->link_recovery_policy.detection_deadline_ms =
                now_ms + NV_DRM_LINK_RECOVERY_DETECT_GRACE_MS;
        }
        if (now_ms < nv_connector->link_recovery_policy.detection_deadline_ms) {
            if (nv_drm_can_recover(nv_dev)) {
                schedule_delayed_work(&nv_dev->link_recovery_work, msecs_to_jiffies(100));
            }
        } else {
            nv_connector->link_recovery_handled_generation = generation;
            nv_connector->link_recovery_policy.detection_deadline_ms = 0;
        }
        goto out;
    }
    nv_connector->link_recovery_policy.detection_deadline_ms = 0;

    /* connection_mutex is held, as in drm_connector_set_link_status_property. */
    connector->state->link_status = DRM_MODE_LINK_STATUS_BAD;
    saved_policy = nv_connector->link_recovery_policy;
    if (!nv_drm_link_recovery_allow_attempt(&nv_connector->link_recovery_policy,
                                            now_ms)) {
        nv_connector->link_recovery_handled_generation = generation;
        if (!nv_connector->link_recovery_policy.blocked_reported) {
            nv_connector->link_recovery_policy.blocked_reported = NV_TRUE;
            NV_DRM_DEV_LOG_ERR(nv_dev,
                "[CONNECTOR:%u:%s] link recovery rate limit reached",
                connector->base.id, connector->name);
            notify = true;
        }
        goto out;
    }
    attempted = true;

    state = nv_drm_atomic_state_base_alloc(dev);
    if (state == NULL) {
        ret = -ENOMEM;
        goto completed;
    }
    state->acquire_ctx = &ctx;
    state->allow_modeset = true;

    connector_state = drm_atomic_get_connector_state(state, connector);
    if (IS_ERR(connector_state)) {
        ret = PTR_ERR(connector_state);
        goto completed;
    }
    crtc_state = drm_atomic_get_crtc_state(state, connector_state->crtc);
    if (IS_ERR(crtc_state)) {
        ret = PTR_ERR(crtc_state);
        goto completed;
    }

    /* Reuse the CURRENT state, including color state, planes and their refs. */
    connector_state->link_status = DRM_MODE_LINK_STATUS_GOOD;
    crtc_state->mode_changed = true;

    ret = drm_atomic_commit(state);
    if (ret == 0) {
        /* The DRM commit return value cannot report a post-swap KAPI failure. */
        ret = nv_drm_atomic_commit_result(state);
    }

completed:
    if (ret != -EDEADLK) {
        /* A new event arriving during the commit remains pending. */
        nv_connector->link_recovery_handled_generation = generation;
        if (ret != 0) {
            connector->state->link_status = DRM_MODE_LINK_STATUS_BAD;
            NV_DRM_DEV_LOG_ERR(nv_dev,
                "[CONNECTOR:%u:%s] link recovery modeset failed: %d",
                connector->base.id, connector->name, ret);
            notify = true;
            nv_drm_queue_link_recovery(nv_connector);
        } else {
            NV_DRM_DEV_LOG_INFO(nv_dev,
                "[CONNECTOR:%u:%s] reapplied display configuration after link recovery",
                connector->base.id, connector->name);
        }
    }

out:
    if (state != NULL) {
        nv_drm_atomic_state_base_put(state);
        state = NULL;
    }
    if (ret == -EDEADLK) {
        if (attempted) {
            nv_connector->link_recovery_policy = saved_policy;
            attempted = false;
        }
        ret = drm_modeset_backoff(&ctx);
        if (ret == 0) {
            goto retry;
        }
    }
    drm_modeset_drop_locks(&ctx);
    drm_modeset_acquire_fini(&ctx);
    return notify;
}

void nv_drm_handle_link_recovery_work(struct work_struct *work)
{
    struct nv_drm_device *nv_dev = container_of(to_delayed_work(work),
        struct nv_drm_device, link_recovery_work);
    struct drm_connector_list_iter iter;
    struct drm_connector *connector;
    bool notify = false;

    drm_connector_list_iter_begin(nv_dev->dev, &iter);
    drm_for_each_connector_iter(connector, &iter) {
        notify |= nv_drm_recover_connector(to_nv_connector(connector));
    }
    drm_connector_list_iter_end(&iter);

    if (notify && atomic_read(&nv_dev->enable_event_handling)) {
        drm_kms_helper_hotplug_event(nv_dev->dev);
    }
}

#endif
