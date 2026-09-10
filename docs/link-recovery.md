# Deferred display link recovery

A display can be detected and finish link training after standby while its stream remains blank. This change requests a full modeset of the current DRM state when the driver reports that a display link needs recovery. The same path handles DisplayPort and HDMI without matching a monitor, EDID, connector number, resolution, refresh rate, or DSC mode.

## Event sources

- DP attach preparation returns a mask of heads whose preparation failed. Each affected display gets a recovery event, including an initial attach before active-head state is committed. Existing pre/post attach and detach notifications remain balanced.
- A DP sink returning from the library's zombie state or being re-detected requests recovery. The producer preserves the event across transient hardware-head assignments; DRM checks whether the output is currently desired.
- Non-DP hotplug and HDMI FRL retrain requests use the same recovery event.
- Failed active modesets enter the same queue, including post-swap KAPI failures and blocking flip/LUT timeouts. Ordinary flips and commits that turn outputs off do not request recovery.

`NVKMS_EVENT_TYPE_DPY_LINK_RECOVERY` is appended to both copies of the event enum. It uses the existing display-identity payload; the event union and KAPI function-table layouts do not change. Both nvidia-modeset and nvidia-drm need the implementation for end-to-end recovery. Clients that do not subscribe to the new event retain their existing behavior.

## Recovery behavior

One deferred worker per DRM device coalesces events for 100 ms without postponing work for every new event. Each connector has a generation counter, so an event arriving during a commit remains pending.

The event callback marks connection status dirty and schedules the existing hotplug notification to request a fresh probe. The worker holds a connector reference and acquires the DRM connection and CRTC modeset locks. It skips unassigned and inactive outputs. If a desired active output still has cached disconnected status, it retains the request for up to two seconds and checks every 100 ms without spending a modeset attempt. New events do not extend that deadline. Each pass checks the current desired state; disabling or unassigning the output ends the wait. A persistently disconnected output expires without a modeset.

Once connected, recovery duplicates the **current** atomic state through DRM helpers, sets link status GOOD, requests a full modeset, and commits synchronously. Normal atomic checks add affected connectors and planes and manage framebuffer, color, and other state references. Recovery therefore uses the user's current configuration. A shared CRTC or physical MST link can affect more than one screen through the normal modeset path.

The worker does not hold the NvKms callback lock or `mode_config.mutex` across a commit. Flip callbacks remain available while recovery is drained for suspend or removal. Event callbacks stop before hotplug work and connector state are torn down. Deadlock backoff releases temporary atomic state, refunds the attempt budget, reacquires locks, and reads the current configuration again.

Each connector permits three attempts in a burst. A new request after 10 seconds without a processed request rearms the budget; continuous requests keep it exhausted. Failures leave link status BAD and send a hotplug notification after releasing modeset locks. Exhaustion produces one notification and error log per burst. A successful reapply log confirms that the driver accepted the configuration; visible output needs separate verification.

The root-writable Boolean module parameter `nvidia_drm.link_recovery` defaults to enabled and can disable new recovery attempts without unloading the driver. An in-flight commit finishes normally.

## Validation and scope

The regression harness compiles production function bodies with fault-injectable doubles under AddressSanitizer and UndefinedBehaviorSanitizer. It covers state lifetime, retry policy, event generations, detection delays, failed-modeset routing, and all 4096 DP head/attach/failure combinations. See [test instructions and limitations](../tests/link-recovery/README.md).

The wake recovery logic completed three consecutive user-confirmed display-off/wake cycles with four connected displays: a Samsung G93SC on DP at 5120x1440, 240 Hz with DSC, two other DP displays, and an HDMI display. Both DP and HDMI recovery completed in every cycle. The delayed connector-detection path ran in two cycles; detection had already caught up in the third. The same boot also restored all four displays.

Those hardware observations used an experimental NVIDIA 610.57.04 build on Ubuntu kernel 7.0.0-31-generic. It also contained diagnostic tracing, a separate overlay fix, and an initial DP link-assessment retry that did not run during the successful boot or wake tests. This branch excludes those changes. The isolated, trimmed build still needs hardware validation; the earlier results are evidence for the retained recovery logic, not a test of this exact branch.

Initial link assessment without usable modes is outside this change's scope. Remaining coverage includes repeated boots, long-term use, unplug and configuration changes during recovery, DP MST, HDMI TMDS, HDR/SDR transitions, suspend/resume, and teardown with kernel locking diagnostics. The mock tests cannot establish electrical link behavior or monitor firmware recovery.
