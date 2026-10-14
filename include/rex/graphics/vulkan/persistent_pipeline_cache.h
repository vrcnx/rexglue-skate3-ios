#pragma once

#include <filesystem>

#include <rex/ui/vulkan/api.h>

namespace rex::graphics::vulkan {

// A VkPipelineCache shared by every pipeline the Vulkan backends build and
// kept on disk between launches. With MoltenVK it holds the SPIR-V -> MSL
// translations, and the OS keeps its own cache of the compiled Metal code, so
// a pipeline seen in an earlier session no longer stalls the frame that first
// needs it. iOS only for now (MoltenVK is linked statically there); elsewhere
// GetPersistentPipelineCache() stays VK_NULL_HANDLE.
void InitializePersistentPipelineCache(VkDevice device, const std::filesystem::path& file);
VkPipelineCache GetPersistentPipelineCache();
// Call after creating a pipeline with the cache; saves on a background thread
// every so many new pipelines so a crash or kill loses little.
void NotePersistentPipelineCreated();
// Writes the cache now (synchronous). Safe to call from any thread.
void SavePersistentPipelineCache();

}  // namespace rex::graphics::vulkan
