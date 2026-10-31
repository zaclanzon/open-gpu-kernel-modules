static void MultiTileInit(void *pDevEvo) { (void)pDevEvo; }
static const NVEvoHalRec blackwellHal = { MultiTileInit };
static const NVEvoHalRec olderHal = { NULL };
static const NVConnectorEvoRec dpConnector = {
    NVKMS_CONNECTOR_SIGNAL_FORMAT_DP, NVKMS_CONNECTOR_TYPE_DP, { FALSE }
};
static const NVConnectorEvoRec hdmiConnector = {
    NVKMS_CONNECTOR_SIGNAL_FORMAT_TMDS, NVKMS_CONNECTOR_TYPE_HDMI, { FALSE }
};
static const NVConnectorEvoRec serializerConnector = {
    NVKMS_CONNECTOR_SIGNAL_FORMAT_DP, NVKMS_CONNECTOR_TYPE_DP_SERIALIZER, { FALSE }
};

enum Kind {
    EMPTY, HDMI, DP, DP_DSC, DP_MST_DSC, DP_DSC_2H1OR, HDMI_2H1OR, SERIALIZER_DSC, KIND_COUNT
};

static NvBool Is2Heads1OR(const enum Kind kind)
{
    return (kind == DP_DSC_2H1OR) || (kind == HDMI_2H1OR);
}

/* Independent of AvoidHwHead0() so the test does not trust the code under test. */
static NvBool ExpectAvoid(const NVDevEvoRec *pDevEvo, const enum Kind kind)
{
    return (pDevEvo->hal == &blackwellHal) && ((kind == DP_DSC) || (kind == DP_DSC_2H1OR));
}

/* The current configuration of one api-head before the modeset. */
typedef struct {
    enum Kind kind;
    NvU32 hwHeadsMask;
    NvBool changed;
} ApiHeadState;

static void SetDpy(NVDispEvoRec *pDispEvo, const int i, const enum Kind kind)
{
    NVDpyEvoRec *pDpy = &pDispEvo->dpys[i];
    const NvBool hdmi = (kind == HDMI) || (kind == HDMI_2H1OR);

    memset(pDpy, 0, sizeof(*pDpy));
    pDpy->pConnectorEvo = hdmi ? &hdmiConnector :
        ((kind == SERIALIZER_DSC) ? &serializerConnector : &dpConnector);
    pDpy->usesDPLib = !hdmi;
    pDpy->mst = (kind == DP_MST_DSC);
    pDpy->dscPossible = (kind == DP_DSC) || (kind == DP_MST_DSC) ||
        (kind == DP_DSC_2H1OR) || (kind == SERIALIZER_DSC);
    pDpy->use2Heads1OR = Is2Heads1OR(kind);
}

/* Programs the per-hardware-head state the way the modeset after assignment does. */
static void Program(NVProposedModeSetHwState *p, const int apiHead)
{
    for (NvU32 head = 0; head < 4; head++) {
        if (p->disp[0].apiHead[apiHead].hwHeadsMask & NVBIT(head)) {
            p->disp[0].head[head].owner = apiHead + 1;
            p->sd[0].head[head].flip.owner = apiHead + 1;
        }
    }
}

static void Setup(NVDevEvoRec *pDevEvo, NVDispEvoRec *pDispEvo, NVProposedModeSetHwState *p,
                  const ApiHeadState state[4])
{
    pDispEvo->pDevEvo = pDevEvo;
    pDispEvo->displayOwner = 0;
    memset(p, 0, sizeof(*p));
    for (int i = 0; i < 4; i++) {
        NVProposedModeSetStateOneApiHead *pApiHead = &p->disp[0].apiHead[i];

        SetDpy(pDispEvo, i, state[i].kind);
        pApiHead->dpyIdList = (state[i].kind == EMPTY) ? -1 : i;
        pApiHead->changed = state[i].changed;
        nvAssignHwHeadsMaskProposedApiHead(pApiHead, state[i].hwHeadsMask);
        Program(p, i);
    }
}

static NvU32 failed = 0, avoidedOnHead0 = 0, moved = 0, swapped = 0, unavoidable = 0, compared = 0;

