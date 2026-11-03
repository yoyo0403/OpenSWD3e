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
