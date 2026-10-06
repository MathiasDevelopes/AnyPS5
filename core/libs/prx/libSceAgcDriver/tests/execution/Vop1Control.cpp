#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "Recompiler.hpp"
#include "ExecutionTest.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;

constexpr std::uint32_t Threads = 64;
constexpr std::uint32_t Stride = 4;
alignas(256) std::array<std::uint32_t, Threads * Stride> Input{};
alignas(256) std::array<std::uint32_t, Threads * Stride> Output{};

alignas(256) constexpr std::array<std::uint32_t, 17> Vop1ControlCode{
    0x34020082, 0xe0302000, 0x80000401, 0xbf8c3f70, 0x7e000000, 0x7e003600, 0x7e008200, 0x4a080881,
    0xd5800000, 0x00000000, 0xd59b0000, 0x00000000, 0xd5c10000, 0x00000000, 0xe0702000, 0x80010401, 0xbf810000,
};

void Run(AgcDriver::VulkanDevice& device) {
    for (std::uint32_t tid = 0; tid < Threads; ++tid) Input[tid * Stride] = tid * 0x01010101u + 7u;
    Output.fill(0xdeadbeefu);
    const auto userData = BufferUserData(BufferDescriptor(Input.data(), static_cast<std::uint32_t>(Input.size()), 4u), BufferDescriptor(Output.data(), static_cast<std::uint32_t>(Output.size()), 4u));
    DispatchCompute(device, Vop1ControlCode, userData, Threads);
}

void Check() {
    for (std::uint32_t tid = 0; tid < Threads; ++tid) {
        const auto expected = Input[tid * Stride] + 1u;
        const auto actual = Output[tid * Stride];
        Require(actual == expected, "vop1 control: thread " + std::to_string(tid) + " is " + std::to_string(actual) + ", expected " + std::to_string(expected));
    }
}

}

int main() {
    return RunVulkanTest("vop1 control tests passed", [](AgcDriver::VulkanDevice& device) {
        Run(device);
        Check();
    });
}
