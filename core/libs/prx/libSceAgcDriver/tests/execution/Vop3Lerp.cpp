#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "Recompiler.hpp"
#include "ExecutionTest.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;

constexpr std::uint32_t Threads = 64;
constexpr std::uint32_t Inputs = 4;
constexpr std::uint32_t Results = 16;
constexpr std::uint32_t Literal = 0x01fe01feu;
alignas(256) std::array<std::uint32_t, Threads * Inputs> Input{};
alignas(256) std::array<std::uint32_t, Threads * Results> Output{};

alignas(256) constexpr std::array<std::uint32_t, 44> LerpCode{
    0x34020082, 0x34060084, 0xe0302000, 0x80000401, 0xe0302004, 0x80000501, 0xe0302008, 0x80000601,
    0xbf8c3f70, 0xd54d000a, 0x041a0b04, 0xd54d000b, 0x041a0905, 0xd54d000c, 0x02020b04, 0xd54d000d,
    0x03060b04, 0xd54d000e, 0x03fe0b04, Literal,    0xd54d000f, 0x041a0ac1, 0xd54d0010, 0x041a0904,
    0x7e220304, 0xd54d0011, 0x041a0b11, 0xe0702000, 0x80010a03, 0xe0702004, 0x80010b03, 0xe0702008,
    0x80010c03, 0xe070200c, 0x80010d03, 0xe0702010, 0x80010e03, 0xe0702014, 0x80010f03, 0xe0702018,
    0x80011003, 0xe070201c, 0x80011103, 0xbf810000,
};

constexpr std::array<std::array<std::uint32_t, 4>, 12> Edges{{
    {0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu}, {0xffffffffu, 0u, 0x01010101u, 0x80808080u}, {0xffffffffu, 0u, 0u, 0x7f7f7f7fu},
    {0u, 0u, 0xffffffffu, 0u}, {0x01010101u, 0u, 0x01010101u, 0x01010101u}, {0x01010101u, 0u, 0xfefefefeu, 0u},
    {0xff00ff00u, 0x00ff00ffu, 0x01000100u, 0x807f807fu}, {0x80808080u, 0x7f7f7f7fu, 0x00010001u, 0x7f807f80u},
    {0x000000ffu, 0x0000ff00u, 0x00010000u, 0x00007f7fu}, {0x12345678u, 0x9abcdef0u, 0xffffffffu, 0x56789ab4u},
    {0xfe01fe01u, 0x01fe01feu, 0x80808080u, 0x7f7f7f7fu}, {0u, 0u, 0u, 0u},
}};

void FillInput() {
    std::uint64_t state = 0xd1b54a32d192ed03ull;
    for (std::uint32_t tid = 0; tid < Threads; ++tid) {
        auto* words = &Input[tid * Inputs];
        for (std::uint32_t j = 0; j < 3u; ++j) {
            state = state * 6364136223846793005ull + 1442695040888963407ull;
            words[j] = tid < Edges.size() ? Edges[tid][j] : static_cast<std::uint32_t>(state >> 32u);
        }
    }
}

void Run(AgcDriver::VulkanDevice& device) {
    Output.fill(0xdeadbeefu);
    const auto userData = BufferUserData(BufferDescriptor(Input.data(), static_cast<std::uint32_t>(Input.size()), 4u), BufferDescriptor(Output.data(), static_cast<std::uint32_t>(Output.size()), 4u));
    DispatchCompute(device, LerpCode, userData, Threads);
}

std::uint32_t Lerp(std::uint32_t lhs, std::uint32_t rhs, std::uint32_t rounding) {
    std::uint32_t result = 0u;
    for (std::uint32_t offset = 0; offset < 32u; offset += 8u) {
        const std::uint32_t sum = ((lhs >> offset) & 0xffu) + ((rhs >> offset) & 0xffu) + ((rounding >> offset) & 1u);
        result |= (sum >> 1u) << offset;
    }
    return result;
}

void Check() {
    constexpr std::array<const char*, 8> names{
        "v_lerp_u8", "v_lerp_u8 with swapped sources", "v_lerp_u8 truncating", "v_lerp_u8 rounding every byte", "v_lerp_u8 with a literal round mode",
        "v_lerp_u8 with an inline constant", "v_lerp_u8 of a value with itself", "v_lerp_u8 into its first source",
    };
    for (std::uint32_t tid = 0; tid < Threads; ++tid) {
        const auto* in = &Input[tid * Inputs];
        const std::uint32_t a = in[0];
        const std::uint32_t b = in[1];
        const std::uint32_t c = in[2];
        const std::array<std::uint32_t, 8> expected{
            tid < Edges.size() ? Edges[tid][3] : Lerp(a, b, c),
            Lerp(a, b, c),
            Lerp(a, b, 0u),
            Lerp(a, b, 0xffffffffu),
            Lerp(a, b, Literal),
            Lerp(0xffffffffu, b, c),
            a,
            Lerp(a, b, c),
        };
        for (std::uint32_t j = 0; j < expected.size(); ++j) {
            const std::uint32_t actual = Output[tid * Results + j];
            Require(actual == expected[j], std::string(names[j]) + ": thread " + std::to_string(tid) + " (" + Hex(a) + ", " + Hex(b) + ", " + Hex(c) + ") is " + Hex(actual) + ", expected " + Hex(expected[j]));
        }
    }
}

}

int main() {
    return RunVulkanTest("vop3 lerp tests passed", [](AgcDriver::VulkanDevice& device) {
        FillInput();
        Run(device);
        Check();
    });
}
