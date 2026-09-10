/* SPDX-License-Identifier: MIT */

struct nv_drm_connector {
    struct drm_connector base;
    atomic_t link_recovery_generation;
    unsigned int link_recovery_handled_generation;
    struct nv_drm_link_recovery_policy link_recovery_policy;
};

struct nv_drm_device {
    struct drm_device *dev;
    atomic_t enable_event_handling;
    bool link_recovery_paused;
    struct delayed_work link_recovery_work;
};

#define MOCK_MAX_CONNECTORS 4

struct mock_drm_faults {
    unsigned int allocation_failures;
    int connector_state_error;
    int crtc_state_error;
    int commit_error;

    /* This failure persists; the errors above are consumed on their next use. */
    int post_swap_error;
    unsigned int deadlock_on_lock_call;
    unsigned int events_during_commit;
    bool pause_during_commit;
    bool disconnect_during_backoff;
    bool change_configuration_during_backoff;
};

static struct {
    struct drm_device device;
    struct nv_drm_device nv_device;
    struct nv_drm_connector connectors[MOCK_MAX_CONNECTORS];
    struct drm_connector_state connector_states[MOCK_MAX_CONNECTORS];
    struct drm_crtc crtcs[MOCK_MAX_CONNECTORS];
    struct drm_crtc_state crtc_states[MOCK_MAX_CONNECTORS];
    unsigned int connector_count;
    unsigned int time_ms;

    struct mock_drm_faults faults;
    unsigned int commit_count;
    unsigned int allocation_count;
    unsigned int free_count;
    unsigned int lock_count;
    unsigned int backoff_count;
    unsigned int error_log_count;
    unsigned int success_log_count;
    unsigned int hotplug_event_count;
    unsigned int committed_mode;
    unsigned int committed_color;
} mock;

void nv_drm_queue_link_recovery(struct nv_drm_connector *nv_connector);

static struct nv_drm_device *to_nv_device(struct drm_device *dev)
{
    return dev->nv;
}

static struct nv_drm_connector *to_nv_connector(struct drm_connector *connector)
{
    return container_of(connector, struct nv_drm_connector, base);
}

/* The fixture represents both jiffies and the boot clock in milliseconds. */
static unsigned int msecs_to_jiffies(unsigned int milliseconds)
{
    return milliseconds;
}

static bool schedule_delayed_work(struct delayed_work *work, unsigned int delay)
{
    if (work->pending) {
        return false;
    }

    work->pending = true;
    work->deadline_ms = mock.time_ms + delay;
    return true;
}

static NvU64 nv_drm_get_time_since_boot_ms(void)
{
    return mock.time_ms;
}

static void drm_modeset_acquire_init(struct drm_modeset_acquire_ctx *ctx,
                                    int flags)
{
    ctx->locks_held = 0;
}

static int drm_modeset_lock(struct drm_modeset_lock *lock,
                           struct drm_modeset_acquire_ctx *ctx)
{
    mock.lock_count++;
    if (mock.lock_count == mock.faults.deadlock_on_lock_call) {
        return -EDEADLK;
    }

    ctx->locks_held++;
    return 0;
}

static int drm_modeset_backoff(struct drm_modeset_acquire_ctx *ctx)
{
    mock.backoff_count++;
    ctx->locks_held = 0;

    if (mock.faults.disconnect_during_backoff) {
        mock.connectors[0].base.status = connector_status_disconnected;
    }
    if (mock.faults.change_configuration_during_backoff) {
        mock.crtc_states[0].user_mode = 144;
        mock.connector_states[0].color = 12;
    }

    return 0;
}

static void drm_modeset_drop_locks(struct drm_modeset_acquire_ctx *ctx)
{
    ctx->locks_held = 0;
}

static void drm_modeset_acquire_fini(struct drm_modeset_acquire_ctx *ctx)
{
    assert(ctx->locks_held == 0);
}

