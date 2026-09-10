/* SPDX-License-Identifier: MIT */

static void reset_test_state(unsigned int connector_count)
{
    unsigned int i;

    assert(connector_count <= MOCK_MAX_CONNECTORS);
    assert(mock.allocation_count == mock.free_count);
    memset(&mock, 0, sizeof(mock));
    mock.connector_count = connector_count;
    mock.time_ms = 100;
    mock.device.nv = &mock.nv_device;
    mock.nv_device.dev = &mock.device;
    mock.nv_device.enable_event_handling.counter = 1;
    nv_drm_link_recovery_enabled = true;

    for (i = 0; i < connector_count; i++) {
        mock.connectors[i].base.dev = &mock.device;
        mock.connectors[i].base.state = &mock.connector_states[i];
        mock.connectors[i].base.status = connector_status_connected;
        mock.connectors[i].base.name = "mock";
        mock.connector_states[i].crtc = &mock.crtcs[i];
        mock.connector_states[i].color = 10;
        mock.crtcs[i].state = &mock.crtc_states[i];
        mock.crtc_states[i].active = true;
        mock.crtc_states[i].user_mode = 240 + i;
    }
}

/* Run one worker pass. Each test controls the clock and any later passes. */
static void run_recovery_work(void)
{
    mock.nv_device.link_recovery_work.pending = false;
    nv_drm_handle_link_recovery_work(&mock.nv_device.link_recovery_work.work);
    assert(mock.allocation_count == mock.free_count);
}

static void request_recovery(void)
{
    nv_drm_queue_link_recovery(&mock.connectors[0]);
}

static void test_idle_worker(void)
{
    reset_test_state(1);
    run_recovery_work();
    assert(mock.commit_count == 0);
}

static void test_recovery_disabled(void)
{
    reset_test_state(1);
    nv_drm_link_recovery_enabled = false;
    request_recovery();
    assert(!mock.nv_device.link_recovery_work.pending);

    nv_drm_link_recovery_enabled = true;
    mock.nv_device.enable_event_handling.counter = 0;
    request_recovery();
    assert(!mock.nv_device.link_recovery_work.pending);

    mock.nv_device.enable_event_handling.counter = 1;
    mock.nv_device.link_recovery_paused = true;
    request_recovery();
    assert(!mock.nv_device.link_recovery_work.pending);

    /* Disabling recovery also stops work that was already queued. */
    mock.nv_device.link_recovery_paused = false;
    request_recovery();
    nv_drm_link_recovery_enabled = false;
    run_recovery_work();
    assert(mock.commit_count == 0);
}

static void test_inactive_outputs(void)
{
    enum inactive_output {
        MISSING_CONNECTOR_STATE,
        UNASSIGNED_CONNECTOR,
        INACTIVE_CRTC,
        INACTIVE_OUTPUT_COUNT
    } output;

    for (output = 0; output < INACTIVE_OUTPUT_COUNT; output++) {
        reset_test_state(1);
        request_recovery();

        switch (output) {
        case MISSING_CONNECTOR_STATE:
            mock.connectors[0].base.state = NULL;
            break;
        case UNASSIGNED_CONNECTOR:
            mock.connector_states[0].crtc = NULL;
            break;
        case INACTIVE_CRTC:
            mock.crtc_states[0].active = false;
            break;
        default:
            assert(false);
        }

        run_recovery_work();
        assert(mock.commit_count == 0);
        assert(mock.hotplug_event_count == 0);
        assert(mock.connectors[0].link_recovery_handled_generation == 1);
        assert(mock.connectors[0].link_recovery_policy.attempts == 0);
    }
}

static void test_disconnected_output_expires(void)
{
    reset_test_state(1);
    request_recovery();
    mock.connectors[0].base.status = connector_status_disconnected;
    run_recovery_work();

    assert(mock.connectors[0].link_recovery_handled_generation == 0);
    assert(mock.nv_device.link_recovery_work.pending);

    mock.time_ms += NV_DRM_LINK_RECOVERY_DETECT_GRACE_MS;
    run_recovery_work();
    assert(!mock.nv_device.link_recovery_work.pending);
    assert(mock.commit_count == 0);
    assert(mock.hotplug_event_count == 0);
    assert(mock.connectors[0].link_recovery_handled_generation == 1);
    assert(mock.connectors[0].link_recovery_policy.attempts == 0);
}

