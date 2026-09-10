/* SPDX-License-Identifier: MIT */
static void reset(unsigned int count)
{
    memset(connectors, 0, sizeof(connectors));
    memset(conn_states, 0, sizeof(conn_states));
    memset(crtcs, 0, sizeof(crtcs));
    memset(crtc_states, 0, sizeof(crtc_states));
    memset(&nv_device, 0, sizeof(nv_device));
    connector_count = count;
    now_ms = 100;
    error_logs = success_logs = hotplugs = commits = allocations = frees = backoffs = 0;
    alloc_fail = connector_error = crtc_error = commit_error = late_error = 0;
    lock_calls = lock_error_at = event_on_commit = pause_on_commit = 0;
    unplug_on_backoff = replace_mode_on_backoff = false;
    nv_drm_link_recovery_enabled = true;
    device.nv = &nv_device;
    nv_device.dev = &device;
    nv_device.enable_event_handling.counter = 1;
    for (unsigned int i = 0; i < count; i++) {
        connectors[i].base.dev = &device;
        connectors[i].base.state = &conn_states[i];
        connectors[i].base.status = connector_status_connected;
        connectors[i].base.name = "mock";
        conn_states[i].crtc = &crtcs[i];
        conn_states[i].color = 10;
        crtcs[i].state = &crtc_states[i];
        crtc_states[i].active = true;
        crtc_states[i].user_mode = 240 + i;
    }
}
static void run_work(void)
{
    nv_device.link_recovery_work.pending = false;
    nv_drm_handle_link_recovery_work(&nv_device.link_recovery_work.work);
    assert(allocations == frees);
}
static void request(void) { nv_drm_queue_link_recovery(&connectors[0]); }
static void test_disabled_and_idle(void)
{
    reset(1); run_work(); assert(commits == 0);
    nv_drm_link_recovery_enabled = false;
    request(); assert(!nv_device.link_recovery_work.pending);
    nv_drm_link_recovery_enabled = true;
    nv_device.enable_event_handling.counter = 0;
    request(); assert(!nv_device.link_recovery_work.pending);
    nv_device.enable_event_handling.counter = 1;
    nv_device.link_recovery_paused = true;
    request(); assert(!nv_device.link_recovery_work.pending);
    nv_device.link_recovery_paused = false;
    request(); nv_drm_link_recovery_enabled = false; run_work(); assert(!commits);
    puts("PASS disabled, paused, stopped and idle queues");
}
static void test_inactive_and_unplugged(void)
{
    for (unsigned int scenario = 0; scenario < 4; scenario++) {
        reset(1); request();
        switch (scenario) {
        case 0: connectors[0].base.status = connector_status_disconnected; break;
        case 1: connectors[0].base.state = NULL; break;
        case 2: conn_states[0].crtc = NULL; break;
        case 3: crtc_states[0].active = false; break;
        }
        run_work();
        assert(commits == 0 && !hotplugs);
        if (scenario == 0) {
            assert(connectors[0].link_recovery_handled_generation == 0);
            assert(nv_device.link_recovery_work.pending);
            now_ms += NV_DRM_LINK_RECOVERY_DETECT_GRACE_MS;
            run_work();
            assert(!nv_device.link_recovery_work.pending);
        }
        assert(connectors[0].link_recovery_handled_generation == 1);
        assert(connectors[0].link_recovery_policy.attempts == 0);
    }
    puts("PASS unplug, missing state, disabled connector and inactive CRTC");
}
static void test_coalesce_and_new_events(void)
{
    reset(1); request(); unsigned int deadline = nv_device.link_recovery_work.deadline;
    now_ms += 50; request(); request();
    assert(nv_device.link_recovery_work.deadline == deadline);
    run_work(); assert(commits == 1 && success_logs == 1 && !hotplugs);
    assert(committed_mode == 240 && committed_color == 10);
    assert(connectors[0].link_recovery_handled_generation == 3);
    run_work(); assert(commits == 1);
    request(); event_on_commit = 1; run_work();
    assert(nv_device.link_recovery_work.pending);
    assert(connectors[0].link_recovery_handled_generation == 4);
    assert(connectors[0].link_recovery_generation.counter == 5);
    run_work(); assert(commits == 3);
    puts("PASS coalescing, current mode/color preservation and event during commit");
}
static void test_error_paths(void)
{
    for (unsigned int scenario = 0; scenario < 5; scenario++) {
        reset(1); request();
        switch (scenario) {
        case 0: alloc_fail = 1; break;
        case 1: connector_error = ENOMEM; break;
        case 2: crtc_error = ENOMEM; break;
        case 3: commit_error = EINVAL; break;
        case 4: late_error = EIO; break;
        }
        run_work();
        assert(conn_states[0].link_status == DRM_MODE_LINK_STATUS_BAD);
        assert(error_logs == 1 && hotplugs == 1 && success_logs == 0);
        assert(nv_device.link_recovery_work.pending);
        late_error = 0; run_work();
        assert(conn_states[0].link_status == DRM_MODE_LINK_STATUS_GOOD);
        assert(success_logs == 1);
    }
    puts("PASS allocation, state acquisition, pre-swap and post-swap failures");
}
static void test_deadlock_backoff(void)
{
    for (unsigned int scenario = 0; scenario < 5; scenario++) {
        reset(1); request();
        switch (scenario) {
        case 0: lock_error_at = 1; break;
        case 1: lock_error_at = 2; break;
        case 2: connector_error = EDEADLK; break;
        case 3: crtc_error = EDEADLK; break;
        case 4: commit_error = EDEADLK; break;
        }
        replace_mode_on_backoff = true;
        run_work();
        assert(backoffs == 1 && success_logs == 1 && !error_logs);
        assert(connectors[0].link_recovery_policy.attempts == 1);
        assert(committed_mode == 144 && committed_color == 12);
    }
    reset(1); request(); crtc_error = EDEADLK; unplug_on_backoff = true; run_work();
    assert(!commits && !error_logs && !connectors[0].link_recovery_policy.attempts);
    puts("PASS lock backoff, retry budget refund, concurrent mode change and unplug");
}
static void test_storm_and_rearm(void)
{
    reset(1); request(); late_error = EIO;
    for (unsigned int i = 0; i < 4; i++) { run_work(); now_ms += 100; }
    assert(commits == 3 && !nv_device.link_recovery_work.pending);
    assert(conn_states[0].link_status == DRM_MODE_LINK_STATUS_BAD);
    unsigned int logged = error_logs, notified = hotplugs;
    for (unsigned int i = 0; i < 100; i++) { request(); run_work(); now_ms += 100; }
    assert(commits == 3 && error_logs == logged && hotplugs == notified);
    now_ms += NV_DRM_LINK_RECOVERY_QUIET_MS;
    late_error = 0; request(); run_work();
    assert(commits == 4 && success_logs == 1);
    assert(connectors[0].link_recovery_policy.attempts == 1);
    puts("PASS bounded retries, continuous event storm and quiet-period rearm");
}
static void test_multiple_connectors_and_pause(void)
{
    reset(4);
    for (unsigned int i = 0; i < 4; i++) nv_drm_queue_link_recovery(&connectors[i]);
    crtc_states[1].active = false;
    connectors[2].base.status = connector_status_disconnected;
    run_work(); assert(commits == 2);
    assert(connectors[0].link_recovery_policy.attempts == 1);
    assert(connectors[3].link_recovery_policy.attempts == 1);
    reset(1); request(); pause_on_commit = 1; late_error = EIO; run_work();
    assert(!nv_device.link_recovery_work.pending);
    reset(1); connectors[0].link_recovery_generation.counter = UINT_MAX;
    connectors[0].link_recovery_handled_generation = UINT_MAX;
    request(); run_work(); assert(commits == 1);
    puts("PASS independent connectors, pause during failure and generation wrap");
}
static void test_failed_modeset_routing(void)
{
    reset(4);
    crtc_states[0].mode_changed = true;
    crtc_states[1].active = false;
    crtc_states[1].mode_changed = true;
    conn_states[2].crtc = NULL;
    nv_drm_atomic_state_base_t state = {};
    nv_drm_recover_failed_modeset(&state);
    assert(connectors[0].link_recovery_generation.counter == 1);
    assert(connectors[1].link_recovery_generation.counter == 0);
    assert(connectors[2].link_recovery_generation.counter == 0);
    assert(connectors[3].link_recovery_generation.counter == 0);
    crtc_states[0].mode_changed = false;
    crtc_states[3].connectors_changed = true;
    nv_drm_recover_failed_modeset(&state);
    assert(connectors[3].link_recovery_generation.counter == 1);
    crtc_states[3].connectors_changed = false;
    crtc_states[3].active_changed = true;
    nv_drm_recover_failed_modeset(&state);
    assert(connectors[3].link_recovery_generation.counter == 2);
    puts("PASS failed modesets select active outputs; ordinary flips and disable commits do not retry");
}
static void test_pending_connection_status(void)
{
    reset(4);
    /* HDMI recovery completes while DP's cached connection status lags detect. */
    connectors[1].base.status = connector_status_disconnected;
    nv_drm_queue_link_recovery(&connectors[0]);
    nv_drm_queue_link_recovery(&connectors[1]);
    run_work();
    assert(commits == 1);
    assert(connectors[1].link_recovery_handled_generation == 0);
    assert(nv_device.link_recovery_work.pending);
    now_ms += 250;
    connectors[1].base.status = connector_status_connected;
    crtc_states[1].user_mode = 240;
    run_work();
    assert(commits == 2 && committed_mode == 240);
    assert(connectors[1].link_recovery_handled_generation == 1);
    assert(connectors[1].link_recovery_policy.attempts == 1);
    puts("PASS mixed HDMI/DP recovery retains a request across stale disconnected status");
}
static void test_detection_wait_boundaries(void)
{
    for (unsigned int scenario = 0; scenario < 4; scenario++) {
        reset(1);
        connectors[0].base.status = connector_status_disconnected;
        request(); run_work();
        now_ms += 100;
        switch (scenario) {
        case 0: conn_states[0].crtc = NULL; break;
        case 1: crtc_states[0].active = false; break;
        case 2: nv_device.link_recovery_paused = true; break;
        case 3: nv_drm_link_recovery_enabled = false; break;
        }
        run_work();
        assert(!commits && !hotplugs && !nv_device.link_recovery_work.pending);
        assert(!connectors[0].link_recovery_policy.attempts);
        if (scenario < 2) {
            assert(connectors[0].link_recovery_handled_generation == 1);
            assert(!connectors[0].link_recovery_policy.detection_deadline_ms);
        }
    }
    reset(1);
    connectors[0].base.status = connector_status_disconnected;
    request(); run_work();
    NvU64 deadline = connectors[0].link_recovery_policy.detection_deadline_ms;
    for (unsigned int i = 0; i < 19; i++) {
        now_ms += 100;
        request(); run_work();
        assert(connectors[0].link_recovery_policy.detection_deadline_ms == deadline);
        assert(!connectors[0].link_recovery_handled_generation);
    }
    now_ms = deadline;
    run_work();
    assert(!commits && !hotplugs && !nv_device.link_recovery_work.pending);
    assert(connectors[0].link_recovery_handled_generation == 20);
    assert(!connectors[0].link_recovery_policy.attempts);
    connectors[0].base.status = connector_status_connected;
    run_work(); assert(!commits);
    request(); run_work(); assert(commits == 1);
    puts("PASS detection wait ends on disable/pause/unassign and cannot be extended by event storms");
}
int main(void)
{
    test_pending_connection_status();
    test_detection_wait_boundaries();
    test_disabled_and_idle();
    test_inactive_and_unplugged();
    test_coalesce_and_new_events();
    test_error_paths();
    test_deadlock_backoff();
    test_storm_and_rearm();
    test_multiple_connectors_and_pause();
    test_failed_modeset_routing();
    puts("All production-worker control-flow tests passed under ASan/UBSan.");
    return 0;
}
