/* SPDX-License-Identifier: MIT */
#ifndef __LINK_RECOVERY_MOCK_DRM_H__
#define __LINK_RECOVERY_MOCK_DRM_H__

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Only the types and fields used by the recovery code are modeled here. */
#define NV_DRM_AVAILABLE 1
#define NV_TRUE true
#define NV_FALSE false

typedef uint64_t NvU64;
typedef bool NvBool;

typedef struct {
    unsigned int counter;
} atomic_t;

static unsigned int atomic_read(atomic_t *value)
{
    return value->counter;
}

static void atomic_inc(atomic_t *value)
{
    value->counter++;
}

#define READ_ONCE(value) (value)
#define module_param_named(...)
#define MODULE_PARM_DESC(...)
#define NV_DRM_DEV_LOG_ERR(...) (++mock.error_log_count)
#define NV_DRM_DEV_LOG_INFO(...) (++mock.success_log_count)

#define DRM_MODE_LINK_STATUS_GOOD 0
#define DRM_MODE_LINK_STATUS_BAD 1
#define connector_status_connected 1
#define connector_status_disconnected 2

#define IS_ERR(pointer) ((uintptr_t)(pointer) >= (uintptr_t)-4095)
#define PTR_ERR(pointer) ((int)(intptr_t)(pointer))
#define ERR_PTR(error) ((void *)(intptr_t)(error))
#define container_of(pointer, type, member) \
    ((type *)((char *)(pointer) - offsetof(type, member)))

struct work_struct {
    int unused;
};

struct delayed_work {
    struct work_struct work;
    bool pending;
    unsigned int deadline_ms;
};

#define to_delayed_work(work) container_of(work, struct delayed_work, work)

/* Lock counts check call ordering, not mutual exclusion between threads. */
struct drm_modeset_acquire_ctx {
    unsigned int locks_held;
};

struct drm_modeset_lock {
    int unused;
};

struct drm_crtc_state {
    bool active;
    bool mode_changed;
    bool connectors_changed;
    bool active_changed;
    unsigned int user_mode;
};

struct drm_crtc {
    struct drm_modeset_lock mutex;
    struct drm_crtc_state *state;
};

struct drm_connector_state {
    struct drm_crtc *crtc;
    int link_status;
    unsigned int color;
};

struct drm_device;

struct drm_connector {
    struct drm_device *dev;
    int status;
    struct drm_connector_state *state;
    struct {
        unsigned int id;
    } base;
    const char *name;
};

struct nv_drm_device;

struct drm_device {
    struct {
        struct drm_modeset_lock connection_mutex;
    } mode_config;
    struct nv_drm_device *nv;
};

struct nv_atomic_state {
    struct drm_modeset_acquire_ctx *acquire_ctx;
    bool allow_modeset;
    struct drm_connector *connector;
    struct drm_connector_state new_connector;
    struct drm_crtc_state new_crtc;
    int commit_result;
};

typedef struct nv_atomic_state nv_drm_atomic_state_base_t;

struct drm_connector_list_iter {
    unsigned int index;
};

#endif /* __LINK_RECOVERY_MOCK_DRM_H__ */
