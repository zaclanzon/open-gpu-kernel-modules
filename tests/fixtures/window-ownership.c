int main(void)
{
    NVEvoChannel core = {0};
    NVEvoChannel base = {2};
    NVEvoChannel overlay = {3};
    TestSubDevice subDevice = {stateCache};
    NVDevEvoRec dev = {
        .core = &core,
        .pSubDevices = {&subDevice},
    };
    NVDispEvoRec disp = {&dev, 0};
    NVEvoModesetUpdateState update = {0};
    NVHwModeTimingsEvo timing = {0};
    NVDscInfoEvoRec dsc = {0};
    NVHwHeadMultiTileConfigRec config = {
        .tilesMask = 3,
        .phywinsMask = {3, 0},
    };

    dev.head[1].numLayers = 2;
    dev.head[1].layer[0] = &base;
    dev.head[1].layer[1] = &overlay;

    /* Simulate stock window owners, including the unrelated HIDE control bit. */
    stateCache[NVCA7D_WINDOW_SET_CONTROL(2)] = 1;
    stateCache[NVCA7D_WINDOW_SET_CONTROL(3)] = 0x101;
    stateCache[NVCA7D_WINDOW_SET_PHYSICAL(2)] = 1;
    stateCache[NVCA7D_WINDOW_SET_PHYSICAL(3)] = 2;
    memcpy(pendingMethods, stateCache, sizeof(stateCache));

    EvoSetMultiTileConfigCA(&disp, 1, &timing, &dsc, &config, &update);

    assert(GetPendingWindowOwner(2) == 1);
    assert(GetPendingWindowOwner(3) == NVCA7D_WINDOW_SET_CONTROL_OWNER_NONE);
    assert(pendingMethods[NVCA7D_WINDOW_SET_CONTROL(3)] == 0x10f);
    assert(pendingMethods[NVCA7D_WINDOW_SET_PHYSICAL(2)] == 3);
    assert(pendingMethods[NVCA7D_WINDOW_SET_PHYSICAL(3)] == 0);
    assert(update.windowMappingChanged);
    assert(update.updateState.noCoreInterlockMask == 12);
    puts("PASS: unused overlay is unowned with no physical windows; "
         "base remains bound");

    /* Repeating the same configuration must not introduce an interlock change. */
    CommitPendingMethods();
    memset(&update, 0, sizeof(update));

    EvoSetMultiTileConfigCA(&disp, 1, &timing, &dsc, &config, &update);

    assert(!update.windowMappingChanged);
    assert(!update.updateState.noCoreInterlockMask);
    puts("PASS: unchanged allocation/ownership introduces no extra interlock "
         "changes");

    /* Restoring overlay resources also restores its original hardware owner. */
    config.tilesMask = 1;
    config.phywinsMask[0] = 1;
    config.phywinsMask[1] = 2;

    EvoSetMultiTileConfigCA(&disp, 1, &timing, &dsc, &config, &update);

    assert(GetPendingWindowOwner(3) == 1);
    assert(pendingMethods[NVCA7D_WINDOW_SET_PHYSICAL(3)] == 2);
    assert(update.windowMappingChanged);
    assert(update.updateState.noCoreInterlockMask & 8);
    puts("PASS: re-enabled overlay regains its owner and physical assignment");

    /* Shutdown releases physical windows while leaving owners unchanged. */
    CommitPendingMethods();
    memset(&update, 0, sizeof(update));
    memset(&config, 0, sizeof(config));

    EvoSetMultiTileConfigCA(&disp, 1, NULL, NULL, &config, &update);

    assert(GetPendingWindowOwner(2) == 1);
    assert(GetPendingWindowOwner(3) == 1);
    assert(pendingMethods[NVCA7D_WINDOW_SET_PHYSICAL(2)] == 0);
    assert(pendingMethods[NVCA7D_WINDOW_SET_PHYSICAL(3)] == 0);
    puts("PASS: shutdown clears physical assignments without changing "
         "ownership in that update");

    /* Only the following activation update removes the unused window owner. */
    CommitPendingMethods();
    memset(&update, 0, sizeof(update));
    config.tilesMask = 3;
    config.phywinsMask[0] = 3;

    EvoSetMultiTileConfigCA(&disp, 1, &timing, &dsc, &config, &update);

    assert(GetPendingWindowOwner(2) == 1);
    assert(GetPendingWindowOwner(3) == NVCA7D_WINDOW_SET_CONTROL_OWNER_NONE);
    assert(update.windowMappingChanged);
    assert(update.updateState.noCoreInterlockMask & 8);
    puts("PASS: subsequent activation unbinds the unused overlay with a "
         "non-interlocked update");

    /*
     * Initialization can queue a default binding before this helper runs.
     * PIO still describes the previous unowned state. The final owner write
     * must override the queued binding, even though PIO matches our target.
     */
    stateCache[NVCA7D_WINDOW_SET_CONTROL(2)] = 1;
    stateCache[NVCA7D_WINDOW_SET_CONTROL(3)] = NVCA7D_WINDOW_SET_CONTROL_OWNER_NONE;
    stateCache[NVCA7D_WINDOW_SET_PHYSICAL(2)] = 3;
    stateCache[NVCA7D_WINDOW_SET_PHYSICAL(3)] = 0;
    memcpy(pendingMethods, stateCache, sizeof(stateCache));
    pendingMethods[NVCA7D_WINDOW_SET_CONTROL(3)] = 1;
    memset(&update, 0, sizeof(update));
    update.windowMappingChanged = TRUE;
    update.updateState.noCoreInterlockMask = 8;

    EvoSetMultiTileConfigCA(&disp, 1, &timing, &dsc, &config, &update);

    assert(GetPendingWindowOwner(3) == NVCA7D_WINDOW_SET_CONTROL_OWNER_NONE);
    assert(update.windowMappingChanged);
    assert(update.updateState.noCoreInterlockMask & 8);
    puts("PASS: an unused overlay overrides a queued initialization binding");

    return 0;
}
