static void MultiTileInit(void *pDevEvo) { (void)pDevEvo; }
static const NVEvoHalRec blackwellHal = { MultiTileInit };
static const NVEvoHalRec olderHal = { NULL };
static const NVConnectorEvoRec dpConnector = {
    NVKMS_CONNECTOR_SIGNAL_FORMAT_DP, NVKMS_CONNECTOR_TYPE_DP
};
static const NVConnectorEvoRec hdmiConnector = {
    NVKMS_CONNECTOR_SIGNAL_FORMAT_TMDS, NVKMS_CONNECTOR_TYPE_HDMI
};

enum Kind { EMPTY, HDMI, DP, DP_DSC, DP_MST_DSC, DP_DSC_2H1OR, HDMI_2H1OR, KIND_COUNT };

typedef NvBool (*AssignFn)(const NVDispEvoRec *, NVProposedModeSetHwStateOneDisp *, NvU32 *,
                           const NvBool);

/* The generic part of AssignProposedHwHeadsOneDisp(): 2Heads1OR first. */
static NvBool Assign(AssignFn assign, const NVDispEvoRec *pDispEvo,
                     NVProposedModeSetHwStateOneDisp *pProposedDisp, NvU32 *pFreeHwHeadsMask)
{
    return assign(pDispEvo, pProposedDisp, pFreeHwHeadsMask, TRUE) &&
           assign(pDispEvo, pProposedDisp, pFreeHwHeadsMask, FALSE);
}

static void Setup(NVDevEvoRec *pDevEvo, NVDispEvoRec *pDispEvo,
                  NVProposedModeSetHwStateOneDisp *pProposedDisp, const enum Kind kinds[4])
{
    pDispEvo->pDevEvo = pDevEvo;
    memset(pProposedDisp, 0, sizeof(*pProposedDisp));
    for (int i = 0; i < 4; i++) {
        NVDpyEvoRec *pDpy = &pDispEvo->dpys[i];
        const enum Kind kind = kinds[i];
        const NvBool hdmi = (kind == HDMI) || (kind == HDMI_2H1OR);

        memset(pDpy, 0, sizeof(*pDpy));
        pDpy->pConnectorEvo = hdmi ? &hdmiConnector : &dpConnector;
        pDpy->usesDPLib = !hdmi;
        pDpy->mst = (kind == DP_MST_DSC);
        pDpy->dscPossible = (kind == DP_DSC) || (kind == DP_MST_DSC) || (kind == DP_DSC_2H1OR);
        pDpy->use2Heads1OR = (kind == DP_DSC_2H1OR) || (kind == HDMI_2H1OR);
        pProposedDisp->apiHead[i].dpyIdList = (kind == EMPTY) ? -1 : i;
        pProposedDisp->apiHead[i].changed = TRUE;
    }
}

/* Independent of AvoidHwHead0() so the test does not trust the code under test. */
static NvBool ExpectAvoid(const NVDevEvoRec *pDevEvo, const enum Kind kind)
{
    return (pDevEvo->hal == &blackwellHal) && ((kind == DP_DSC) || (kind == DP_DSC_2H1OR));
}

static NvU32 avoidedOnHead0 = 0, moved = 0, swapped = 0, unavoidable = 0, compared = 0;

static void CheckValid(const NVDevEvoRec *pDevEvo, const NVProposedModeSetHwStateOneDisp *p,
                       const enum Kind kinds[4], const NvU32 initialFree, const NvU32 finalFree)
{
    NvU32 used = 0;

    for (int i = 0; i < 4; i++) {
        const NvU32 mask = p->apiHead[i].hwHeadsMask;

        if (kinds[i] == EMPTY) {
            assert(mask == 0);
            continue;
        }
        assert(nvPopCount32(mask) == (((kinds[i] == DP_DSC_2H1OR) || (kinds[i] == HDMI_2H1OR)) ? 2U : 1U));
        assert(p->apiHead[i].attributes.numberOfHardwareHeadsUsed == nvPopCount32(mask));
        assert((mask & ~initialFree) == 0);
        assert((mask & used) == 0);
        used |= mask;
        for (NvU32 head = 0; head < 4; head++) {
            if (mask & NVBIT(head)) {
                assert(pDevEvo->apiHead[i].numLayers <= pDevEvo->head[head].numLayers);
            }
        }
    }
    assert(finalFree == (initialFree & ~used));
}