static NvU32 UsedHeads(const NVProposedModeSetHwState *p)
{
    NvU32 used = 0;

    for (int i = 0; i < 4; i++) {
        used |= p->disp[0].apiHead[i].hwHeadsMask;
    }
    return used;
}

static void CheckValid(const NVDevEvoRec *pDevEvo, const NVProposedModeSetHwState *p,
                       const ApiHeadState state[4])
{
    NvU32 used = 0;

    for (int i = 0; i < 4; i++) {
        const NvU32 mask = p->disp[0].apiHead[i].hwHeadsMask;

        assert(p->disp[0].apiHead[i].attributes.numberOfHardwareHeadsUsed == nvPopCount32(mask));
        if (state[i].kind == EMPTY) {
            assert(mask == 0);
            continue;
        }
        if (!state[i].changed) {
            assert(mask == state[i].hwHeadsMask);
        } else {
            assert(nvPopCount32(mask) == (Is2Heads1OR(state[i].kind) ? 2U : 1U));
        }
        assert((mask & ~(NVBIT(pDevEvo->numHeads) - 1)) == 0);
        assert((mask & used) == 0);
        used |= mask;
        for (NvU32 head = 0; head < 4; head++) {
            if (mask & NVBIT(head)) {
                assert(pDevEvo->apiHead[i].numLayers <= pDevEvo->head[head].numLayers);
                /* Never another api-head's state; unchanged heads keep theirs. */
                assert((p->disp[0].head[head].owner == 0) ||
                       (p->disp[0].head[head].owner == i + 1));
                assert(p->sd[0].head[head].flip.owner == p->disp[0].head[head].owner);
                if (!state[i].changed) {
                    assert(p->disp[0].head[head].owner == i + 1);
                }
            }
        }
    }
    for (NvU32 head = 0; head < 4; head++) {
        if (!(used & NVBIT(head))) {
            assert(p->disp[0].head[head].owner == 0);
            assert(p->sd[0].head[head].flip.owner == 0);
        }
    }
}

static void RunOne(NVDevEvoRec *pDevEvo, const ApiHeadState state[4])
{
    NVDispEvoRec disp;
    NVProposedModeSetHwState patched, reference;
    int refAvoiderOnHead0 = -1, avoiderOnHead0 = -1;

    Setup(pDevEvo, &disp, &patched, state);
    reference = patched;

    const NvBool patchedOk = AssignProposedHwHeadsOneDisp(&disp, &patched);
    const NvBool referenceOk = ReferenceAssignProposedHwHeadsOneDisp(&disp, &reference);

    /* The move never turns a stock success into a failure, or the reverse. */
    assert(patchedOk == referenceOk);
    if (!patchedOk) {
        failed++;
        return;
    }
    CheckValid(pDevEvo, &patched, state);
    CheckValid(pDevEvo, &reference, state);

    for (int i = 0; i < 4; i++) {
        if (state[i].changed && ExpectAvoid(pDevEvo, state[i].kind)) {
            if (reference.disp[0].apiHead[i].hwHeadsMask == NVBIT(0)) {
                refAvoiderOnHead0 = i;
            }
            if (patched.disp[0].apiHead[i].hwHeadsMask == NVBIT(0)) {
                avoiderOnHead0 = i;
            }
        }
    }

    /* Stock results stand unless stock left a changed dpy to avoid on head 0. */
    if (refAvoiderOnHead0 < 0) {
        assert(memcmp(&patched, &reference, sizeof(patched)) == 0);
        compared++;
        return;
    }
    avoidedOnHead0++;

    /* An api-head that ends up elsewhere than in stock is programmed from scratch. */
    for (int i = 0; i < 4; i++) {
        const NvU32 mask = patched.disp[0].apiHead[i].hwHeadsMask;

        if (mask != reference.disp[0].apiHead[i].hwHeadsMask) {
            assert(state[i].changed);
            for (NvU32 head = 0; head < 4; head++) {
                if (mask & NVBIT(head)) {
                    assert(patched.disp[0].head[head].owner == 0);
                }
            }
        }
    }

    if (avoiderOnHead0 < 0) {
        if (UsedHeads(&patched) != UsedHeads(&reference)) {
            moved++;
        } else {
            swapped++;
        }
        return;
    }

    /* Still on head 0: no free head and no swap may have been possible. */
    unavoidable++;
    const int a = avoiderOnHead0;
    const NvU32 used = UsedHeads(&patched);
    for (NvU32 head = 1; head < pDevEvo->numHeads; head++) {
        if (pDevEvo->apiHead[a].numLayers > pDevEvo->head[head].numLayers) {
            continue;
        }
        assert(used & NVBIT(head));
        for (int b = 0; b < 4; b++) {
            if (patched.disp[0].apiHead[b].hwHeadsMask & NVBIT(head)) {
                assert(!state[b].changed ||
                       (patched.disp[0].apiHead[b].hwHeadsMask != NVBIT(head)) ||
                       ExpectAvoid(pDevEvo, state[b].kind) ||
                       (state[b].kind == SERIALIZER_DSC) ||
                       (pDevEvo->apiHead[b].numLayers > pDevEvo->head[0].numLayers));
            }
        }
    }
}

