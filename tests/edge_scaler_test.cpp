#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include "platform/edge_scaler.h"
#include "test.h"

using swd3e::EdgeScaler;
using swd3e::u16;
using swd3e::u32;

namespace
{

constexpr u16 kBlack = 0x0000;
constexpr u16 kWhite = 0xFFFF;
constexpr u32 kBlackOut = 0xFF000000u;
constexpr u32 kWhiteOut = 0xFFFFFFFFu;

// The white pixel's corner crossed by a black edge blends to this: a corner keeps part of its own pixel, and the blend
// fades over the quadrant's width (the values the scaler's rules give).
constexpr u32 kCornerNear = 0xFF505050u;
constexpr u32 kCornerFar = 0xFFA7A7A7u;

// A colour whose difference from black is faint enough to be a gradient, not an edge.
constexpr u16 kFaint = 0x0020;

std::vector<u32> Scaled(const std::vector<u16>& image, int width, int height, int factor)
{
    std::vector<u32> scaled(
        static_cast<std::size_t>(width) * factor * (static_cast<std::size_t>(height) * factor),
        0xFEEEEEEEu
    );
    EdgeScaler::Scale(image.data(), width, height, factor, scaled.data());
    return scaled;
}

// The image woven at `parts`: each pixel a `parts` x `parts` block holding it (the ground at a render scale).
std::vector<u16> Woven(const std::vector<u16>& image, int width, int height, int parts)
{
    std::vector<u16> woven(
        static_cast<std::size_t>(width) * parts * (static_cast<std::size_t>(height) * parts),
        0xF00Fu
    );

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            for (int row = 0; row < parts; ++row)
            {
                for (int column = 0; column < parts; ++column)
                {
                    woven[(static_cast<std::size_t>(y) * parts + row) * width * parts + x * parts + column] =
                        image[static_cast<std::size_t>(y) * width + x];
                }
            }
        }
    }

    return woven;
}

std::vector<u32> ScaledWoven(const std::vector<u16>& woven, int width, int height, int parts, int factor)
{
    std::vector<u32> scaled(
        static_cast<std::size_t>(width) * factor * (static_cast<std::size_t>(height) * factor),
        0xFEEEEEEEu
    );
    EdgeScaler::ScaleWoven(woven.data(), width, height, parts, factor, scaled.data());
    return scaled;
}

bool AllOf(const std::vector<u32>& scaled, u32 color)
{
    return std::all_of(
        scaled.begin(),
        scaled.end(),
        [color](u32 pixel)
        {
            return pixel == color;
        }
    );
}

}  // namespace

TEST_CASE("edge scaler: a plain picture scales to plain blocks")
{
    const std::vector<u16> white(6 * 4, kWhite);
    CHECK(AllOf(Scaled(white, 6, 4, 2), kWhiteOut));
    CHECK(AllOf(Scaled(white, 6, 4, 4), kWhiteOut));

    const std::vector<u16> black(6 * 4, kBlack);
    CHECK(AllOf(Scaled(black, 6, 4, 3), kBlackOut));
}

TEST_CASE("edge scaler: a straight edge stays whole pixels")
{
    constexpr int kWidth = 8;
    constexpr int kHeight = 6;
    std::vector<u16> image(static_cast<std::size_t>(kWidth) * kHeight, kBlack);

    for (int y = 0; y < kHeight; ++y)
    {
        for (int x = kWidth / 2; x < kWidth; ++x)
        {
            image[static_cast<std::size_t>(y) * kWidth + x] = kWhite;
        }
    }

    for (int factor : {2, 3, 4})
    {
        const std::vector<u32> scaled = Scaled(image, kWidth, kHeight, factor);
        CHECK_EQ(scaled.size(), static_cast<std::size_t>(kWidth * factor) * kHeight * factor);

        for (int y = 0; y < kHeight * factor; ++y)
        {
            for (int x = 0; x < kWidth * factor; ++x)
            {
                const u32 expected = x < (kWidth / 2) * factor ? kBlackOut : kWhiteOut;
                CHECK(scaled[static_cast<std::size_t>(y) * kWidth * factor + x] == expected);
            }
        }
    }
}