static void test_event_coalescing(void)
{
    unsigned int original_deadline;

    reset_test_state(1);
    request_recovery();
    original_deadline = mock.nv_device.link_recovery_work.deadline_ms;

    mock.time_ms += 50;
    request_recovery();
    request_recovery();
    assert(mock.nv_device.link_recovery_work.deadline_ms == original_deadline);

    run_recovery_work();
    assert(mock.commit_count == 1);
    assert(mock.success_log_count == 1);
    assert(mock.hotplug_event_count == 0);
    assert(mock.committed_mode == 240);
    assert(mock.committed_color == 10);
    assert(mock.connectors[0].link_recovery_handled_generation == 3);

    run_recovery_work();
    assert(mock.commit_count == 1);
}

static void test_event_during_commit(void)
{
    reset_test_state(1);
    request_recovery();
    mock.faults.events_during_commit = 1;
    run_recovery_work();

    assert(mock.nv_device.link_recovery_work.pending);
    assert(mock.connectors[0].link_recovery_handled_generation == 1);
    assert(mock.connectors[0].link_recovery_generation.counter == 2);

    run_recovery_work();
    assert(mock.commit_count == 2);
    assert(mock.connectors[0].link_recovery_handled_generation == 2);
}

enum failure_point {
    ALLOCATE_ATOMIC_STATE,
    ACQUIRE_CONNECTION_LOCK,
    ACQUIRE_CRTC_LOCK,
    GET_CONNECTOR_STATE,
    GET_CRTC_STATE,
    COMMIT_BEFORE_STATE_SWAP,
    COMMIT_AFTER_STATE_SWAP
};

static void inject_failure(enum failure_point point, int error)
{
    switch (point) {
    case ALLOCATE_ATOMIC_STATE:
        assert(error == ENOMEM);
        mock.faults.allocation_failures = 1;
        break;
    case ACQUIRE_CONNECTION_LOCK:
        assert(error == EDEADLK);
        mock.faults.deadlock_on_lock_call = 1;
        break;
    case ACQUIRE_CRTC_LOCK:
        assert(error == EDEADLK);
        mock.faults.deadlock_on_lock_call = 2;
        break;
    case GET_CONNECTOR_STATE:
        mock.faults.connector_state_error = error;
        break;
    case GET_CRTC_STATE:
        mock.faults.crtc_state_error = error;
        break;
    case COMMIT_BEFORE_STATE_SWAP:
        mock.faults.commit_error = error;
        break;
    case COMMIT_AFTER_STATE_SWAP:
        mock.faults.post_swap_error = error;
        break;
    }
}

static void test_failed_attempt_is_retried(void)
{
    static const struct {
        enum failure_point point;
        int error;
    } failures[] = {
        { ALLOCATE_ATOMIC_STATE, ENOMEM },
        { GET_CONNECTOR_STATE, ENOMEM },
        { GET_CRTC_STATE, ENOMEM },
        { COMMIT_BEFORE_STATE_SWAP, EINVAL },
        { COMMIT_AFTER_STATE_SWAP, EIO }
    };
    unsigned int i;

    for (i = 0; i < sizeof(failures) / sizeof(failures[0]); i++) {
        reset_test_state(1);
        request_recovery();
        inject_failure(failures[i].point, failures[i].error);
        run_recovery_work();

        assert(mock.connector_states[0].link_status ==
               DRM_MODE_LINK_STATUS_BAD);
        assert(mock.error_log_count == 1);
        assert(mock.hotplug_event_count == 1);
        assert(mock.success_log_count == 0);
        assert(mock.nv_device.link_recovery_work.pending);

        mock.faults.post_swap_error = 0;
        run_recovery_work();
        assert(mock.connector_states[0].link_status ==
               DRM_MODE_LINK_STATUS_GOOD);
        assert(mock.success_log_count == 1);
    }
}

static void test_deadlock_backoff_uses_current_configuration(void)
{
    static const enum failure_point failures[] = {
        ACQUIRE_CONNECTION_LOCK,
        ACQUIRE_CRTC_LOCK,
        GET_CONNECTOR_STATE,
        GET_CRTC_STATE,
        COMMIT_BEFORE_STATE_SWAP
    };
    unsigned int i;

    for (i = 0; i < sizeof(failures) / sizeof(failures[0]); i++) {
        reset_test_state(1);
        request_recovery();
        inject_failure(failures[i], EDEADLK);
        mock.faults.change_configuration_during_backoff = true;
        run_recovery_work();

        assert(mock.backoff_count == 1);
        assert(mock.success_log_count == 1);
        assert(mock.error_log_count == 0);
        assert(mock.connectors[0].link_recovery_policy.attempts == 1);
        assert(mock.committed_mode == 144);
        assert(mock.committed_color == 12);
    }
}