static nv_drm_atomic_state_base_t *nv_drm_atomic_state_base_alloc(
    struct drm_device *dev)
{
    nv_drm_atomic_state_base_t *state;

    if (mock.faults.allocation_failures != 0) {
        mock.faults.allocation_failures--;
        return NULL;
    }

    state = calloc(1, sizeof(*state));
    assert(state != NULL);
    mock.allocation_count++;
    return state;
}

static void nv_drm_atomic_state_base_put(nv_drm_atomic_state_base_t *state)
{
    assert(state != NULL);
    mock.free_count++;
    free(state);
}

static int mock_take_error(int *injected_error)
{
    int error = *injected_error;

    *injected_error = 0;
    return error;
}

static struct drm_connector_state *drm_atomic_get_connector_state(
    nv_drm_atomic_state_base_t *state, struct drm_connector *connector)
{
    int error = mock_take_error(&mock.faults.connector_state_error);

    assert(state->acquire_ctx->locks_held >= 2);
    if (error != 0) {
        return ERR_PTR(-error);
    }

    state->connector = connector;
    state->new_connector = *connector->state;
    return &state->new_connector;
}

static struct drm_crtc_state *drm_atomic_get_crtc_state(
    nv_drm_atomic_state_base_t *state, struct drm_crtc *crtc)
{
    int error = mock_take_error(&mock.faults.crtc_state_error);

    if (error != 0) {
        return ERR_PTR(-error);
    }

    assert(crtc != NULL);
    state->new_crtc = *crtc->state;
    return &state->new_crtc;
}

static int drm_atomic_commit(nv_drm_atomic_state_base_t *state)
{
    int error;

    assert(state->acquire_ctx->locks_held >= 2);
    assert(state->allow_modeset);
    assert(state->new_crtc.mode_changed);
    assert(state->new_connector.link_status == DRM_MODE_LINK_STATUS_GOOD);
    mock.commit_count++;

    if (mock.faults.events_during_commit != 0) {
        mock.faults.events_during_commit--;
        nv_drm_queue_link_recovery(to_nv_connector(state->connector));
    }
    if (mock.faults.pause_during_commit) {
        mock.nv_device.link_recovery_paused = true;
    }

    error = mock_take_error(&mock.faults.commit_error);
    if (error != 0) {
        return -error;
    }

    mock.committed_mode = state->new_crtc.user_mode;
    mock.committed_color = state->new_connector.color;
    *state->connector->state = state->new_connector;

    /* Model KAPI failure after DRM has already accepted the new state. */
    state->commit_result = -mock.faults.post_swap_error;
    return 0;
}

static int nv_drm_atomic_commit_result(nv_drm_atomic_state_base_t *state)
{
    return state->commit_result;
}

static void drm_connector_list_iter_begin(
    struct drm_device *dev, struct drm_connector_list_iter *iter)
{
    iter->index = 0;
}

#define drm_for_each_connector_iter(connector, iter)                         \
    for (; (iter)->index < mock.connector_count &&                           \
         ((connector) = &mock.connectors[(iter)->index].base, true);          \
         (iter)->index++)

static void drm_connector_list_iter_end(struct drm_connector_list_iter *iter)
{
}

static void drm_kms_helper_hotplug_event(struct drm_device *dev)
{
    mock.hotplug_event_count++;
}

/* Routing tests explicitly populate the fixture's new connector/CRTC states. */
#define for_each_new_connector_in_state(state, connector, conn_state, index) \
    for ((index) = 0; (index) < (int)mock.connector_count &&                  \
         ((connector) = &mock.connectors[index].base,                        \
          (conn_state) = (connector)->state, true); (index)++)

static struct drm_crtc_state *drm_atomic_get_new_crtc_state(
    nv_drm_atomic_state_base_t *state, struct drm_crtc *crtc)
{
    return crtc->state;
}

static bool drm_atomic_crtc_needs_modeset(const struct drm_crtc_state *state)
{
    return state->mode_changed || state->connectors_changed ||
           state->active_changed;
}