TEST_CASE("edge scaler: a corner crossed by an edge blends at that corner")
{
    // A single white pixel at the south-east of black: its north-west corner is crossed by the edge.
    const std::vector<u16> image = {kBlack, kBlack, kBlack, kWhite};
    const std::vector<u32> scaled = Scaled(image, 2, 2, 2);
    CHECK_EQ(scaled.size(), static_cast<std::size_t>(16));

    // Every sub-pixel stays its own pixel's colour but that corner, which takes a mix of both.
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            const u32 pixel = scaled[static_cast<std::size_t>(y) * 4 + x];
            const bool corner = x == 2 && y == 2;

            if (corner)
            {
                CHECK(pixel == kCornerNear);
            }
            else if (x >= 2 && y >= 2)
            {
                CHECK(pixel == kWhiteOut);
            }
            else
            {
                CHECK(pixel == kBlackOut);
            }
        }
    }
}

TEST_CASE("edge scaler: the blend fades over the quadrant")
{
    const std::vector<u16> image = {kBlack, kBlack, kBlack, kWhite};
    const std::vector<u32> scaled = Scaled(image, 2, 2, 4);
    const std::array expected = {kCornerNear, kCornerFar, kCornerFar, kCornerFar};

    for (int y = 0; y < 2; ++y)
    {
        for (int x = 0; x < 2; ++x)
        {
            const u32 pixel = scaled[static_cast<std::size_t>(4 + y) * 8 + 4 + x];
            CHECK(pixel == expected[static_cast<std::size_t>(y * 2 + x)]);
        }
    }
}

TEST_CASE("edge scaler: a faint difference is not an edge")
{
    const std::vector<u16> image = {kBlack, kBlack, kBlack, kFaint};
    const std::vector<u32> scaled = Scaled(image, 2, 2, 2);
    // The faint pixel expands its one green level to four of eight (0x0400); nothing blends.
    constexpr u32 kFaintOut = 0xFF000400u;

    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            const bool faint = x >= 2 && y >= 2;
            CHECK(scaled[static_cast<std::size_t>(y) * 4 + x] == (faint ? kFaintOut : kBlackOut));
        }
    }
}

TEST_CASE("edge scaler: the same picture scales the same")
{
    std::vector<u16> image(16 * 16, kBlack);

    for (int y = 0; y < 16; ++y)
    {
        for (int x = 0; x < 16; ++x)
        {
            if (x + y >= 16)
            {
                image[static_cast<std::size_t>(y) * 16 + x] = kWhite;
            }
        }
    }

    const std::vector<u32> first = Scaled(image, 16, 16, 3);
    const std::vector<u32> second = Scaled(image, 16, 16, 3);
    CHECK(first == second);
}

TEST_CASE("edge scaler: rows apart come out the same")
{
    // A picture of one repeated row, taller than the threaded row count: whatever bands the rows are worked in, every
    // row's sub-pixels are that row's own.
    constexpr int kWidth = 8;
    constexpr int kHeight = 128;
    std::vector<u16> image(static_cast<std::size_t>(kWidth) * kHeight, kBlack);

    for (int y = 0; y < kHeight; ++y)
    {
        for (int x = kWidth / 2; x < kWidth; ++x)
        {
            image[static_cast<std::size_t>(y) * kWidth + x] = kWhite;
        }
    }

    const std::vector<u32> scaled = Scaled(image, kWidth, kHeight, 4);

    for (int y = 1; y < kHeight * 4; ++y)
    {
        const bool same = std::equal(
            scaled.begin() + static_cast<std::ptrdiff_t>(y - 1) * kWidth * 4,
            scaled.begin() + static_cast<std::ptrdiff_t>(y) * kWidth * 4,
            scaled.begin() + static_cast<std::ptrdiff_t>(y) * kWidth * 4
        );
        CHECK(same);
    }
}

TEST_CASE("edge scaler: a lone pixel stays whole")
{
    constexpr int kSize = 9;
    std::vector<u16> image(static_cast<std::size_t>(kSize) * kSize, kBlack);
    image[static_cast<std::size_t>(4) * kSize + 4] = kWhite;

    for (int factor : {2, 3})
    {
        const std::vector<u32> scaled = Scaled(image, kSize, kSize, factor);

        for (u32 pixel : scaled)
        {
            CHECK(pixel == kBlackOut || pixel == kWhiteOut);
        }
    }
}

