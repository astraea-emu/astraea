#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "game.h"

// Host-only adapter for the independent author's pure C game logic.
// Never treat this as Sony HLE, PS5 VideoOut, or guest code execution.
void platform_set_light_bar(uint32_t color) { (void)color; }

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    char *end = NULL;
    const unsigned long seed = strtoul(argv[1], &end, 0);
    if (!end || *end != '\0' || seed > UINT32_MAX) return 3;

    Input input = {0};
    game_init((uint32_t)seed);
    game_update(1.0f / 60.0f, &input);
    game_render(60);

    uint64_t hash = UINT64_C(14695981039346656037);
    uint32_t nonzero_pixels = 0;
    for (int y = 0; y < ViewHeight; ++y) {
        for (int x = 0; x < ViewWidth; ++x) {
            const uint32_t pixel = canvas[y][x];
            nonzero_pixels += (pixel != 0U);
            // Explicit little-endian pixel serialization independent of
            // in-memory uint32_t representation.
            for (int byte = 0; byte < 4; ++byte) {
                hash ^= (uint8_t)(pixel >> (byte * 8));
                hash *= UINT64_C(1099511628211);
            }
        }
    }

    printf("canvas_width=%d\n", ViewWidth);
    printf("canvas_height=%d\n", ViewHeight);
    printf("nonzero_pixels=%" PRIu32 "\n", nonzero_pixels);
    printf("canvas_fnv1a64=%016" PRIx64 "\n", hash);
    return nonzero_pixels == 0U ? 4 : 0;
}