static void test_disconnect_during_backoff(void)
{
    reset_test_state(1);
    request_recovery();
    inject_failure(GET_CRTC_STATE, EDEADLK);
    mock.faults.disconnect_during_backoff = true;
    run_recovery_work();

    assert(mock.commit_count == 0);
    assert(mock.error_log_count == 0);
    assert(mock.connectors[0].link_recovery_policy.attempts == 0);
}

static void test_retry_limit_and_quiet_period(void)
{
    unsigned int errors_after_limit;
    unsigned int hotplugs_after_limit;
    unsigned int i;

    reset_test_state(1);
    request_recovery();
    mock.faults.post_swap_error = EIO;

    /* Three failed commits are followed by a pass that exhausts the budget. */
    for (i = 0; i < 4; i++) {
        run_recovery_work();
        mock.time_ms += 100;
    }
    assert(mock.commit_count == 3);
    assert(!mock.nv_device.link_recovery_work.pending);
    assert(mock.connector_states[0].link_status == DRM_MODE_LINK_STATUS_BAD);
    errors_after_limit = mock.error_log_count;
    hotplugs_after_limit = mock.hotplug_event_count;

    /* A continuous event storm must not rearm retries or repeat warnings. */
    for (i = 0; i < 100; i++) {
        request_recovery();
        run_recovery_work();
        mock.time_ms += 100;
    }
    assert(mock.commit_count == 3);
    assert(mock.error_log_count == errors_after_limit);
    assert(mock.hotplug_event_count == hotplugs_after_limit);

    mock.time_ms += NV_DRM_LINK_RECOVERY_QUIET_MS;
    mock.faults.post_swap_error = 0;
    request_recovery();
    run_recovery_work();
    assert(mock.commit_count == 4);
    assert(mock.success_log_count == 1);
    assert(mock.connectors[0].link_recovery_policy.attempts == 1);
}

static void test_connectors_recover_independently(void)
{
    unsigned int i;

    reset_test_state(4);
    for (i = 0; i < mock.connector_count; i++) {
        nv_drm_queue_link_recovery(&mock.connectors[i]);
    }
    mock.crtc_states[1].active = false;
    mock.connectors[2].base.status = connector_status_disconnected;
    run_recovery_work();

    assert(mock.commit_count == 2);
    assert(mock.connectors[0].link_recovery_policy.attempts == 1);
    assert(mock.connectors[3].link_recovery_policy.attempts == 1);
}

static void test_pause_during_failed_commit(void)
{
    reset_test_state(1);
    request_recovery();
    mock.faults.pause_during_commit = true;
    mock.faults.post_swap_error = EIO;
    run_recovery_work();
    assert(!mock.nv_device.link_recovery_work.pending);
}

static void test_generation_wrap(void)
{
    reset_test_state(1);
    mock.connectors[0].link_recovery_generation.counter = UINT_MAX;
    mock.connectors[0].link_recovery_handled_generation = UINT_MAX;
    request_recovery();
    run_recovery_work();
    assert(mock.commit_count == 1);
}

static void test_failed_modeset_routing(void)
{
    nv_drm_atomic_state_base_t state = {0};

    reset_test_state(4);
    mock.crtc_states[0].mode_changed = true;
    mock.crtc_states[1].active = false;
    mock.crtc_states[1].mode_changed = true;
    mock.connector_states[2].crtc = NULL;

    /* Only output 0 needs an active modeset; output 3 is an ordinary flip. */
    nv_drm_recover_failed_modeset(&state);
    assert(mock.connectors[0].link_recovery_generation.counter == 1);
    assert(mock.connectors[1].link_recovery_generation.counter == 0);
    assert(mock.connectors[2].link_recovery_generation.counter == 0);
    assert(mock.connectors[3].link_recovery_generation.counter == 0);

    mock.crtc_states[0].mode_changed = false;
    mock.crtc_states[3].connectors_changed = true;
    nv_drm_recover_failed_modeset(&state);
    assert(mock.connectors[3].link_recovery_generation.counter == 1);

    mock.crtc_states[3].connectors_changed = false;
    mock.crtc_states[3].active_changed = true;
    nv_drm_recover_failed_modeset(&state);
    assert(mock.connectors[3].link_recovery_generation.counter == 2);
}

static void test_delayed_detection_with_another_active_output(void)
{
    reset_test_state(4);

    /* HDMI recovery completes while DP's cached connection status is stale. */
    mock.connectors[1].base.status = connector_status_disconnected;
    nv_drm_queue_link_recovery(&mock.connectors[0]);
    nv_drm_queue_link_recovery(&mock.connectors[1]);
    run_recovery_work();
    assert(mock.commit_count == 1);
    assert(mock.connectors[1].link_recovery_handled_generation == 0);
    assert(mock.nv_device.link_recovery_work.pending);

    mock.time_ms += 250;
    mock.connectors[1].base.status = connector_status_connected;
    mock.crtc_states[1].user_mode = 240;
    run_recovery_work();
    assert(mock.commit_count == 2);
    assert(mock.committed_mode == 240);
    assert(mock.connectors[1].link_recovery_handled_generation == 1);
    assert(mock.connectors[1].link_recovery_policy.attempts == 1);
}

