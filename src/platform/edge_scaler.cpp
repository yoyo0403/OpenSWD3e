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

// A side of the edge reaches on for another pixel when that pixel differs by less than this part of the edge's step:
// the border of a region reaches, the section of a one-pixel fringe or dither does not.
constexpr u32 kDepthCross = 2;

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

// Where a logical pixel's colour comes from and what its block's sub-pixels show: the frame as drawn (`parts` of 1,
// every sub-pixel the pixel's own colour) or a canvas woven at `parts` (each logical pixel a `parts` x `parts` block
// of its sub-pixel samples, as the render scale paints it — the block's north-west sample the pixel's own colour,
// each sub-pixel its own woven sample, taken by nearest when `factor` differs from `parts`).
class BlockSource
{
public:
    BlockSource(const u16* pixels, int width, int parts, int factor)
        : pixels(pixels),
          stride(static_cast<std::size_t>(width) * static_cast<std::size_t>(parts)),
          parts(parts),
          factor(factor)
    {
    }

    // The pixel's colour the corners are judged on: the woven canvas's north-west sample of its block, the frame as
    // the whole-pixel painting drew it.
    u16 Whole(int x, int y) const
    {
        return this->pixels[BlockAt(x, y)];
    }

    // Whether every sub-pixel of the pixel's block holds its own colour (a woven block may differ within itself).
    bool Uniform(int x, int y) const
    {
        const u16 whole = this->Whole(x, y);
        const u16* block = this->pixels + BlockAt(x, y);

        for (int row = 0; row < this->parts; ++row)
        {
            for (int column = 0; column < this->parts; ++column)
            {
                if (block[static_cast<std::size_t>(row) * this->stride + column] != whole)
                {
                    return false;
                }
            }
        }

        return true;
    }

    // The colour the output sub-pixel (sx, sy) of the block shows.
    Color SubPixel(int x, int y, int sx, int sy) const
    {
        const int column = std::min(sx * this->parts / this->factor, this->parts - 1);
        const int row = std::min(sy * this->parts / this->factor, this->parts - 1);
        return Expand(this->pixels[BlockAt(x, y) + static_cast<std::size_t>(row) * this->stride + column]);
    }

private:
    std::size_t BlockAt(int x, int y) const
    {
        return (static_cast<std::size_t>(y) * this->parts) * this->stride + static_cast<std::size_t>(x) * this->parts;
    }

    const u16* pixels;
    std::size_t stride;  // the width of the frame or the woven canvas, in pixels
    int parts;
    int factor;
};

// One of the four corners of a pixel: the two neighbours beside it (`first` above or below, `second` at its side), the
// pixel diagonally beyond the corner, the next pixels outward of the two neighbours, this pixel's own two neighbours
// away from the corner, its two diagonal neighbours along the edge, and where its quadrant of the scaled block lies.
struct Corner
{
    const Color* first;
    const Color* second;
    const Color* diagonal;
    const Color* outwardFirst;
    const Color* outwardSecond;
    const Color* awayFirst;
    const Color* awaySecond;
    const Color* alongFirst;
    const Color* alongSecond;
    bool eastSide;
    bool southSide;
};

