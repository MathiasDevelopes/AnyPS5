#include "prx/libSceAgcDriver/Graphics/include/DepthSurface.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Resources.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Texture.hpp"
#include "prx/libSceAgcDriver/Graphics/include/TextureDetiler.hpp"
#include "VulkanTestDevice.hpp"
#include <SDL_loadso.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace AgcDriver::Graphics;

constexpr VkExtent2D Extent{64, 32};
constexpr std::uint32_t FormatR8Uint = 5;
constexpr std::size_t PlaneBytes = 65536;

alignas(4096) std::array<std::uint8_t, PlaneBytes> Depth{};
alignas(4096) std::array<std::uint8_t, PlaneBytes> Stencil{};
alignas(4096) std::array<std::uint8_t, PlaneBytes> TiledDepth{};
alignas(4096) std::array<std::uint8_t, PlaneBytes> TiledStencil{};
alignas(256) std::array<std::uint32_t, 64> Htile{};
alignas(256) std::array<std::uint32_t, 64> TiledHtile{};
constexpr std::size_t HtileBytes = ((Extent.width + 7u) / 8u) * ((Extent.height + 7u) / 8u) * 4u;

class Device {
public:
    Device() {
#ifdef _WIN32
        library = SDL_LoadObject("vulkan-1.dll");
#else
        library = SDL_LoadObject("libvulkan.so.1");
#endif
        Require(library != nullptr, "cannot load Vulkan");
        try {
            instanceProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_LoadFunction(library, "vkGetInstanceProcAddr"));
            Require(instanceProc != nullptr, "missing Vulkan instance resolver");
            VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
            application.apiVersion = VK_API_VERSION_1_1;
            VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
            info.pApplicationInfo = &application;
            Check(function<PFN_vkCreateInstance>("vkCreateInstance")(&info, nullptr, &instance), "vkCreateInstance");
            std::uint32_t count = 0;
            const auto enumerate = function<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices");
            Check(enumerate(instance, &count, nullptr), "vkEnumeratePhysicalDevices");
            Require(count != 0, "no Vulkan device");
            std::vector<VkPhysicalDevice> devices(count);
            Check(enumerate(instance, &count, devices.data()), "vkEnumeratePhysicalDevices");
            context.physical = devices.front();
            const auto queues = function<PFN_vkGetPhysicalDeviceQueueFamilyProperties>("vkGetPhysicalDeviceQueueFamilyProperties");
            queues(context.physical, &count, nullptr);
            std::vector<VkQueueFamilyProperties> families(count);
            queues(context.physical, &count, families.data());
            std::uint32_t family = 0;
            while (family < count && (families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) ++family;
            Require(family < count, "no Vulkan graphics queue");
            const float priority = 1;
            VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            queue.queueFamilyIndex = family;
            queue.queueCount = 1;
            queue.pQueuePriorities = &priority;
            VkDeviceCreateInfo device{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
            device.queueCreateInfoCount = 1;
            device.pQueueCreateInfos = &queue;
            Check(function<PFN_vkCreateDevice>("vkCreateDevice")(context.physical, &device, nullptr, &context.device), "vkCreateDevice");
            context.deviceProc = function<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");
            context.formatProperties = function<PFN_vkGetPhysicalDeviceFormatProperties>("vkGetPhysicalDeviceFormatProperties");
            function<PFN_vkGetPhysicalDeviceMemoryProperties>("vkGetPhysicalDeviceMemoryProperties")(context.physical, &context.memory);
            VkPhysicalDeviceProperties properties{};
            function<PFN_vkGetPhysicalDeviceProperties>("vkGetPhysicalDeviceProperties")(context.physical, &properties);
            context.limits = properties.limits;
            context.Function<PFN_vkGetDeviceQueue>("vkGetDeviceQueue")(context.device, family, 0, &context.queue);
            VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            pool.queueFamilyIndex = family;
            Check(context.Function<PFN_vkCreateCommandPool>("vkCreateCommandPool")(context.device, &pool, nullptr, &context.pool), "vkCreateCommandPool");
        } catch (...) {
            release();
            throw;
        }
    }

    ~Device() { release(); }
    const Context& GetContext() const { return context; }

private:
    template<typename TFunction>
    TFunction function(const char* name) const {
        const auto result = reinterpret_cast<TFunction>(instanceProc(instance, name));
        Require(result != nullptr, name);
        return result;
    }

    void release() noexcept {
        if (context.device != VK_NULL_HANDLE) ClearDepthSurfaces(context.device);
        if (context.pool != VK_NULL_HANDLE) context.Function<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(context.device, context.pool, nullptr);
        context.bufferPool.reset();
        if (context.device != VK_NULL_HANDLE) function<PFN_vkDestroyDevice>("vkDestroyDevice")(context.device, nullptr);
        if (instance != VK_NULL_HANDLE) function<PFN_vkDestroyInstance>("vkDestroyInstance")(instance, nullptr);
        if (library != nullptr) SDL_UnloadObject(library);
    }

    void* library = nullptr;
    PFN_vkGetInstanceProcAddr instanceProc = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    Context context{};
};

std::uint64_t Address(const std::array<std::uint8_t, PlaneBytes>& plane) {
    return reinterpret_cast<std::uint64_t>(plane.data());
}

std::shared_ptr<StorageTexture> StencilStorage(const Context& context, std::uint64_t address) {
    GuestTextureResource resource{};
    resource.baseAddress = address;
    resource.width = Extent.width;
    resource.height = Extent.height;
    resource.mipCount = 1;
    resource.dimension = TextureDimension::k2D;
    resource.format = FormatR8Uint;
    return std::make_shared<StorageTexture>(context, *context.detiler, resource, 0);
}

bool Holds(const Context& context, const StorageTexture& storage, std::uint8_t value) {
    Buffer readback(context, static_cast<std::size_t>(Extent.width) * Extent.height, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    CommandBatch batch(context);
    RecordMemoryBarrier(context, batch.Handle(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {Extent.width, Extent.height, 1};
    context.Function<PFN_vkCmdCopyImageToBuffer>("vkCmdCopyImageToBuffer")(batch.Handle(), storage.Image(), VK_IMAGE_LAYOUT_GENERAL, readback.Handle(), 1, &copy);
    RecordMemoryBarrier(context, batch.Handle(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT);
    batch.SubmitAndWait();
    for (const auto byte : readback.Bytes()) {
        if (std::to_integer<std::uint8_t>(byte) != value) return false;
    }
    return true;
}

void Fill(const Context& context, const StorageTexture& storage, std::uint8_t value) {
    CommandBatch batch(context);
    RecordMemoryBarrier(context, batch.Handle(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    VkClearColorValue clear{};
    clear.uint32[0] = value;
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    context.Function<PFN_vkCmdClearColorImage>("vkCmdClearColorImage")(batch.Handle(), storage.Image(), VK_IMAGE_LAYOUT_GENERAL, &clear, 1, &range);
    RecordMemoryBarrier(context, batch.Handle(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT);
    batch.SubmitAndWait();
}

bool RefusedInHtile(const Context& context, const std::shared_ptr<StorageTexture>& storage) {
    try {
        SeedStorageFromDepth(context, storage);
    } catch (const std::runtime_error& error) {
        return std::string(error.what()).find("while HTILE holds its stencil state is not implemented") != std::string::npos;
    }
    return false;
}

void Run(const Context& base) {
    TextureDetiler detiler(base);
    auto context = base;
    context.detiler = &detiler;

    const auto htile = reinterpret_cast<std::uint64_t>(Htile.data());
    const DepthTarget target{Address(Depth), Address(Stencil), Extent, VK_FORMAT_D32_SFLOAT_S8_UINT, 1.0f, 0x11, htile, false};
    DepthSurfaceView(context, target);
    const auto first = StencilStorage(context, Address(Stencil));
    SeedStorageFromDepth(context, first);
    Require(Holds(context, *first, 0x11), "an R8 storage image over the stencil plane did not read the stencil cleared to 0x11");

    Fill(context, *first, 0x15);
    DepthSurfaceView(context, target);
    const auto second = StencilStorage(context, Address(Stencil));
    SeedStorageFromDepth(context, second);
    Require(Holds(context, *second, 0x15), "0x15 written through R8 storage did not reach the stencil plane");

    Fill(context, *second, 0x16);
    NoteDepthMetadataFill(htile, HtileBytes, 0u);
    DepthSurfaceView(context, target);
    const auto third = StencilStorage(context, Address(Stencil));
    SeedStorageFromDepth(context, third);
    Require(Holds(context, *third, 0x16), "a fast clear of the depth plane alone dropped the stencil written through R8 storage");

    const DepthTarget tiled{Address(TiledDepth), Address(TiledStencil), Extent, VK_FORMAT_D32_SFLOAT_S8_UINT, 1.0f, 0x11, reinterpret_cast<std::uint64_t>(TiledHtile.data()), true};
    DepthSurfaceView(context, tiled);
    Require(RefusedInHtile(context, StencilStorage(context, Address(TiledStencil))), "storage access to a stencil plane HTILE holds was not refused");
}

}

int main() {
    try {
        std::unique_ptr<Device> device;
        try {
            device = std::make_unique<Device>();
        } catch (const std::exception& error) {
            if (std::getenv("ANYPS5_REQUIRE_VULKAN") != nullptr) throw;
            std::printf("skipped, no usable Vulkan device: %s\n", error.what());
            return VulkanTestSkipped;
        }
        Run(device->GetContext());
        std::puts("depth stencil storage tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
