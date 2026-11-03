#pragma once

#include "core/types.h"

namespace swd3e
{

// The rebuild's own edge-directed scaler of the logical frame ([display] scaler = "edge", deviations.md): the frame's
// pixels stay whole, and a pixel whose corner is crossed by a diagonal edge gets that corner blended into the colour of
// the region beyond the edge, so diagonals come out round at a whole-number scale. Written from the published idea of
// edge-directed pixel scaling alone (own rules, weights and thresholds; no third-party code).
class EdgeScaler
{
public:
    static constexpr int LeastFactor = 2;  // below this the corners would not sharpen anything
    static constexpr int MostFactor = 4;   // beyond this the renderer's linear pass is enough

    // Scales `source` (`width` x `height` RGB565 pixels, rows contiguous) by `factor` into `target`, which holds
    // `width` * `factor` x `height` * `factor` ARGB8888 pixels with contiguous rows. The same input always gives the
    // same output; rows are worked on by several threads when there are many of them.
    static void Scale(const u16* source, int width, int height, int factor, u32* target);
};

}  // namespace swd3e