static void RunOne(NVDevEvoRec *pDevEvo, const enum Kind kinds[4], const NvU32 initialFree)
{
    NVDispEvoRec disp;
    NVProposedModeSetHwStateOneDisp patched, reference;
    NvU32 patchedFree = initialFree, referenceFree = initialFree;
    int refAvoiderOnHead0 = -1, avoiderOnHead0 = -1;

    Setup(pDevEvo, &disp, &patched, kinds);
    reference = patched;

    const NvBool patchedOk = Assign(AssignProposedHwHeadsGeneric, &disp, &patched, &patchedFree);
    const NvBool referenceOk =
        Assign(ReferenceAssignProposedHwHeadsGeneric, &disp, &reference, &referenceFree);

    /* The move never turns a stock success into a failure, or the reverse. */
    assert(patchedOk == referenceOk);
    if (!patchedOk) {
        return;
    }
    CheckValid(pDevEvo, &patched, kinds, initialFree, patchedFree);
    CheckValid(pDevEvo, &reference, kinds, initialFree, referenceFree);

    for (int i = 0; i < 4; i++) {
        if ((kinds[i] != EMPTY) && ExpectAvoid(pDevEvo, kinds[i])) {
            if (reference.apiHead[i].hwHeadsMask == NVBIT(0)) {
                refAvoiderOnHead0 = i;
            }
            if (patched.apiHead[i].hwHeadsMask == NVBIT(0)) {
                avoiderOnHead0 = i;
            }
        }
    }

    /* Stock results stand unless stock put a dpy to avoid on head 0. */
    if (refAvoiderOnHead0 < 0) {
        for (int i = 0; i < 4; i++) {
            assert(patched.apiHead[i].hwHeadsMask == reference.apiHead[i].hwHeadsMask);
        }
        assert(avoiderOnHead0 < 0);
        compared++;
        return;
    }
    avoidedOnHead0++;

    if (avoiderOnHead0 < 0) {
        if (patchedFree != referenceFree) {
            moved++;
        } else {
            swapped++;
        }
        return;
    }

    /* Still on head 0: no free head and no single swap may have been possible. */
    unavoidable++;
    const int a = avoiderOnHead0;
    for (NvU32 head = 1; head < pDevEvo->numHeads; head++) {
        if (pDevEvo->apiHead[a].numLayers > pDevEvo->head[head].numLayers) {
            continue;
        }
        assert((patchedFree & NVBIT(head)) == 0);
        for (int b = 0; b < 4; b++) {
            if ((kinds[b] != EMPTY) && (patched.apiHead[b].hwHeadsMask == NVBIT(head))) {
                assert(ExpectAvoid(pDevEvo, kinds[b]) ||
                       (pDevEvo->apiHead[b].numLayers > pDevEvo->head[0].numLayers));
            }
        }
    }
}

static void ZSystem(const enum Kind kinds[4], const NvU32 expected[4], const char *name)
{
    NVDevEvoRec dev = { &blackwellHal, 4, 4, { { 2 }, { 2 }, { 2 }, { 2 } },
                        { { 2 }, { 2 }, { 2 }, { 2 } } };
    NVDispEvoRec disp;
    NVProposedModeSetHwStateOneDisp proposed;
    NvU32 freeHwHeadsMask = 0xf;

    Setup(&dev, &disp, &proposed, kinds);
    assert(Assign(AssignProposedHwHeadsGeneric, &disp, &proposed, &freeHwHeadsMask));
    for (int i = 0; i < 4; i++) {
        if (proposed.apiHead[i].hwHeadsMask != expected[i]) {
            fprintf(stderr, "%s: api head %d got 0x%x, expected 0x%x\n", name, i,
                    proposed.apiHead[i].hwHeadsMask, expected[i]);
            assert(0);
        }
    }
    printf("%s: ok\n", name);
}

int main(void)
{
    /*
     * Layouts traced on 2026-10-31, api heads in CRTC order. MSI is HDMI DSC,
     * the G9 is DP SST DSC, HYTE and LG are DP without DSC.
     */
    const enum Kind usual[4] = { HDMI, DP_DSC, DP, DP };          /* MSI G9 HYTE LG */
    const NvU32 usualHeads[4] = { NVBIT(3), NVBIT(2), NVBIT(1), NVBIT(0) };
    const enum Kind fallback[4] = { DP, DP, HDMI, DP_DSC };       /* HYTE LG MSI G9 */
    const NvU32 fallbackHeads[4] = { NVBIT(0), NVBIT(2), NVBIT(1), NVBIT(3) };
    ZSystem(usual, usualHeads, "usual layout keeps stock heads");
    ZSystem(fallback, fallbackHeads, "fallback layout moves the G9 off head 0");

    const NvU32 layerPatterns[4][4] = {
        { 2, 2, 2, 2 }, { 1, 2, 2, 2 }, { 2, 2, 2, 1 }, { 1, 1, 2, 2 },
    };
    const NvU32 apiLayerPatterns[4][4] = {
        { 1, 1, 1, 1 }, { 2, 2, 2, 2 }, { 2, 1, 1, 2 }, { 1, 2, 2, 1 },
    };
    const NVEvoHalRec *hals[2] = { &blackwellHal, &olderHal };
    NvU32 runs = 0;

    for (int h = 0; h < 2; h++)
    for (NvU32 numHeads = 2; numHeads <= 4; numHeads++)
    for (int lp = 0; lp < 4; lp++)
    for (int ap = 0; ap < 4; ap++)
    for (NvU32 initialFree = 0; initialFree < NVBIT(numHeads); initialFree++)
    for (int k0 = 0; k0 < KIND_COUNT; k0++)
    for (int k1 = 0; k1 < KIND_COUNT; k1++)
    for (int k2 = 0; k2 < KIND_COUNT; k2++)
    for (int k3 = 0; k3 < KIND_COUNT; k3++) {
        NVDevEvoRec dev = { hals[h], numHeads, 4, { { 0 } }, { { 0 } } };
        const enum Kind kinds[4] = { k0, k1, k2, k3 };

        for (int i = 0; i < 4; i++) {
            dev.head[i].numLayers = layerPatterns[lp][i];
            dev.apiHead[i].numLayers = apiLayerPatterns[ap][i];
        }
        RunOne(&dev, kinds, initialFree);
        runs++;
    }

    printf("%u layouts: %u match stock, %u had a DSC dpy on head 0 in stock "
           "(%u moved to a free head, %u swapped, %u unavoidable)\n",
           runs, compared, avoidedOnHead0, moved, swapped, unavoidable);
    assert(moved > 0 && swapped > 0 && unavoidable > 0);
    return 0;
}
