#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_TESTS_EXECUTION_EXECUTIONTEST_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_TESTS_EXECUTION_EXECUTIONTEST_HPP

#include "VulkanTestDevice.hpp"
#include "Recompiler.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iostream>
#include <span>
#include <string>
#include <vector>

inline std::array<std::uint32_t, 4> BufferDescriptor(const void* data, std::uint32_t records, std::uint32_t stride = 0u) {
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu) | (stride << 16u), records, 0x01016facu};
}

inline std::vector<std::uint32_t> BufferUserData(const std::array<std::uint32_t, 4>& input, const std::array<std::uint32_t, 4>& output) {
    std::vector<std::uint32_t> userData(8, 0u);
    std::copy(input.begin(), input.end(), userData.begin());
    std::copy(output.begin(), output.end(), userData.begin() + 4);
    return userData;
}

inline std::string Hex(std::uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08x", value);
    return text;
}

inline void DispatchCompute(AgcDriver::VulkanDevice& device, std::span<const std::uint32_t> code, std::span<const std::uint32_t> userData, std::uint32_t threads, std::uint32_t waveSize, const ShaderRecompiler::SpirvTarget& target) {
    const std::array<ShaderRecompiler::MemoryRegion, 1> memory{{{reinterpret_cast<std::uintptr_t>(code.data()), std::as_bytes(code)}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{threads, 1, 1}, 0u, {false, false, false}, false, 1};
    ShaderRecompiler::RecompileRequest request{
        {ShaderRecompiler::ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(code.data()), code, 0, {}},
        {waveSize, 0, userData, compute, std::nullopt, std::nullopt, memory},
        target,
        {0, 0, 0, 128}
    };
    request.useCache = false;
    const auto result = ShaderRecompiler::Recompile(request);
    device.Dispatch(result, 1, 1, 1, {}, reinterpret_cast<std::uintptr_t>(code.data()));
    device.WaitIdle();
}

inline void DispatchCompute(AgcDriver::VulkanDevice& device, std::span<const std::uint32_t> code, std::span<const std::uint32_t> userData, std::uint32_t threads, std::uint32_t waveSize = 32u) {
    DispatchCompute(device, code, userData, threads, waveSize, device.Target());
}

template <typename TBody>
int RunVulkanTest(const char* passed, TBody body) {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        body(*device);
        std::puts(passed);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#endif
