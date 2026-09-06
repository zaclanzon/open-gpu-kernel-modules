static NvBool CompleteAllocation(const NVEvoIsModePossibleDispOutput *pOutput, NvU32 heads,
                                 NvU32 layers, const NvU32 required[4])
{
    NvU32 usedTiles = 0;
    NvU32 usedWindows = 0;

    for (NvU32 head = 0; head < heads; head++) {
        const NVHwHeadMultiTileConfigRec *pConfig = &pOutput->head[head].multiTileConfig;
        if (nvPopCount32(pConfig->tilesMask) != required[head] ||
            (pConfig->tilesMask & (usedTiles | ~0xffU))) {
            return FALSE;
        }
        usedTiles |= pConfig->tilesMask;
        for (NvU32 layer = 0; layer < layers; layer++) {
            NvU32 mask = pConfig->phywinsMask[layer];
            if (nvPopCount32(mask) != required[head] || (mask & (usedWindows | ~0xffU))) {
                return FALSE;
            }
            usedWindows |= mask;
        }
    }
    return TRUE;
}

static void SameAllocation(const NVEvoIsModePossibleDispOutput *pReference,
                           const NVEvoIsModePossibleDispOutput *pActual, NvU32 heads, NvU32 layers)
{
    for (NvU32 head = 0; head < heads; head++) {
        assert(pReference->head[head].multiTileConfig.tilesMask ==
               pActual->head[head].multiTileConfig.tilesMask);
        for (NvU32 layer = 0; layer < layers; layer++) {
            assert(pReference->head[head].multiTileConfig.phywinsMask[layer] ==
                   pActual->head[head].multiTileConfig.phywinsMask[layer]);
        }
    }
}

static unsigned identical;
static unsigned rejected;
static unsigned corrected;
static unsigned unchanged;

static void TestConfiguration(NvU32 heads, NvU32 layers, NvU32 scalerTiles, NvU32 scalingMask,
                              NvU32 tileChoices, NvBool reuse)
{
    NVDevEvoRec dev = {.numHeads = heads, .numHwTiles = 8, .numHwPhywins = 8};
    NVDispEvoRec disp = {.pDevEvo = &dev};
    NVEvoIsModePossibleDispInput input = {0};
    NVEvoIsModePossibleDispOutput reference = {0};
    NVEvoIsModePossibleDispOutput actual = {0};
    NVHwModeTimingsEvo timings[4] = {0};
    struct NvKmsUsageBounds usage[4] = {0};
    NvU32 required[4] = {0};
    NvU32 requiredTiles = 0;
    NvU32 requiredScalerTiles = 0;

    for (NvU32 tile = 0; tile < 8; tile++) {
        dev.gpus[0].capabilities.hwTile[tile].type =
            tile < scalerTiles ? NV_EVO_HW_TILE_TYPE_0 : NV_EVO_HW_TILE_TYPE_1;
    }
    for (NvU32 head = 0; head < heads; head++) {
        dev.head[head].numLayers = layers;
        input.head[head].pTimings = &timings[head];
        input.head[head].pUsage = &usage[head];
        input.head[head].modesetRequested = TRUE;
        timings[head].viewPort.in.width = 1920;
        timings[head].viewPort.out.width = scalingMask & NVBIT(head) ? 1280 : 1920;
        required[head] = 1 + ((tileChoices >> (2 * head)) & 3);
        requiredTiles += required[head];
        if (scalingMask & NVBIT(head)) {
            requiredScalerTiles += required[head];
        }
        if (reuse) {
            input.head[head].multiTileConfig.tilesMask = NVBIT(head);
        }
        for (NvU32 layer = 0; layer < layers; layer++) {
            usage[head].layer[layer].usable = TRUE;
            if (reuse) {
                input.head[head].multiTileConfig.phywinsMask[layer] = NVBIT(head * layers + layer);
            }
        }
    }
    if (requiredTiles * layers > 8) {
        return;
    }
    NvBool expected = requiredScalerTiles <= scalerTiles;
    NvBool referenceResult =
        ReferenceEvoAssignHwHeadMultiTileConfigDispOutputCA(&disp, &input, required, &reference);
    NvBool actualResult =
        EvoAssignHwHeadMultiTileConfigDispOutputCA(&disp, &input, required, &actual);
    assert(actualResult == expected);
    if (actualResult) {
        assert(CompleteAllocation(&actual, heads, layers, required));
    }
    if (referenceResult && !CompleteAllocation(&reference, heads, layers, required)) {
        corrected++;
        return;
    }
    assert(referenceResult == actualResult);
    if (!actualResult) {
        rejected++;
        return;
    }
    SameAllocation(&reference, &actual, heads, layers);
    identical++;

    /* Replay with every subset of heads marked unchanged. */
    for (NvU32 head = 0; head < heads; head++) {
        input.head[head].multiTileConfig = actual.head[head].multiTileConfig;
    }
    for (NvU32 changedMask = 0; changedMask < NVBIT(heads); changedMask++) {
        for (NvU32 head = 0; head < heads; head++) {
            input.head[head].modesetRequested = !!(changedMask & NVBIT(head));
        }
        assert(ReferenceEvoAssignHwHeadMultiTileConfigDispOutputCA(&disp, &input, required,
                                                                   &reference));
        assert(EvoAssignHwHeadMultiTileConfigDispOutputCA(&disp, &input, required, &actual));
        SameAllocation(&reference, &actual, heads, layers);
        unchanged++;
    }
}

int main(void)
{
    for (NvU32 heads = 1; heads <= 4; heads++) {
        for (NvU32 layers = 1; layers <= 2; layers++) {
            for (NvU32 scalerTiles = 0; scalerTiles <= 8; scalerTiles++) {
                for (NvU32 scalingMask = 0; scalingMask < NVBIT(heads); scalingMask++) {
                    for (NvU32 tiles = 0; tiles < NVBIT(2 * heads); tiles++) {
                        TestConfiguration(heads, layers, scalerTiles, scalingMask, tiles, FALSE);
                        TestConfiguration(heads, layers, scalerTiles, scalingMask, tiles, TRUE);
                    }
                }
            }
        }
    }
    printf("PASS: %u complete reference allocations exactly match\n", identical);
    printf("PASS: %u unchanged-head replays exactly match\n", unchanged);
    printf("PASS: %u impossible configurations rejected by both\n", rejected);
    printf("PASS: %u incomplete upstream results handled by the retry fix\n", corrected);
    return 0;
}
