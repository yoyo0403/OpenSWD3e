#include "platform/edge_scaler.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <thread>
#include <vector>

namespace swd3e
{

namespace
{

// The colour distance of two pixels: the squared difference of the luma (counted four times) and of the three channels,
// in the 8-bit units the channels expand to. Pixel-art edges differ in luma far more than the faint gradients the
// game's translucent effects leave behind, which the threshold below tells apart.
constexpr int kLumaWeight = 4;

// A corner of a pixel is blended into the colour beyond the edge only when that edge is a real one: the two neighbours
// of the corner must differ from the pixel about equally (both their sides crossed), be close to each other (one edge
// crossing the corner, not two), and differ clearly (an edge, not a gradient).
constexpr u32 kCornerThreshold = 900;  // the least mean distance of the corner's neighbours (8-bit squared)
constexpr u32 kCornerDominance = 4;    // one side at most this much farther (squared) than the other
constexpr u32 kCornerCross = 2;        // the neighbours may differ at most this part of their mean distance
constexpr int kCornerGain = 176;       // the strongest blend (of 256): a corner keeps part of its own pixel
constexpr int kWeightOne = 256;

// The blend of a corner reaches from the block's corner inward, fading over the quadrant's width.
constexpr int kFullReach = 1;  // the doubled distance of the sub-pixel at the corner itself

// Rows below this are scaled on the calling thread: the threads would cost more than they save.
constexpr int kLeastThreadedRows = 96;
constexpr unsigned kMostThreads = 8;

struct Color
{
    int r;
    int g;
    int b;
};

// A pixel of the game's RGB565 screen with its channels expanded to 8 bits.
Color Expand(u16 pixel)
{
    const int r = pixel >> 11 & 0x1F;
    const int g = pixel >> 5 & 0x3F;
    const int b = pixel & 0x1F;
    return {r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2};
}

u32 Distance(const Color& a, const Color& b)
{
    const int dr = a.r - b.r;
    const int dg = a.g - b.g;
    const int db = a.b - b.b;
    const int dy = (dr + 2 * dg + db) / 4;
    return static_cast<u32>(kLumaWeight * dy * dy + dr * dr + dg * dg + db * db);
}

u32 Combine(const Color& color)
{
    return 0xFF000000u | static_cast<u32>(color.r) << 16 | static_cast<u32>(color.g) << 8 | static_cast<u32>(color.b);
}

// `color` mixed towards `tint` by `part` of kWeightOne.
Color Mix(const Color& color, const Color& tint, int part)
{
    return {
        (color.r * (kWeightOne - part) + tint.r * part + kWeightOne / 2) / kWeightOne,
        (color.g * (kWeightOne - part) + tint.g * part + kWeightOne / 2) / kWeightOne,
        (color.b * (kWeightOne - part) + tint.b * part + kWeightOne / 2) / kWeightOne,
    };
}

// One of the four corners of a pixel: the two neighbours beside it (`first` above or below, `second` at its side), the
// diagonal beyond it, and where its quadrant of the scaled block lies.
struct Corner
{
    const Color* first;
    const Color* second;
    const Color* diagonal;
    bool eastSide;
    bool southSide;
};

// The corner's blend weight (0..kWeightOne) with the colour of its two neighbours averaged, or 0 for no blend: a
// crossing edge gives a full-weight blend, a faint gradient or a straight edge none.
int CornerWeight(const Color& center, const Corner& corner, Color& tint)
{
    const u32 firstDistance = Distance(center, *corner.first);
    const u32 secondDistance = Distance(center, *corner.second);
    const u32 nearDistance = (firstDistance + secondDistance) / 2;

    if (nearDistance < kCornerThreshold)
    {
        return 0;
    }

    const u32 nearer = std::min(firstDistance, secondDistance);
    const u32 farther = std::max(firstDistance, secondDistance);

    if (farther > nearer * kCornerDominance)
    {
        return 0;
    }

    const u32 cross = Distance(*corner.first, *corner.second);

    if (cross * kCornerCross >= nearDistance)
    {
        return 0;
    }

    const int reach = static_cast<int>((nearDistance - cross / 2) * kWeightOne / nearDistance);
    tint = {
        (corner.first->r + corner.second->r) / 2,
        (corner.first->g + corner.second->g) / 2,
        (corner.first->b + corner.second->b) / 2,
    };

    return reach * kCornerGain / kWeightOne;
}

// How much of the corner's blend the sub-pixel (x, y) of the block takes: full at the block's corner, fading over the
// quadrant's width. The distances are doubled to stay whole.
int SubPixelReach(const Corner& corner, int factor, int x, int y)
{
    const int fromSide = corner.eastSide ? 2 * factor - (2 * x + 1) : 2 * x + 1;
    const int fromRow = corner.southSide ? 2 * factor - (2 * y + 1) : 2 * y + 1;
    const int far = std::max(fromSide, fromRow);

    if (far <= kFullReach)
    {
        return kWeightOne;
    }

    return std::max(0, kWeightOne - (far - kFullReach) * kWeightOne / factor);
}

// The `factor` x `factor` block of the pixel at (x, y): the pixel's own colour everywhere but in corners an edge
// crosses, which take the colour beyond that edge by their reach.
void ScalePixel(const u16* source, int width, int height, int x, int y, int factor, u32* target)
{
    const int left = std::max(x - 1, 0);
    const int right = std::min(x + 1, width - 1);
    const int above = std::max(y - 1, 0);
    const int below = std::min(y + 1, height - 1);

    const u16 centerPixel = source[static_cast<std::size_t>(y) * width + x];
    const u16 northPixel = source[static_cast<std::size_t>(above) * width + x];
    const u16 southPixel = source[static_cast<std::size_t>(below) * width + x];
    const u16 westPixel = source[static_cast<std::size_t>(y) * width + left];
    const u16 eastPixel = source[static_cast<std::size_t>(y) * width + right];

    // The pixels around are all the same colour: the block is plain (most of the frame's area).
    if (northPixel == centerPixel && southPixel == centerPixel && westPixel == centerPixel && eastPixel == centerPixel)
    {
        const u32 plain = Combine(Expand(centerPixel));
        u32* block =
            target + static_cast<std::size_t>(y * factor) * width * factor + static_cast<std::size_t>(x * factor);

        for (int sy = 0; sy < factor; ++sy)
        {
            std::fill(block + sy * width * factor, block + sy * width * factor + factor, plain);
        }

        return;
    }

    const std::array<Color, 9> around = {
        Expand(source[static_cast<std::size_t>(above) * width + left]),
        Expand(northPixel),
        Expand(source[static_cast<std::size_t>(above) * width + right]),
        Expand(westPixel),
        Expand(centerPixel),
        Expand(eastPixel),
        Expand(source[static_cast<std::size_t>(below) * width + left]),
        Expand(southPixel),
        Expand(source[static_cast<std::size_t>(below) * width + right]),
    };

    // The corners around the center: north, north-east, east, south-east, south, south-west, west, north-west.
    const std::array<Corner, 4> corners = {{
        {&around[1], &around[5], &around[2], true, false},   // north-east
        {&around[1], &around[3], &around[0], false, false},  // north-west
        {&around[7], &around[5], &around[8], true, true},    // south-east
        {&around[7], &around[3], &around[6], false, true},   // south-west
    }};

    std::array<Color, 4> tints{};
    std::array<int, 4> weights{};

    for (std::size_t corner = 0; corner < corners.size(); ++corner)
    {
        weights[corner] = CornerWeight(around[4], corners[corner], tints[corner]);
    }

    // A sub-pixel belongs to the quadrant of the corner it lies in.
    const int half = (factor + 1) / 2;
    const Color center = around[4];
    u32* block = target + static_cast<std::size_t>(y * factor) * width * factor + static_cast<std::size_t>(x * factor);

    for (int sy = 0; sy < factor; ++sy)
    {
        const bool south = sy >= half;

        for (int sx = 0; sx < factor; ++sx)
        {
            const bool east = sx >= half;
            const std::size_t corner = east ? (south ? 2 : 0) : (south ? 3 : 1);
            Color subPixel = center;

            if (weights[corner] > 0)
            {
                const int reach = SubPixelReach(corners[corner], factor, sx, sy);
                subPixel = Mix(center, tints[corner], weights[corner] * reach / kWeightOne);
            }

            block[sy * width * factor + sx] = Combine(subPixel);
        }
    }
}

// The rows from `from` to `to` (not included).
void ScaleRows(const u16* source, int width, int height, int from, int to, int factor, u32* target)
{
    for (int y = from; y < to; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            ScalePixel(source, width, height, x, y, factor, target);
        }
    }
}

}  // namespace

void EdgeScaler::Scale(const u16* source, int width, int height, int factor, u32* target)
{
    if (source == nullptr || target == nullptr || width <= 0 || height <= 0)
    {
        return;
    }

    const int bounded = std::clamp(factor, EdgeScaler::LeastFactor, EdgeScaler::MostFactor);
    const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
    const int bands = static_cast<int>(std::min(cores, kMostThreads));

    if (height < kLeastThreadedRows || bands <= 1)
    {
        ScaleRows(source, width, height, 0, height, bounded, target);
        return;
    }

    std::vector<std::thread> workers;

    for (int band = 1; band < bands; ++band)
    {
        const int from = height * band / bands;
        const int to = height * (band + 1) / bands;
        workers.emplace_back(
            [source, width, height, from, to, bounded, target]
            {
                ScaleRows(source, width, height, from, to, bounded, target);
            }
        );
    }

    ScaleRows(source, width, height, 0, height / bands, bounded, target);

    for (std::thread& worker : workers)
    {
        worker.join();
    }
}

}  // namespace swd3e