static NVDevEvoRec ZSystemDev(void)
{
    NVDevEvoRec dev = { &blackwellHal, 4, 4, { { 2 }, { 2 }, { 2 }, { 2 } },
                        { { 2 }, { 2 }, { 2 }, { 2 } } };
    return dev;
}

static void Expect(const NVProposedModeSetHwState *p, const NvU32 expected[4], const char *name)
{
    for (int i = 0; i < 4; i++) {
        if (p->disp[0].apiHead[i].hwHeadsMask != expected[i]) {
            fprintf(stderr, "%s: api head %d got 0x%x, expected 0x%x\n", name, i,
                    p->disp[0].apiHead[i].hwHeadsMask, expected[i]);
            assert(0);
        }
    }
    printf("%s: ok\n", name);
}

/* One modeset that enables every api-head at once. */
static void ZSystem(const enum Kind kinds[4], const NvU32 expected[4], const char *name)
{
    NVDevEvoRec dev = ZSystemDev();
    NVDispEvoRec disp;
    NVProposedModeSetHwState proposed;
    ApiHeadState state[4];

    for (int i = 0; i < 4; i++) {
        state[i] = (ApiHeadState){ kinds[i], 0, TRUE };
    }
    Setup(&dev, &disp, &proposed, state);
    assert(AssignProposedHwHeadsOneDisp(&disp, &proposed));
    Expect(&proposed, expected, name);
}

/*
 * Boot on 2026-10-31 15:51: fbcon enabled one api-head per modeset as each
 * dpy was detected, the G9 last, then gnome-shell set all four at login.
 */
static void ZSystemFbconBoot(const enum Kind kinds[4], const int order[4],
                             const NvU32 expectedFbcon[4], const NvU32 expectedLogin[4])
{
    NVDevEvoRec dev = ZSystemDev();
    NVDispEvoRec disp;
    NVProposedModeSetHwState proposed;
    ApiHeadState state[4];

    for (int i = 0; i < 4; i++) {
        state[i] = (ApiHeadState){ EMPTY, 0, FALSE };
    }
    Setup(&dev, &disp, &proposed, state);
    for (int step = 0; step < 4; step++) {
        const int apiHead = order[step];

        for (int i = 0; i < 4; i++) {
            proposed.disp[0].apiHead[i].changed = FALSE;
        }
        SetDpy(&disp, apiHead, kinds[apiHead]);
        proposed.disp[0].apiHead[apiHead].dpyIdList = apiHead;
        proposed.disp[0].apiHead[apiHead].changed = TRUE;
        assert(AssignProposedHwHeadsOneDisp(&disp, &proposed));
        Program(&proposed, apiHead);
    }
    Expect(&proposed, expectedFbcon, "fbcon lights the G9 last, on head 0");

    for (int i = 0; i < 4; i++) {
        proposed.disp[0].apiHead[i].changed = TRUE;
    }
    assert(AssignProposedHwHeadsOneDisp(&disp, &proposed));
    Expect(&proposed, expectedLogin, "login modeset moves the G9 off head 0");
    assert(proposed.disp[0].head[0].owner == 0 && proposed.disp[0].head[3].owner == 0);
}

typedef struct {
    NVDevEvoRec dev;
    ApiHeadState state[4];
    NvU32 stride, index, runs;
} Sweep;

