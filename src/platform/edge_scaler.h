#pragma once

#include "core/types.h"

namespace swd3e
{

// The rebuild's own edge-directed scaler of the logical frame ([display] scaler = "edge", deviations.md): the frame's
// pixels stay whole, and a pixel whose corner is crossed by a diagonal edge gets that corner blended into the colour of
// the region beyond the edge, so diagonals come out round at a whole-number scale. The blend asks the edge's regions
// for reach on both sides of it, so one-pixel details (a walking role's limbs, dither pixels) stay whole while strokes
// and long diagonals round. Written from the published idea of edge-directed pixel scaling alone (own rules, weights
// and thresholds; no third-party code).
class EdgeScaler
{
public:
    static constexpr int LeastFactor = 2;  // below this the corners would not sharpen anything
    static constexpr int MostFactor = 4;   // beyond this the renderer's linear pass is enough

    // Scales `source` (`width` x `height` RGB565 pixels, rows contiguous) by `factor` into `target`, which holds
    // `width` * `factor` x `height` * `factor` ARGB8888 pixels with contiguous rows. The same input always gives the
    // same output; rows are worked on by several threads when there are many of them.
    static void Scale(const u16* source, int width, int height, int factor, u32* target);

    // The same over a canvas woven at `parts` (`source` `width` * `parts` x `height` * `parts` pixels, each logical
    // pixel a `parts` x `parts` block of its sub-pixel samples, as the render scale paints it; `parts` of 1 is the
    // frame itself). The corners are still judged on the frame as drawn — each block's north-west sample — while the
    // sub-pixels take their own woven colours (by nearest, when `factor` differs from `parts`), so a woven picture
    // keeps its detail (a depth-scaled role's woven sampling) and its diagonals round as they do unwoven. A block
    // that holds one colour throughout scales exactly as `Scale` scales its pixel.
    static void ScaleWoven(const u16* source, int width, int height, int parts, int factor, u32* target);
};

}  // namespace swd3e