TEST_CASE("edge scaler: a one-pixel line stays whole")
{
    // A line one pixel wide, whose ends' corners sit on the diagonal: the ends' own colour does not continue along
    // the edge, so nothing blends and the line keeps whole pixels.
    constexpr int kWidth = 11;
    constexpr int kHeight = 7;
    std::vector<u16> rows(static_cast<std::size_t>(kWidth) * kHeight, kBlack);
    std::vector<u16> columns(rows);

    for (int x = 4; x <= 6; ++x)
    {
        rows[static_cast<std::size_t>(3) * kWidth + x] = kWhite;
    }

    for (int y = 2; y <= 4; ++y)
    {
        columns[static_cast<std::size_t>(y) * kWidth + 5] = kWhite;
    }

    for (const std::vector<u16>& image : {rows, columns})
    {
        const std::vector<u32> scaled = Scaled(image, kWidth, kHeight, 2);

        for (int y = 0; y < kHeight * 2; ++y)
        {
            for (int x = 0; x < kWidth * 2; ++x)
            {
                const bool white = image[static_cast<std::size_t>(y / 2) * kWidth + x / 2] == kWhite;
                CHECK(scaled[static_cast<std::size_t>(y) * kWidth * 2 + x] == (white ? kWhiteOut : kBlackOut));
            }
        }
    }
}

TEST_CASE("edge scaler: a one-pixel diagonal rounds its steps")
{
    // A diagonal one pixel wide, like a thin font stroke: each step's corners facing the diagonal blend into the
    // ground (the stroke continues along the edge), while the ends, where it stops, stay whole.
    constexpr int kSize = 12;
    std::vector<u16> image(static_cast<std::size_t>(kSize) * kSize, kBlack);

    for (int i = 2; i <= 9; ++i)
    {
        image[static_cast<std::size_t>(i) * kSize + i] = kWhite;
    }

    const std::vector<u32> scaled = Scaled(image, kSize, kSize, 2);
    int blended = 0;

    for (u32 pixel : scaled)
    {
        if (pixel == kCornerNear)
        {
            ++blended;
        }
        else
        {
            CHECK(pixel == kBlackOut || pixel == kWhiteOut);
        }
    }

    // The six pixels between the ends blend the two sub-pixels of their corners facing the diagonal; the ends blend
    // none. One of them, at (5, 5), takes the blend at the sub-pixels (11, 10) and (10, 11).
    CHECK_EQ(blended, 12);
    CHECK(scaled[static_cast<std::size_t>(10) * kSize * 2 + 11] == kCornerNear);
    CHECK(scaled[static_cast<std::size_t>(11) * kSize * 2 + 10] == kCornerNear);

    for (int y = 0; y < 2; ++y)
    {
        for (int x = 0; x < 2; ++x)
        {
            CHECK(scaled[static_cast<std::size_t>(4 + y) * kSize * 2 + 4 + x] == kWhiteOut);
        }
    }
}

TEST_CASE("edge scaler: a one-pixel fringe leaves its sides whole")
{
    // A diagonal one pixel wide between two colours, like a role's outline between its body and the ground: the
    // colour past the fringe does not reach on, so neither side's pixels take the fringe's colour at their corners.
    constexpr int kSize = 13;
    constexpr u16 kFringe = 0x7BEF;
    std::vector<u16> image(static_cast<std::size_t>(kSize) * kSize, kBlack);

    for (int y = 0; y < kSize; ++y)
    {
        for (int x = 0; x < kSize; ++x)
        {
            const int across = x + y;
            image[static_cast<std::size_t>(y) * kSize + x] = across < 7 ? kBlack : (across == 7 ? kFringe : kWhite);
        }
    }

    const std::vector<u32> scaled = Scaled(image, kSize, kSize, 2);

    // The frame's edge reads as its own pixels, so beside it the fringe seems to reach on; the assertion keeps away
    // from it. Away from it, neither side of the fringe blends.
    for (int y = 2; y < kSize - 2; ++y)
    {
        for (int x = 2; x < kSize - 2; ++x)
        {
            const u16 pixel = image[static_cast<std::size_t>(y) * kSize + x];

            if (pixel == kFringe)
            {
                continue;
            }

            const u32 expected = pixel == kBlack ? kBlackOut : kWhiteOut;

            for (int sy = 0; sy < 2; ++sy)
            {
                for (int sx = 0; sx < 2; ++sx)
                {
                    CHECK(scaled[static_cast<std::size_t>(y * 2 + sy) * kSize * 2 + x * 2 + sx] == expected);
                }
            }
        }
    }
}

