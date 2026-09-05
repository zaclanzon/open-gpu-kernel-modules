"""Stress production allocation with an independent capacity/uniqueness oracle.

The model covers four heads, eight tiles, two layers per head, mixed scaler
capabilities, fresh/reused assignments, and 8/32 physical-window limits.
It tests software allocation only, not RM acceptance or hardware execution.
"""
from pathlib import Path
import ast
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
module = ast.parse((root / 'tests/phywin-allocation.py').read_text())
preamble = next(
    ast.literal_eval(node.value)
    for node in module.body
    if isinstance(node, ast.Assign)
    and any(isinstance(target, ast.Name) and target.id == 'preamble'
            for target in node.targets)
)
# Exercise the API's eight-layer array limit as well as two-layer desktops.
preamble = preamble.replace('[2]', '[8]')
source = (root / 'src/nvidia-modeset/src/nvkms-evo4.c').read_text()
start = source.index('static NvBool RequiredScalerTiles(')
end = source.index('static void\nEvoIsModePossibleCA(', start)
scenarios = r'''
static NvBool TestAllocation(
    NvU32 windowCount,
    NvBool reuse,
    NvU32 scalerTiles,
    NvU32 scalingMask,
    NvU32 overlayMask,
    NvU32 tileChoices)
{
    NVDevEvoRec dev = { 0 };
    NVDispEvoRec disp = { .pDevEvo = &dev };
    NVEvoIsModePossibleDispInput input = { 0 };
    NVEvoIsModePossibleDispOutput output = { 0 };
    NVHwModeTimingsEvo timings[4] = { 0 };
    struct NvKmsUsageBounds usage[4] = { 0 };
    NvU32 required[4];
    NvU32 totalTiles = 0;
    NvU32 totalWindows = 0;
    NvU32 requiredScalerTiles = 0;
    NvU32 assignedTiles = 0;
    NvU32 assignedWindows = 0;

    dev.numHeads = 4;
    dev.numHwTiles = 8;
    dev.numHwPhywins = windowCount;
    for (NvU32 tile = 0; tile < 8; tile++) {
        dev.gpus[0].capabilities.hwTile[tile].type =
            tile < scalerTiles ? NV_EVO_HW_TILE_TYPE_0 :
                                 NV_EVO_HW_TILE_TYPE_1;
    }
    for (NvU32 head = 0; head < 4; head++) {
        dev.head[head].numLayers = 2;
        input.head[head].pTimings = &timings[head];
        input.head[head].pUsage = &usage[head];
        input.head[head].modesetRequested = TRUE;
        usage[head].layer[0].usable = TRUE;
        usage[head].layer[1].usable = !!(overlayMask & NVBIT(head));
        timings[head].viewPort.in.width = 1920;
        timings[head].viewPort.out.width =
            (scalingMask & NVBIT(head)) ? 1280 : 1920;
        required[head] = 1 + ((tileChoices >> (2 * head)) & 3);
        if (reuse) {
            input.head[head].multiTileConfig.tilesMask = NVBIT(head);
            input.head[head].multiTileConfig.phywinsMask[0] =
                NVBIT(windowCount - 8 + 2 * head);
            input.head[head].multiTileConfig.phywinsMask[1] =
                NVBIT(windowCount - 8 + 2 * head + 1);
        }
        if (scalingMask & NVBIT(head)) {
            requiredScalerTiles += required[head];
        }
        totalTiles += required[head];
        totalWindows += required[head] *
            (1 + usage[head].layer[1].usable);
    }

    const NVEvoIsModePossibleDispInput originalInput = input;
    NvBool result = EvoAssignHwHeadMultiTileConfigDispOutputCA(
        &disp, &input, required, &output);
    assert(memcmp(&input, &originalInput, sizeof(input)) == 0);
    NvBool expected = totalTiles <= 8 && totalWindows <= windowCount &&
                      requiredScalerTiles <= scalerTiles;
    if (result != expected) {
        printf("FAIL: windows=%u reuse=%u scalerTiles=%u scaling=%u "
               "overlays=%u tiles=%u "
               "needTiles=%u needWindows=%u accepted=%d\n",
               windowCount, reuse, scalerTiles, scalingMask,
               overlayMask, tileChoices,
               totalTiles, totalWindows, result);
        return FALSE;
    }
    if (result) {
        for (NvU32 head = 0; head < 4; head++) {
            const NVHwHeadMultiTileConfigRec *pConfig =
                &output.head[head].multiTileConfig;
            assert(nvPopCount32(pConfig->tilesMask) == required[head]);
            assert(!(pConfig->tilesMask & ~0xffU));
            assert(!(pConfig->tilesMask & assignedTiles));
            assignedTiles |= pConfig->tilesMask;
            for (NvU32 layer = 0; layer < 2; layer++) {
                NvU32 expectedCount = usage[head].layer[layer].usable ?
                    required[head] : 0;
                assert(nvPopCount32(pConfig->phywinsMask[layer]) ==
                       expectedCount);
                assert(!(pConfig->phywinsMask[layer] &
                         ~(NvU32)(NVBIT64(windowCount) - 1)));
                assert(!(pConfig->phywinsMask[layer] & assignedWindows));
                assignedWindows |= pConfig->phywinsMask[layer];
            }
        }
    }
    return TRUE;
}

static void TestBoundaryConditions(void)
{
    NVDevEvoRec dev = { .numHeads = 4, .numHwTiles = 8, .numHwPhywins = 32 };
    NVDispEvoRec disp = { .pDevEvo = &dev };
    NVEvoIsModePossibleDispInput input = { 0 };
    NVEvoIsModePossibleDispOutput output = { 0 };
    NVHwModeTimingsEvo timing = { 0 };
    struct NvKmsUsageBounds usage = { 0 };
    NvU32 required[4] = { 0 };

    /* Inactive heads have NULL timing/usage pointers and need no resources. */
    assert(EvoAssignHwHeadMultiTileConfigDispOutputCA(
        &disp, &input, required, &output));

    dev.head[0].numLayers = 8;
    input.head[0].pTimings = &timing;
    input.head[0].pUsage = &usage;
    input.head[0].modesetRequested = TRUE;
    input.head[0].multiTileConfig.tilesMask = 1;
    input.head[0].multiTileConfig.phywinsMask[0] = NVBIT(31);
    required[0] = 1;
    for (NvU32 layer = 0; layer < 8; layer++) {
        usage.layer[layer].usable = TRUE;
    }
    assert(EvoAssignHwHeadMultiTileConfigDispOutputCA(
        &disp, &input, required, &output));
    assert(output.head[0].multiTileConfig.phywinsMask[0] == NVBIT(31));
    for (NvU32 layer = 0; layer < 8; layer++) {
        assert(nvPopCount32(output.head[0].multiTileConfig.phywinsMask[layer]) == 1);
    }

    /* Failed helper allocation must not publish its partial result. */
    NVHwHeadMultiTileConfigRec config = { .tilesMask = 3 };
    const NVHwHeadMultiTileConfigRec originalConfig = config;
    NvU32 freeWindows = 1;
    assert(!AssignNewPhywinsIfNeeded(&disp, &config, 0, &usage, &freeWindows));
    assert(memcmp(&config, &originalConfig, sizeof(config)) == 0);
    assert(freeWindows == 1);
    puts("PASS: inactive heads, eight layers, bit 31, and allocation failure isolation");
}

int main(void)
{
    unsigned cases = 0;

    TestBoundaryConditions();

    for (NvU32 windowCount = 8; windowCount <= 32; windowCount += 24) {
        for (NvU32 reuse = 0; reuse < 2; reuse++) {
            for (NvU32 scalerTiles = 0; scalerTiles <= 8; scalerTiles++) {
                for (NvU32 scalingMask = 0; scalingMask < 16; scalingMask++) {
                    for (NvU32 overlayMask = 0; overlayMask < 16; overlayMask++) {
                        for (NvU32 tileChoices = 0; tileChoices < 256;
                                tileChoices++) {
                            assert(TestAllocation(windowCount, reuse,
                                                  scalerTiles, scalingMask,
                                                  overlayMask, tileChoices));
                            cases++;
                        }
                    }
                }
            }
        }
    }
    printf("PASS: %u allocation combinations\n", cases);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    test = Path(directory) / 'test.c'
    binary = Path(directory) / 'test'
    test.write_text(preamble + source[start:end] + scenarios)
    subprocess.run([
        'cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=address,undefined', str(test), '-o', str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
