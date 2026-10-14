#pragma once

#include <functional>

namespace rex::ui {

// iOS refuses (and may terminate the app for) GPU work submitted while the app
// is in the background, so Vulkan submission is suspended across background
// transitions. Never set on other platforms.
void SetGpuSubmissionSuspended(bool suspended);
bool IsGpuSubmissionSuspended();

// Blocks a worker thread (guest GPU, native renderer) until submission
// resumes. Returns immediately on the UI thread: it has to stay free to
// receive the foreground event, so its paints check IsGpuSubmissionSuspended
// and skip instead.
void WaitForGpuSubmissionResume();

// GPU devices register how to wait for their queues to drain. Suspending runs
// every drain after blocking new submissions, so all work already handed to
// Metal finishes before iOS backgrounds the app (work still in flight then is
// aborted, which can leave the device unusable on return).
using GpuDrainId = int;
GpuDrainId AddGpuDrainCallback(std::function<void()> drain);
void RemoveGpuDrainCallback(GpuDrainId id);

}  // namespace rex::ui
