#include <rex/ui/gpu_suspend.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <pthread.h>
#endif

namespace rex::ui {

namespace {
std::atomic<bool> suspended_{false};
std::mutex mutex_;
std::condition_variable resumed_;

std::mutex drains_mutex_;
std::vector<std::pair<GpuDrainId, std::function<void()>>> drains_;
GpuDrainId next_drain_id_ = 1;
}  // namespace

void SetGpuSubmissionSuspended(bool suspended) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    suspended_.store(suspended, std::memory_order_release);
  }
  if (!suspended) {
    resumed_.notify_all();
    return;
  }
  std::lock_guard<std::mutex> lock(drains_mutex_);
  for (auto& [id, drain] : drains_) {
    drain();
  }
}

GpuDrainId AddGpuDrainCallback(std::function<void()> drain) {
  std::lock_guard<std::mutex> lock(drains_mutex_);
  GpuDrainId id = next_drain_id_++;
  drains_.emplace_back(id, std::move(drain));
  return id;
}

void RemoveGpuDrainCallback(GpuDrainId id) {
  std::lock_guard<std::mutex> lock(drains_mutex_);
  std::erase_if(drains_, [id](const auto& entry) { return entry.first == id; });
}

bool IsGpuSubmissionSuspended() {
  return suspended_.load(std::memory_order_acquire);
}

void WaitForGpuSubmissionResume() {
  if (!suspended_.load(std::memory_order_acquire)) {
    return;
  }
#if defined(__APPLE__)
  if (pthread_main_np()) {
    return;
  }
#endif
  std::unique_lock<std::mutex> lock(mutex_);
  resumed_.wait(lock, [] { return !suspended_.load(std::memory_order_acquire); });
}

}  // namespace rex::ui
