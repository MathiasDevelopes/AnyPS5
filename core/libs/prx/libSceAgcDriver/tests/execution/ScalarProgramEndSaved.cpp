#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "Recompiler.hpp"
#include "ExecutionTest.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;

constexpr std::uint32_t Threads = 32;
constexpr std::uint32_t Inputs = 4;
constexpr std::uint32_t Results = 16;
alignas(256) std::array<std::uint32_t, Threads * Inputs> Input{};
alignas(256) std::array<std::uint32_t, Threads * Results> Output{};

alignas(256) constexpr std::array<std::uint32_t, 30> Code{
    0x34020084, 0x34060086, 0xe0301000, 0x80000401, 0xbf8c3f70, 0xbf800000, 0xf4980000, 0xfa000000,
    0xf49c0000, 0xfa000000, 0xf4a00000, 0xfa000000, 0xf4a40000, 0xfa000000, 0xbfa40000, 0x4a0a0881,
    0x7e0c02ff, 0x00000bad, 0xbf060000, 0xbf840007, 0xbf840003, 0xe0701000, 0x80010503, 0xbf9b0000,
    0xe0701000, 0x80010603, 0xbf9e0000, 0xe0701004, 0x80010603, 0xbf810000,
};

void Fill(std::uint32_t tid, std::uint32_t* words) { words[0] = tid * 0x01010101u + 7u; }

void Expect(std::uint32_t tid, std::uint32_t actual, std::uint32_t expected, const char* name) {
    Require(actual == expected, std::string("scalar program end saved: lane ") + std::to_string(tid) + " " + name + " is " + Hex(actual) + ", expected " + Hex(expected));
}

void Run(AgcDriver::VulkanDevice& device) {
    for (std::uint32_t tid = 0; tid < Threads; ++tid) Fill(tid, &Input[tid * Inputs]);
    Output.fill(0xdeadbeefu);
    const auto userData = BufferUserData(BufferDescriptor(Input.data(), static_cast<std::uint32_t>(Input.size() * 4u)), BufferDescriptor(Output.data(), static_cast<std::uint32_t>(Output.size() * 4u)));
    DispatchCompute(device, Code, userData, Threads);
}

void Check() {
    for (std::uint32_t tid = 0; tid < Threads; ++tid) {
        const std::uint32_t* in = &Input[tid * Inputs];
        const std::uint32_t* out = &Output[tid * Results];
        Expect(tid, out[0], in[0] + 1u, "store before the end");
        Expect(tid, out[1], 0xdeadbeefu, "nothing past the end");
    }
}

}

int main() {
    return RunVulkanTest("scalar program end saved tests passed", [](AgcDriver::VulkanDevice& device) {
        Run(device);
        Check();
    });
}
