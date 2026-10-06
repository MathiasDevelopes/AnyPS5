#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "Recompiler.hpp"
#include "ExecutionTest.hpp"
#include <array>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;

constexpr std::uint32_t Threads = 64;
constexpr std::uint32_t Stride = 4;
alignas(256) std::array<std::uint32_t, Threads * Stride> Input{};
alignas(256) std::array<std::uint32_t, Threads * Stride> Output{};

alignas(256) constexpr std::array<std::uint32_t, 23> CacheControlCode{
    0x34020082, 0xbf930000, 0xbf940001, 0xe0302000, 0x80000401, 0xf4840000, 0x00000000, 0xf4800000,
    0x00000000, 0xf47c0000, 0x00000000, 0xe1c80000, 0x00000000, 0xe1c40000, 0x00000000, 0xbfa20000,
    0xbf8c3f70, 0xbfa80001, 0x4a080881, 0xbf950001, 0xe0702000, 0x80010401, 0xbf810000,
};

void Run(AgcDriver::VulkanDevice& device) {
    for (std::uint32_t tid = 0; tid < Threads; ++tid) Input[tid * Stride] = tid * 0x01010101u + 7u;
    Output.fill(0xdeadbeefu);
    const auto userData = BufferUserData(BufferDescriptor(Input.data(), static_cast<std::uint32_t>(Input.size()), 4u), BufferDescriptor(Output.data(), static_cast<std::uint32_t>(Output.size()), 4u));
    DispatchCompute(device, CacheControlCode, userData, Threads);
}

void Check() {
    for (std::uint32_t tid = 0; tid < Threads; ++tid) {
        const auto expected = Input[tid * Stride] + 1u;
        const auto actual = Output[tid * Stride];
        Require(actual == expected, "cache control: thread " + std::to_string(tid) + " is " + std::to_string(actual) + ", expected " + std::to_string(expected));
    }
}

}

int main() {
    return RunVulkanTest("cache control tests passed", [](AgcDriver::VulkanDevice& device) {
        Run(device);
        Check();
    });
}
