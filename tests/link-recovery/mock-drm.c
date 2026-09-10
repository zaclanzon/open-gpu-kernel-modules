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
static struct nv_drm_connector connectors[4];
static struct drm_connector_state conn_states[4];
static struct drm_crtc crtcs[4];
static struct drm_crtc_state crtc_states[4];
static struct drm_device device;
static struct nv_drm_device nv_device;
static unsigned int connector_count, commits, allocations, frees, backoffs;
static unsigned int alloc_fail, connector_error, crtc_error, commit_error, late_error;
static unsigned int lock_calls, lock_error_at, event_on_commit, pause_on_commit;
static bool unplug_on_backoff, replace_mode_on_backoff;
static unsigned int committed_mode, committed_color;
static void nv_drm_queue_link_recovery(struct nv_drm_connector *connector);
static struct nv_drm_device *to_nv_device(struct drm_device *dev) { return dev->nv; }
static struct nv_drm_connector *to_nv_connector(struct drm_connector *c) {
    return container_of(c, struct nv_drm_connector, base);
}
static unsigned int msecs_to_jiffies(unsigned int value) { return value; }
static bool schedule_delayed_work(struct delayed_work *w, unsigned int delay) {
    if (w->pending) return false;
    w->pending = true;
    w->deadline = now_ms + delay;
    return true;
}
static NvU64 nv_drm_get_time_since_boot_ms(void) { return now_ms; }
static void drm_modeset_acquire_init(struct drm_modeset_acquire_ctx *ctx, int flags) {
    ctx->held = 0;
}
static int drm_modeset_lock(struct drm_modeset_lock *lock, struct drm_modeset_acquire_ctx *ctx) {
    if (++lock_calls == lock_error_at) return -EDEADLK;
    ctx->held++;
    return 0;
}
static int drm_modeset_backoff(struct drm_modeset_acquire_ctx *ctx) {
    backoffs++;
    ctx->held = 0;
    if (unplug_on_backoff) connectors[0].base.status = connector_status_disconnected;
    if (replace_mode_on_backoff) {
        crtc_states[0].user_mode = 144;
        conn_states[0].color = 12;
    }
    return 0;
}
static void drm_modeset_drop_locks(struct drm_modeset_acquire_ctx *ctx) { ctx->held = 0; }
static void drm_modeset_acquire_fini(struct drm_modeset_acquire_ctx *ctx) { assert(!ctx->held); }
static nv_drm_atomic_state_base_t *nv_drm_atomic_state_base_alloc(struct drm_device *dev) {
    if (alloc_fail) { alloc_fail--; return NULL; }
    allocations++;
    return calloc(1, sizeof(nv_drm_atomic_state_base_t));
}
static void nv_drm_atomic_state_base_put(nv_drm_atomic_state_base_t *state) {
    assert(state != NULL);
    frees++;
    free(state);
}
static struct drm_connector_state *drm_atomic_get_connector_state(
    nv_drm_atomic_state_base_t *state, struct drm_connector *c) {
    assert(state->acquire_ctx->held >= 2);
    if (connector_error) { int err = connector_error; connector_error = 0; return ERR_PTR(-err); }
    state->connector = c;
    state->new_connector = *c->state;
    return &state->new_connector;
}
static struct drm_crtc_state *drm_atomic_get_crtc_state(
    nv_drm_atomic_state_base_t *state, struct drm_crtc *crtc) {
    if (crtc_error) { int err = crtc_error; crtc_error = 0; return ERR_PTR(-err); }
    assert(crtc != NULL);
    state->new_crtc = *crtc->state;
    return &state->new_crtc;
}
static int drm_atomic_commit(nv_drm_atomic_state_base_t *state) {
    assert(state->acquire_ctx->held >= 2);
    assert(state->allow_modeset && state->new_crtc.mode_changed);
    assert(state->new_connector.link_status == DRM_MODE_LINK_STATUS_GOOD);
    commits++;
    if (event_on_commit) {
        event_on_commit--;
        nv_drm_queue_link_recovery(to_nv_connector(state->connector));
    }
    if (pause_on_commit) {
        nv_device.link_recovery_paused = true;
    }
    if (commit_error) { int err = commit_error; commit_error = 0; return -err; }
    committed_mode = state->new_crtc.user_mode;
    committed_color = state->new_connector.color;
    *state->connector->state = state->new_connector;
    state->commit_result = -(int)late_error;
    return 0;
}
static int nv_drm_atomic_commit_result(nv_drm_atomic_state_base_t *state) {
    return state->commit_result;
}
static void drm_connector_list_iter_begin(struct drm_device *dev, struct drm_connector_list_iter *iter) {
    iter->index = 0;
}
#define drm_for_each_connector_iter(c, iter) \
    for (; (iter)->index < connector_count && ((c) = &connectors[(iter)->index].base, true); (iter)->index++)
static void drm_connector_list_iter_end(struct drm_connector_list_iter *iter) {}
static void drm_kms_helper_hotplug_event(struct drm_device *dev) { hotplugs++; }

#define for_each_new_connector_in_state(state, conn, conn_state, i) \
    for ((i) = 0; (i) < (int)connector_count && \
         ((conn) = &connectors[i].base, (conn_state) = conn->state, true); (i)++)
static struct drm_crtc_state *drm_atomic_get_new_crtc_state(
    nv_drm_atomic_state_base_t *state, struct drm_crtc *crtc) { return crtc->state; }
static bool drm_atomic_crtc_needs_modeset(const struct drm_crtc_state *state) {
    return state->mode_changed || state->connectors_changed || state->active_changed;
}
