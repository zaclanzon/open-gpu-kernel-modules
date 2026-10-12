int main(void)
{
    Hal hal = {update};
    NVDevEvoRec dev = {1, &hal};
    Disp disp = {&dev};
    NVEvoChannel channel = {4};
    NVModeSetWorkArea work = {0};
    NVProposedModeSetHwState proposed = {0};
    NVProposedModeSetHwStateOneDisp one = {0};
    EvoTrackPhysicalWindowChangeCA(&dev, &channel, 1, 1, &work.modesetUpdateState);
    assert(!work.modesetUpdateState.windowMappingChanged);
    assert(!work.modesetUpdateState.updateState.noCoreInterlockMask);
    EvoTrackPhysicalWindowChangeCA(&dev, &channel, 1, 0, &work.modesetUpdateState);
    assert(work.modesetUpdateState.windowMappingChanged);
    assert(work.modesetUpdateState.updateState.noCoreInterlockMask == 4);
    puts("PASS: changed physical assignment disables its core interlock; unchanged is untouched");
    memset(&work, 0, sizeof(work));
    changeInPre = 1;
    KickoffProposedModeSetHwState(&disp, &proposed, &one, FALSE, &work);
    assert(n == 3 && memcmp(events, "CFU", 3) == 0);
    puts("PASS: assignment changed during PreUpdate -> core update, then flip update");
    n = 0;
    changeInPre = 0;
    memset(&work, 0, sizeof(work));
    KickoffProposedModeSetHwState(&disp, &proposed, &one, FALSE, &work);
    assert(n == 2 && memcmp(events, "FC", 2) == 0);
    puts("PASS: unchanged assignment retains combined flip/core update");
    n = 0;
    work.modesetUpdateState.windowMappingChanged = TRUE;
    KickoffProposedModeSetHwState(&disp, &proposed, &one, FALSE, &work);
    assert(n == 3 && memcmp(events, "CFU", 3) == 0);
    puts("PASS: existing logical mapping change still decouples updates");
}
