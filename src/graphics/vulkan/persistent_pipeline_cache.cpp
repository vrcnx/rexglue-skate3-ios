#include <rex/graphics/vulkan/persistent_pipeline_cache.h>

#include <atomic>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>
#include <vector>

#include <rex/logging.h>
#include <rex/platform.h>
#include <rex/ui/gpu_suspend.h>

#if REX_PLATFORM_IOS
// MoltenVK is linked statically on iOS.
extern "C" {
VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineCache(VkDevice device,
                                                     const VkPipelineCacheCreateInfo* create_info,
                                                     const VkAllocationCallbacks* allocator,
                                                     VkPipelineCache* cache);
VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineCacheData(VkDevice device, VkPipelineCache cache,
                                                      size_t* data_size, void* data);
}
#endif

namespace rex::graphics::vulkan {

namespace {
VkDevice g_device = VK_NULL_HANDLE;
std::atomic<VkPipelineCache> g_cache{VK_NULL_HANDLE};
std::filesystem::path g_file;
std::mutex g_save_mutex;
std::atomic<uint32_t> g_created_since_save{0};
std::atomic<bool> g_save_in_flight{false};
constexpr uint32_t kSaveEveryPipelines = 64;
}  // namespace

void InitializePersistentPipelineCache(VkDevice device, const std::filesystem::path& file) {
#if REX_PLATFORM_IOS
  if (g_cache.load() != VK_NULL_HANDLE || device == VK_NULL_HANDLE) {
    return;
  }
  std::vector<char> initial;
  {
    std::ifstream in(file, std::ios::binary);
    if (in) {
      initial.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
  }
  VkPipelineCacheCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  info.initialDataSize = initial.size();
  info.pInitialData = initial.empty() ? nullptr : initial.data();
  VkPipelineCache cache = VK_NULL_HANDLE;
  VkResult result = vkCreatePipelineCache(device, &info, nullptr, &cache);
  if (result != VK_SUCCESS && !initial.empty()) {
    // Stale or damaged file (driver update, other device): start empty.
    info.initialDataSize = 0;
    info.pInitialData = nullptr;
    result = vkCreatePipelineCache(device, &info, nullptr, &cache);
  }
  if (result != VK_SUCCESS) {
    REXLOG_WARN("Persistent pipeline cache unavailable ({})", int32_t(result));
    return;
  }
  g_device = device;
  g_file = file;
  g_cache.store(cache);
  REXLOG_INFO("Persistent pipeline cache: {} ({} KB loaded)", file.string(),
              initial.size() / 1024);
  // Leaving the app is the last reliable moment on iOS (a swipe-kill gives
  // no exit callback), so save whenever it is backgrounded.
  rex::ui::AddGpuDrainCallback([] { SavePersistentPipelineCache(); });
#else
  (void)device;
  (void)file;
#endif
}

VkPipelineCache GetPersistentPipelineCache() {
  return g_cache.load(std::memory_order_relaxed);
}

void NotePersistentPipelineCreated() {
  if (g_cache.load(std::memory_order_relaxed) == VK_NULL_HANDLE) {
    return;
  }
  if (g_created_since_save.fetch_add(1, std::memory_order_relaxed) + 1 < kSaveEveryPipelines) {
    return;
  }
  bool expected = false;
  if (!g_save_in_flight.compare_exchange_strong(expected, true)) {
    return;
  }
  std::thread([] {
    SavePersistentPipelineCache();
    g_save_in_flight.store(false);
  }).detach();
}

void SavePersistentPipelineCache() {
#if REX_PLATFORM_IOS
  const VkPipelineCache cache = g_cache.load();
  if (cache == VK_NULL_HANDLE) {
    return;
  }
  std::lock_guard<std::mutex> lock(g_save_mutex);
  g_created_since_save.store(0, std::memory_order_relaxed);
  size_t size = 0;
  if (vkGetPipelineCacheData(g_device, cache, &size, nullptr) != VK_SUCCESS || !size) {
    return;
  }
  std::vector<char> data(size);
  if (vkGetPipelineCacheData(g_device, cache, &size, data.data()) != VK_SUCCESS) {
    return;
  }
  std::error_code ec;
  std::filesystem::create_directories(g_file.parent_path(), ec);
  // Write then rename, so a kill mid-write never leaves a truncated cache.
  const auto temp = g_file.string() + ".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out.write(data.data(), std::streamsize(size));
    if (!out) {
      return;
    }
  }
  std::filesystem::rename(temp, g_file, ec);
#endif
}

}  // namespace rex::graphics::vulkan