/* Every kind and every new, kept or unchanged assignment for each api-head. */
static void Enumerate(Sweep *s, const int i, const NvU32 used)
{
    if (i == 4) {
        if ((s->index++ % s->stride) == 0) {
            RunOne(&s->dev, s->state);
            s->runs++;
        }
        return;
    }

    const NvU32 numHeads = s->dev.numHeads;
    NvU32 masks[6], numMasks = 0;

    for (NvU32 head = 0; head < numHeads; head++) {
        masks[numMasks++] = NVBIT(head);
    }
    masks[numMasks++] = NVBIT(0) | NVBIT(1);
    if (numHeads == 4) {
        masks[numMasks++] = NVBIT(2) | NVBIT(3);
    }

    s->state[i] = (ApiHeadState){ EMPTY, 0, TRUE };
    Enumerate(s, i + 1, used);

    for (int kind = HDMI; kind < KIND_COUNT; kind++) {
        s->state[i] = (ApiHeadState){ kind, 0, TRUE };
        Enumerate(s, i + 1, used);

        for (NvU32 m = 0; m < numMasks; m++) {
            NvBool fits = TRUE;

            if (masks[m] & used) {
                continue;
            }
            for (NvU32 head = 0; head < numHeads; head++) {
                if ((masks[m] & NVBIT(head)) &&
                    (s->dev.apiHead[i].numLayers > s->dev.head[head].numLayers)) {
                    fits = FALSE;
                }
            }
            if (!fits) {
                continue;
            }
            /* A kept SST serializer head trips a stock nvAssert(). */
            if (kind != SERIALIZER_DSC) {
                s->state[i] = (ApiHeadState){ kind, masks[m], TRUE };
                Enumerate(s, i + 1, used | masks[m]);
            }
            s->state[i] = (ApiHeadState){ kind, masks[m], FALSE };
            Enumerate(s, i + 1, used | masks[m]);
        }
    }
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
    const int fbconOrder[4] = { 2, 3, 0, 1 };                     /* HYTE LG MSI G9 */
    const NvU32 fbconHeads[4] = { NVBIT(1), NVBIT(0), NVBIT(3), NVBIT(2) };
    const NvU32 loginHeads[4] = { NVBIT(1), NVBIT(3), NVBIT(0), NVBIT(2) };
    ZSystem(usual, usualHeads, "usual layout keeps stock heads");
    ZSystem(fallback, fallbackHeads, "fallback layout moves the G9 off head 0");
    ZSystemFbconBoot(usual, fbconOrder, fbconHeads, loginHeads);

    const NvU32 layerPatterns[4][4] = {
        { 2, 2, 2, 2 }, { 1, 2, 2, 2 }, { 2, 2, 2, 1 }, { 1, 1, 2, 2 },
    };
    const NvU32 apiLayerPatterns[4][4] = {
        { 1, 1, 1, 1 }, { 2, 2, 2, 2 }, { 2, 1, 1, 2 }, { 1, 2, 2, 1 },
    };
    const NVEvoHalRec *hals[2] = { &blackwellHal, &olderHal };
    NvU32 runs = 0;

    /* Uniform layers, as on GB202, in full; uneven layers sampled by stride. */
    for (int h = 0; h < 2; h++)
    for (NvU32 numHeads = 2; numHeads <= 4; numHeads++)
    for (int lp = 0; lp < 4; lp++)
    for (int ap = 0; ap < 4; ap++) {
        const NvBool uniform = (lp == 0) && (ap == 0);
        Sweep s = { { hals[h], numHeads, 4, { { 0 } }, { { 0 } } }, { { 0 } },
                    uniform ? 1 : 29, (NvU32)(lp * 4 + ap), 0 };

        for (int i = 0; i < 4; i++) {
            s.dev.head[i].numLayers = layerPatterns[lp][i];
            s.dev.apiHead[i].numLayers = apiLayerPatterns[ap][i];
        }
        Enumerate(&s, 0, ~(NVBIT(numHeads) - 1) & 0xf);
        runs += s.runs;
    }

    printf("%u layouts: %u fail in both, %u match stock, %u had a changed DSC dpy on head 0 "
           "in stock (%u moved to a free head, %u swapped, %u unavoidable)\n",
           runs, failed, compared, avoidedOnHead0, moved, swapped, unavoidable);
    assert(moved > 0 && swapped > 0 && unavoidable > 0);
    return 0;
}