static void test_detection_wait_stops_when_recovery_is_unwanted(void)
{
    enum stop_reason {
        CONNECTOR_UNASSIGNED,
        CRTC_DISABLED,
        RECOVERY_PAUSED,
        RECOVERY_DISABLED,
        STOP_REASON_COUNT
    } reason;

    for (reason = 0; reason < STOP_REASON_COUNT; reason++) {
        reset_test_state(1);
        mock.connectors[0].base.status = connector_status_disconnected;
        request_recovery();
        run_recovery_work();
        mock.time_ms += 100;

        switch (reason) {
        case CONNECTOR_UNASSIGNED:
            mock.connector_states[0].crtc = NULL;
            break;
        case CRTC_DISABLED:
            mock.crtc_states[0].active = false;
            break;
        case RECOVERY_PAUSED:
            mock.nv_device.link_recovery_paused = true;
            break;
        case RECOVERY_DISABLED:
            nv_drm_link_recovery_enabled = false;
            break;
        default:
            assert(false);
        }

        run_recovery_work();
        assert(mock.commit_count == 0);
        assert(mock.hotplug_event_count == 0);
        assert(!mock.nv_device.link_recovery_work.pending);
        assert(mock.connectors[0].link_recovery_policy.attempts == 0);
        if (reason == CONNECTOR_UNASSIGNED || reason == CRTC_DISABLED) {
            assert(mock.connectors[0].link_recovery_handled_generation == 1);
            assert(mock.connectors[0].link_recovery_policy.detection_deadline_ms
                   == 0);
        }
    }
}

static void test_detection_deadline_survives_event_storm(void)
{
    NvU64 detection_deadline;
    unsigned int i;

    reset_test_state(1);
    mock.connectors[0].base.status = connector_status_disconnected;
    request_recovery();
    run_recovery_work();
    detection_deadline =
        mock.connectors[0].link_recovery_policy.detection_deadline_ms;

    /* Keep adding requests until just before the original two-second limit. */
    for (i = 0; i < 19; i++) {
        mock.time_ms += 100;
        request_recovery();
        run_recovery_work();
        assert(mock.connectors[0].link_recovery_policy.detection_deadline_ms
               == detection_deadline);
        assert(mock.connectors[0].link_recovery_handled_generation == 0);
    }

    mock.time_ms = detection_deadline;
    run_recovery_work();
    assert(mock.commit_count == 0);
    assert(mock.hotplug_event_count == 0);
    assert(!mock.nv_device.link_recovery_work.pending);
    assert(mock.connectors[0].link_recovery_handled_generation == 20);
    assert(mock.connectors[0].link_recovery_policy.attempts == 0);

    /* Detection alone must not revive an expired request. */
    mock.connectors[0].base.status = connector_status_connected;
    run_recovery_work();
    assert(mock.commit_count == 0);
    request_recovery();
    run_recovery_work();
    assert(mock.commit_count == 1);
}

static void run_test(const char *name, void (*test)(void))
{
    printf("RUN  %s\n", name);
    fflush(stdout);
    test();
    printf("PASS %s\n", name);
}

int main(void)
{
    run_test("idle worker", test_idle_worker);
    run_test("disabled recovery", test_recovery_disabled);
    run_test("inactive outputs", test_inactive_outputs);
    run_test("disconnected output expiry", test_disconnected_output_expires);
    run_test("event coalescing", test_event_coalescing);
    run_test("event during commit", test_event_during_commit);
    run_test("failed attempt retry", test_failed_attempt_is_retried);
    run_test("current configuration after deadlock backoff",
             test_deadlock_backoff_uses_current_configuration);
    run_test("disconnect during backoff", test_disconnect_during_backoff);
    run_test("retry limit and quiet period", test_retry_limit_and_quiet_period);
    run_test("independent connectors", test_connectors_recover_independently);
    run_test("pause during failed commit", test_pause_during_failed_commit);
    run_test("generation wrap", test_generation_wrap);
    run_test("failed modeset routing", test_failed_modeset_routing);
    run_test("delayed detection with another active output",
             test_delayed_detection_with_another_active_output);
    run_test("stop waiting when recovery is unwanted",
             test_detection_wait_stops_when_recovery_is_unwanted);
    run_test("detection deadline during an event storm",
             test_detection_deadline_survives_event_storm);
    return 0;
}
