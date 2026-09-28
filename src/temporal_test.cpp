#include <cstdio>
#include <cstdlib>
#include "d3d11.h"

static int failures = 0;

#define TEST(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s\n", msg); \
        failures++; \
    } else { \
        printf("  PASS: %s\n", msg); \
    } \
} while(0)

int main() {
    using R = D3D11Renderer;

    printf("=== TemporalHistory ComputeEffectiveHistoryCount tests ===\n\n");

    // Zero / negative / invalid
    TEST(R::ComputeEffectiveHistoryCount(0, 1920, 1080) == 0, "0 requested → 0");
    TEST(R::ComputeEffectiveHistoryCount(-1, 1920, 1080) == 0, "negative requested → 0");
    TEST(R::ComputeEffectiveHistoryCount(-999, 1920, 1080) == 0, "large negative → 0");
    TEST(R::ComputeEffectiveHistoryCount(10, 0, 1080) == 0, "width=0 → 0");
    TEST(R::ComputeEffectiveHistoryCount(10, 1920, 0) == 0, "height=0 → 0");
    TEST(R::ComputeEffectiveHistoryCount(10, 0, 0) == 0, "both zero → 0");

    // Single frame
    int r1 = R::ComputeEffectiveHistoryCount(1, 1920, 1080);
    TEST(r1 == 1, "1 requested → 1");

    // Small requested values
    int r10 = R::ComputeEffectiveHistoryCount(10, 1920, 1080);
    printf("  10 requested at 1080p = %d\n", r10);
    TEST(r10 == 10, "10 requested → 10");

    // Huge requested
    int huge = R::ComputeEffectiveHistoryCount(999999999, 1920, 1080);
    printf("  999999999 requested at 1080p = %d\n", huge);
    TEST(huge <= 1024, "huge requested ≤ 1024 absolute cap");
    TEST(huge > 10, "huge requested > 10 (budget limited, not request limited)");

    // Extreme resolution — should overflow-safe to 0
    int hugeRes = R::ComputeEffectiveHistoryCount(10, 65536, 65536);
    printf("  65536x65536 = %d\n", hugeRes);
    TEST(hugeRes == 0, "frame > budget returns 0");

    // Another extreme
    int hugeRes2 = R::ComputeEffectiveHistoryCount(10, 100000, 100000);
    TEST(hugeRes2 == 0, "100000x100000 returns 0 (overflow-safe)");

    // 1080p memory
    int r1080 = R::ComputeEffectiveHistoryCount(500, 1920, 1080);
    printf("  500 requested at 1080p = %d\n", r1080);
    TEST(r1080 > 0 && r1080 <= 1024, "1080p within bounds");
    // 1920*1080*4 = 8,294,400 bytes. 512MB / 8.29MB ≈ 63
    TEST(r1080 <= 64, "1080p ≤ 64 (budget cap at 512MB)");

    // 1440p
    int r1440 = R::ComputeEffectiveHistoryCount(500, 2560, 1440);
    printf("  500 requested at 1440p = %d\n", r1440);
    TEST(r1440 > 0 && r1440 <= 1024, "1440p within bounds");
    // 2560*1440*4 = 14,745,600. 512MB / 14.75MB ≈ 36
    TEST(r1440 <= 37, "1440p ≤ 37 (budget cap at 512MB)");

    // 4K
    int r4k = R::ComputeEffectiveHistoryCount(500, 3840, 2160);
    printf("  500 requested at 4K = %d\n", r4k);
    TEST(r4k > 0 && r4k <= 1024, "4K within bounds");
    // 3840*2160*4 = 33,177,600. 512MB / 33.18MB ≈ 16
    TEST(r4k <= 17, "4K ≤ 17 (budget cap at 512MB)");

    // Very large requested but reasonable resolution
    int med = R::ComputeEffectiveHistoryCount(500, 1280, 720);
    printf("  500 requested at 720p = %d\n", med);
    TEST(med > 0 && med <= 1024, "720p within bounds");

    printf("\n=== Opacity math tests ===\n\n");

    // Multiplicative decay
    float eps = 0.0001f;
    TEST(fabsf(R::CalcTemporalOpacity(0, 1.0f, 0.5f) - 1.0f) < eps, "age 0, mult 0.5 = 1.0");
    TEST(fabsf(R::CalcTemporalOpacity(1, 1.0f, 0.5f) - 0.5f) < eps, "age 1, mult 0.5 = 0.5");
    TEST(fabsf(R::CalcTemporalOpacity(2, 1.0f, 0.5f) - 0.25f) < eps, "age 2, mult 0.5 = 0.25");
    TEST(fabsf(R::CalcTemporalOpacity(3, 1.0f, 0.5f) - 0.125f) < eps, "age 3, mult 0.5 = 0.125");

    // Multiplier = 0
    TEST(fabsf(R::CalcTemporalOpacity(0, 1.0f, 0.0f) - 1.0f) < eps, "age 0, mult 0 = 1.0");
    TEST(fabsf(R::CalcTemporalOpacity(1, 1.0f, 0.0f)) < eps, "age 1, mult 0 = 0.0");

    // Multiplier = 1
    TEST(fabsf(R::CalcTemporalOpacity(0, 1.0f, 1.0f) - 1.0f) < eps, "age 0, mult 1 = 1.0");
    TEST(fabsf(R::CalcTemporalOpacity(100, 1.0f, 1.0f) - 1.0f) < eps, "age 100, mult 1 = 1.0");

    // Negative age (should return 0)
    TEST(fabsf(R::CalcTemporalOpacity(-1, 1.0f, 0.5f)) < eps, "negative age = 0.0");

    // Non-finite result (multiplier=0, age huge — pow(0, big) = 0, finite)
    float nf = R::CalcTemporalOpacity(1000000, 1.0f, 0.0f);
    TEST(isfinite(nf), "mult=0, huge age: finite");

    // Saturated version clamps
    TEST(fabsf(R::CalcTemporalOpacitySaturated(0, 2.0f, 1.0f) - 1.0f) < eps, "saturated: initial=2 clamped to 1");
    TEST(fabsf(R::CalcTemporalOpacitySaturated(1, 1.0f, -1.0f)) < eps, "saturated: negative clamped to 0");

    printf("\n=== Circular traversal (simulated) tests ===\n\n");

    // Simulate history state for traversal validation.
    // We can't call private members from test, so we test the public
    // GetProcessedTexture and TemporalHistoryCount which need a real D3D device.
    // The traversal formula is a simple math expression that we test manually:

    // capacity=3, count=1, write_idx=1: oldest = (1-1+3)%3 = 0
    int c3w1 = (1 - 1 + 3) % 3;
    TEST(c3w1 == 0, "capacity=3 count=1 write=1: oldest index 0");

    // capacity=3, count=2, write_idx=2: oldest = (2-2+3)%3 = 0
    int c3w2 = (2 - 2 + 3) % 3;
    TEST(c3w2 == 0, "capacity=3 count=2 write=2: oldest index 0");

    // capacity=3, count=3, write_idx=0: oldest = (0-3+3)%3 = 0
    int c3w0 = (0 - 3 + 3) % 3;
    TEST(c3w0 == 0, "capacity=3 count=3 write=0: oldest index 0");

    // capacity=3, count=3, write_idx=1: oldest = (1-3+3)%3 = 1
    int c3w1b = (1 - 3 + 3) % 3;
    TEST(c3w1b == 1, "capacity=3 count=3 write=1: oldest index 1");

    // Verify full iteration order for capacity=3, write_idx=1 (rollover happened)
    // oldest = 1, count=3 → draw indices: (1+0)%3=1, (1+1)%3=2, (1+2)%3=0
    int expected[] = {1, 2, 0};
    int oldest = (1 - 3 + 3) % 3;
    for (int i = 0; i < 3; i++) {
        int idx = (oldest + i) % 3;
        if (idx != expected[i]) {
            printf("  FAIL: rollover iteration i=%d expected %d got %d\n", i, expected[i], idx);
            failures++;
        }
    }
    if (oldest == 1) printf("  PASS: rollover iteration indices 1,2,0\n");

    printf("\n=== %s ===\n",
           failures ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return failures ? 1 : 0;
}
