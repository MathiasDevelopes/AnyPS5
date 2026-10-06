#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "Recompiler.hpp"
#include "ExecutionTest.hpp"
#include <array>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;

constexpr std::uint32_t Threads = 32;
constexpr std::uint32_t Stride = 4;
alignas(256) std::array<std::uint32_t, Threads * Stride> Input{};
alignas(256) std::array<std::uint32_t, Threads * Stride> Output{};

alignas(256) constexpr std::array<std::uint32_t, 21> ReadLaneCode{
    0x34020082, 0xe0302000, 0x80000401, 0xe0302004, 0x80000501, 0xbf8c3f70, 0xd7600008, 0x00014b04,
    0x7e280505, 0xd7600009, 0x00002904, 0x7e140208, 0x7e160209, 0x7e180214, 0xe0702000, 0x80010a01,
    0xe0702004, 0x80010b01, 0xe0702008, 0x80010c01, 0xbf810000,
};

void Run(AgcDriver::VulkanDevice& device) {
    for (std::uint32_t tid = 0; tid < Threads; ++tid) {
        Input[tid * Stride] = 0xa0000000u + tid * 0x01010101u;
        Input[tid * Stride + 1] = 69u + tid;
    }
    Output.fill(0xdeadbeefu);
    const auto userData = BufferUserData(BufferDescriptor(Input.data(), static_cast<std::uint32_t>(Input.size()), 4u), BufferDescriptor(Output.data(), static_cast<std::uint32_t>(Output.size()), 4u));
    DispatchCompute(device, ReadLaneCode, userData, Threads);
}

void Check() {
    for (std::uint32_t tid = 0; tid < Threads; ++tid) {
        const std::array<std::uint32_t, 3> expected{Input[5u * Stride], Input[5u * Stride], 69u};
        for (std::uint32_t j = 0; j < expected.size(); ++j) {
            const auto actual = Output[tid * Stride + j];
            Require(actual == expected[j], "read lane: lane " + std::to_string(tid) + " result " + std::to_string(j) + " is " + std::to_string(actual) + ", expected " + std::to_string(expected[j]));
        }
    }
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        if (device->Target().subgroupSize < Threads) {
            std::printf("skipped, subgroup size %u cannot hold a wave32\n", device->Target().subgroupSize);
            return VulkanTestSkipped;
        }
        Run(*device);
        Check();
        std::puts("read lane tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
