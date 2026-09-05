static NVDevEvoRec dev;
static NVDispEvoRec disp;
static NVEvoIsModePossibleDispInput input;
static NVHwModeTimingsEvo timings[4];
static struct NvKmsUsageBounds usage[4];
static void setup(void)
{
    memset(&dev, 0, sizeof(dev));
    memset(&input, 0, sizeof(input));
    memset(usage, 0, sizeof(usage));
    dev.numHeads = 4;
    dev.numHwTiles = 8;
    dev.numHwPhywins = 8;
    disp.pDevEvo = &dev;
    disp.displayOwner = 0;
    for (NvU32 h = 0; h < 4; h++) {
        dev.head[h].numLayers = 2;
        input.head[h].pTimings = &timings[h];
        input.head[h].pUsage = &usage[h];
        input.head[h].modesetRequested = TRUE;
        input.head[h].multiTileConfig.tilesMask = NVBIT(7 - h);
        input.head[h].multiTileConfig.phywinsMask[0] = NVBIT(2 * h);
        input.head[h].multiTileConfig.phywinsMask[1] = NVBIT(2 * h + 1);
        usage[h].layer[0].usable = TRUE;
    }
}
static void checkUnique(NVEvoIsModePossibleDispOutput *out)
{
    NvU32 tiles = 0, windows = 0;
    for (NvU32 h = 0; h < 4; h++) {
        NVHwHeadMultiTileConfigRec *c = &out->head[h].multiTileConfig;
        assert(!(tiles & c->tilesMask));
        tiles |= c->tilesMask;
        for (NvU32 l = 0; l < 2; l++) {
            assert(!(windows & c->phywinsMask[l]));
            windows |= c->phywinsMask[l];
            if (usage[h].layer[l].usable)
                assert(nvPopCount32(c->phywinsMask[l]) == nvPopCount32(c->tilesMask));
            else
                assert(c->phywinsMask[l] == 0);
        }
    }
}
int main(void)
{
    NvU32 required[4] = {1, 1, 2, 2};
    NVEvoIsModePossibleDispOutput out = {0};
    setup();
    assert(EvoAssignHwHeadMultiTileConfigDispOutputCA(&disp, &input, required, &out));
    checkUnique(&out);
    puts("PASS: four heads, six tiles, unused overlays released");
    setup();
    for (NvU32 h = 0; h < 4; h++)
        usage[h].layer[1].usable = TRUE;
    assert(!EvoAssignHwHeadMultiTileConfigDispOutputCA(&disp, &input, required, &out));
    puts("PASS: twelve required physical windows still rejected");
    setup();
    for (NvU32 h = 0; h < 4; h++) {
        required[h] = 1;
        input.head[h].multiTileConfig.phywinsMask[1] = 0;
    }
    usage[0].layer[1].usable = TRUE;
    assert(EvoAssignHwHeadMultiTileConfigDispOutputCA(&disp, &input, required, &out));
    checkUnique(&out);
    puts("PASS: enabling a layer allocates windows without changing tile count");
    setup();
    usage[0].layer[1].usable = TRUE;
    input.head[0].modesetRequested = FALSE;
    required[2] = 2;
    required[3] = 2;
    assert(EvoAssignHwHeadMultiTileConfigDispOutputCA(&disp, &input, required, &out));
    assert(!memcmp(&out.head[0].multiTileConfig, &input.head[0].multiTileConfig,
                   sizeof(NVHwHeadMultiTileConfigRec)));
    checkUnique(&out);
    puts("PASS: unchanged head retains its assignments");
    input.head[0].multiTileConfig.phywinsMask[1] = 0;
    assert(!EvoAssignHwHeadMultiTileConfigDispOutputCA(&disp, &input, required, &out));
    puts("PASS: unchanged head cannot enable an unassigned overlay without a modeset");

    NVProposedModeSetHwStateOneDisp proposed = {0};
    memset(disp.headState, 0, sizeof(disp.headState));
    disp.apiHeadState[0].hwHeadsMask = 1;
    disp.headState[0].multiTileConfig.phywinsMask[1] = 2;
    proposed.head[0].multiTileConfig.phywinsMask[1] = 2;
    assert(!IsCurrentMultiTileConfigOneApiHeadIncompatible(&disp, 0, &proposed));
    proposed.head[0].multiTileConfig.phywinsMask[1] = 0;
    proposed.head[0].multiTileConfig.phywinsMask[0] = 2;
    assert(IsCurrentMultiTileConfigOneApiHeadIncompatible(&disp, 0, &proposed));
    proposed.head[0].multiTileConfig.phywinsMask[0] = 0;
    proposed.head[1].multiTileConfig.phywinsMask[0] = 2;
    assert(IsCurrentMultiTileConfigOneApiHeadIncompatible(&disp, 0, &proposed));
    puts("PASS: window ownership changes require shutdown, retained ownership does not");
    memset(disp.headState, 0, sizeof(disp.headState));
    memset(&proposed, 0, sizeof(proposed));
    disp.headState[0].multiTileConfig.phywinsMask[0] = 1;
    proposed.head[0].multiTileConfig.phywinsMask[0] = 1;
    proposed.head[0].multiTileConfig.phywinsMask[1] = 2;
    assert(IsCurrentMultiTileConfigOneApiHeadIncompatible(&disp, 0, &proposed));
    disp.headState[0].multiTileConfig.phywinsMask[1] = 2;
    assert(!IsCurrentMultiTileConfigOneApiHeadIncompatible(&disp, 0, &proposed));
    proposed.head[0].multiTileConfig.phywinsMask[1] = 0;
    assert(IsCurrentMultiTileConfigOneApiHeadIncompatible(&disp, 0, &proposed));
    puts("PASS: binding or unbinding an overlay requires shutdown even without resource transfer");

    return 0;
}