// How many of the two distances (0, 1 or 2) are small enough that what they measure reaches on for another pixel.
int DepthHalves(u32 nearDistance, u32 first, u32 second)
{
    const int firstDeep = first * kDepthCross < nearDistance ? 1 : 0;
    const int secondDeep = second * kDepthCross < nearDistance ? 1 : 0;
    return firstDeep + secondDeep;
}

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

    // This pixel's colour reaching diagonally past the corner means two regions only touch there: the blend would cut
    // the touching point, so the corner stays whole.
    if (Distance(center, *corner.diagonal) * kDepthCross < nearDistance)
    {
        return 0;
    }

    // Both sides of the edge must reach beyond the one pixel about the corner. The colour beyond keeps its next pixels
    // outward (a region's border, not the section of a thin fringe); a far side only half reaching thins the blend to
    // half. This pixel's own colour reaches away from the corner or continues along the edge (a stroke passes, a lone
    // dot or a one-pixel limb's end does not), or the blend would eat the detail.
    const int farHalves = DepthHalves(
        nearDistance,
        Distance(*corner.first, *corner.outwardFirst),
        Distance(*corner.second, *corner.outwardSecond)
    );
    const int ownHalves = std::max(
        DepthHalves(nearDistance, Distance(center, *corner.awayFirst), Distance(center, *corner.awaySecond)),
        DepthHalves(nearDistance, Distance(center, *corner.alongFirst), Distance(center, *corner.alongSecond))
    );

    if (farHalves == 0 || ownHalves < 2)
    {
        return 0;
    }

    const int reach = static_cast<int>((nearDistance - cross / 2) * kWeightOne / nearDistance);
    tint = {
        (corner.first->r + corner.second->r) / 2,
        (corner.first->g + corner.second->g) / 2,
        (corner.first->b + corner.second->b) / 2,
    };

    return reach * kCornerGain * farHalves / (2 * kWeightOne);
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
// crosses, which take the colour beyond that edge by their reach. A woven block's sub-pixels take their own woven
// colours (BlockSource::SubPixel), a corner blending from the sub-pixel it sits on.
void ScalePixel(const BlockSource& source, int width, int height, int x, int y, int factor, u32* target)
{
    const int left = std::max(x - 1, 0);
    const int right = std::min(x + 1, width - 1);
    const int above = std::max(y - 1, 0);
    const int below = std::min(y + 1, height - 1);

    const u16 centerPixel = source.Whole(x, y);
    const u16 northPixel = source.Whole(x, above);
    const u16 southPixel = source.Whole(x, below);
    const u16 westPixel = source.Whole(left, y);
    const u16 eastPixel = source.Whole(right, y);

    // The pixels around are all the same colour and the block holds it throughout: the block is plain (most of the
    // frame's area).
    if (northPixel == centerPixel && southPixel == centerPixel && westPixel == centerPixel &&
        eastPixel == centerPixel && source.Uniform(x, y))
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

    const int left2 = std::max(x - 2, 0);
    const int right2 = std::min(x + 2, width - 1);
    const int above2 = std::max(y - 2, 0);
    const int below2 = std::min(y + 2, height - 1);

    const std::array<Color, 13> around = {
        Expand(source.Whole(left, above)),
        Expand(northPixel),
        Expand(source.Whole(right, above)),
        Expand(westPixel),
        Expand(centerPixel),
        Expand(eastPixel),
        Expand(source.Whole(left, below)),
        Expand(southPixel),
        Expand(source.Whole(right, below)),
        Expand(source.Whole(x, above2)),
        Expand(source.Whole(right2, y)),
        Expand(source.Whole(x, below2)),
        Expand(source.Whole(left2, y)),
    };

    // The 3x3 about the pixel: row-major from its north-west to its south-east, then the next pixels outward of its
    // north, east, south and west. The corners around the center: north-east, north-west, south-east, south-west.
    const std::array<Corner, 4> corners = {{
        {&around[1],
         &around[5],
         &around[2],
         &around[9],
         &around[10],
         &around[3],
         &around[7],
         &around[0],
         &around[8],
         true,
         false},  // north-east
        {&around[1],
         &around[3],
         &around[0],
         &around[9],
         &around[12],
         &around[5],
         &around[7],
         &around[2],
         &around[6],
         false,
         false},  // north-west
        {&around[7],
         &around[5],
         &around[8],
         &around[11],
         &around[10],
         &around[3],
         &around[1],
         &around[6],
         &around[2],
         true,
         true},  // south-east
        {&around[7],
         &around[3],
         &around[6],
         &around[11],
         &around[12],
         &around[5],
         &around[1],
         &around[8],
         &around[0],
         false,
         true},  // south-west
    }};

    std::array<Color, 4> tints{};
    std::array<int, 4> weights{};

    for (std::size_t corner = 0; corner < corners.size(); ++corner)
    {
        weights[corner] = CornerWeight(around[4], corners[corner], tints[corner]);
    }

    // A sub-pixel belongs to the quadrant of the corner it lies in.
    const int half = (factor + 1) / 2;
    u32* block = target + static_cast<std::size_t>(y * factor) * width * factor + static_cast<std::size_t>(x * factor);

    for (int sy = 0; sy < factor; ++sy)
    {
        const bool south = sy >= half;

        for (int sx = 0; sx < factor; ++sx)
        {
            const bool east = sx >= half;
            const std::size_t corner = east ? (south ? 2 : 0) : (south ? 3 : 1);
            Color subPixel = source.SubPixel(x, y, sx, sy);

            if (weights[corner] > 0)
            {
                const int reach = SubPixelReach(corners[corner], factor, sx, sy);
                subPixel = Mix(subPixel, tints[corner], weights[corner] * reach / kWeightOne);
            }

            block[sy * width * factor + sx] = Combine(subPixel);
        }
    }
}

// The rows from `from` to `to` (not included).
void ScaleRows(const BlockSource& source, int width, int height, int from, int to, int factor, u32* target)
{
    for (int y = from; y < to; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            ScalePixel(source, width, height, x, y, factor, target);
        }
    }
}

// The whole frame or woven canvas scaled, its rows worked on by several threads when there are many of them.
void ScaleCanvas(const BlockSource& source, int width, int height, int factor, u32* target)
{
    const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
    const int bands = static_cast<int>(std::min(cores, kMostThreads));

    if (height < kLeastThreadedRows || bands <= 1)
    {
        ScaleRows(source, width, height, 0, height, factor, target);
        return;
    }

    std::vector<std::thread> workers;

    for (int band = 1; band < bands; ++band)
    {
        const int from = height * band / bands;
        const int to = height * (band + 1) / bands;
        workers.emplace_back(
            [source, width, height, from, to, factor, target]
            {
                ScaleRows(source, width, height, from, to, factor, target);
            }
        );
    }

    ScaleRows(source, width, height, 0, height / bands, factor, target);

    for (std::thread& worker : workers)
    {
        worker.join();
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
    ScaleCanvas(BlockSource(source, width, 1, bounded), width, height, bounded, target);
}

void EdgeScaler::ScaleWoven(const u16* source, int width, int height, int parts, int factor, u32* target)
{
    if (source == nullptr || target == nullptr || width <= 0 || height <= 0 || parts < 1)
    {
        return;
    }

    const int bounded = std::clamp(factor, EdgeScaler::LeastFactor, EdgeScaler::MostFactor);
    ScaleCanvas(BlockSource(source, width, parts, bounded), width, height, bounded, target);
}

}  // namespace swd3e