TEST_CASE("edge scaler: a checkerboard dither stays whole")
{
    // Every second pixel differs: the colour beyond each corner does not reach on, and this pixel's own colour
    // touches it diagonally, so no corner blends.
    constexpr int kSize = 8;
    std::vector<u16> image(static_cast<std::size_t>(kSize) * kSize, kBlack);

    for (int y = 0; y < kSize; ++y)
    {
        for (int x = 0; x < kSize; ++x)
        {
            if ((x + y) % 2 != 0)
            {
                image[static_cast<std::size_t>(y) * kSize + x] = kWhite;
            }
        }
    }

    const std::vector<u32> scaled = Scaled(image, kSize, kSize, 2);

    for (int y = 0; y < kSize * 2; ++y)
    {
        for (int x = 0; x < kSize * 2; ++x)
        {
            const bool white = image[static_cast<std::size_t>(y / 2) * kSize + x / 2] == kWhite;
            CHECK(scaled[static_cast<std::size_t>(y) * kSize * 2 + x] == (white ? kWhiteOut : kBlackOut));
        }
    }
}

TEST_CASE("edge scaler: a woven canvas of whole blocks scales as the frame")
{
    // The ground at a render scale: every block holds one colour throughout, and the woven canvas scales to the
    // unwoven frame's picture at any parts (the corners judged on each block's north-west sample).
    const std::vector<u16> image = {kBlack, kBlack, kBlack, kWhite};

    for (int parts : {2, 3})
    {
        for (int factor : {2, 3, 4})
        {
            CHECK(ScaledWoven(Woven(image, 2, 2, parts), 2, 2, parts, factor) == Scaled(image, 2, 2, factor));
        }
    }

    const std::vector<u16> plain(6 * 4, kWhite);
    CHECK(ScaledWoven(Woven(plain, 6, 4, 2), 6, 4, 2, 4) == Scaled(plain, 6, 4, 4));
}

TEST_CASE("edge scaler: a woven block keeps its own samples")
{
    // A block whose samples differ (a depth-scaled role's woven sampling) keeps every sample it holds: a lone pixel
    // blends nothing, so each sub-pixel takes its own woven colour. The block's north-west sample stays the whole
    // pixel's colour the corners are judged on.
    constexpr int kSize = 9;
    constexpr int kParts = 2;
    std::vector<u16> woven = Woven(std::vector<u16>(kSize * kSize, kBlack), kSize, kSize, kParts);
    const std::size_t block = static_cast<std::size_t>(4) * kParts * kSize * kParts + 4 * kParts;
    woven[block] = kWhite;

    const std::vector<u32> scaled = ScaledWoven(woven, kSize, kSize, kParts, kParts);

    for (int y = 0; y < kSize * kParts; ++y)
    {
        for (int x = 0; x < kSize * kParts; ++x)
        {
            const u32 expected = x == 8 && y == 8 ? kWhiteOut : kBlackOut;
            CHECK(scaled[static_cast<std::size_t>(y) * kSize * kParts + x] == expected);
        }
    }
}

TEST_CASE("edge scaler: a woven block blends its corner and passes the rest through")
{
    // The corner an edge crosses blends from the sample it sits on — the block's north-west one, the pixel's own
    // colour — while the block's other sub-pixels, which the blend does not reach, take their own woven colours: the
    // corner as unwoven, the rest the ground's.
    const std::vector<u16> image = {kBlack, kBlack, kBlack, kWhite};
    constexpr int kParts = 2;
    std::vector<u16> woven = Woven(image, 2, 2, kParts);
    const std::size_t block = static_cast<std::size_t>(1) * kParts * 2 * kParts + 1 * kParts;

    for (int offset : {1, 2, 3})
    {
        woven[block + (offset / kParts) * 2 * kParts + offset % kParts] = kBlack;
    }

    const std::vector<u32> scaled = ScaledWoven(woven, 2, 2, kParts, kParts);

    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            const bool corner = x == 2 && y == 2;
            CHECK(scaled[static_cast<std::size_t>(y) * 4 + x] == (corner ? kCornerNear : kBlackOut));
        }
    }
}
