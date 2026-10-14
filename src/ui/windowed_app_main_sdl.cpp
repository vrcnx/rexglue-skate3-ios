#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/platform.h>
#include <rex/ui/windowed_app.h>
#include <rex/ui/windowed_app_context_sdl.h>

#include <SDL3/SDL.h>

#if REX_PLATFORM_IOS
// UIKit must own the process entry: SDL_RunApp starts UIApplicationMain and
// calls back into RexSDLMain once the app has launched. The app target's own
// main() calls rex_ios_main (see ios/main.m), which keeps the entry point
// explicit instead of relying on the linker pulling main out of an archive.
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>

static int RexSDLMain(int argc, char** argv);

#include <rex/ui/gpu_suspend.h>

// Called synchronously from the UIKit app delegate callbacks, so submission
// is suspended before iOS starts refusing GPU work.
static bool HandleAppLifecycleEvent(void*, SDL_Event* event) {
  switch (event->type) {
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
      REXLOG_INFO("Entering background: suspending GPU submission");
      rex::ui::SetGpuSubmissionSuspended(true);
      break;
    case SDL_EVENT_DID_ENTER_FOREGROUND:
      REXLOG_INFO("Entered foreground: resuming GPU submission");
      rex::ui::SetGpuSubmissionSuspended(false);
      break;
    default:
      break;
  }
  return true;
}

extern "C" int rex_ios_main(int argc, char** argv) {
  return SDL_RunApp(argc, argv, RexSDLMain, nullptr);
}

static int RexSDLMain(int argc, char** argv) {
  // SDL may call the main function once per connected scene; only the first
  // call owns the app.
  static bool started = false;
  if (started) {
    return EXIT_SUCCESS;
  }
  started = true;
#else
int main(int argc, char** argv) {
#endif
  auto remaining = rex::cvar::Init(argc, argv);
  rex::cvar::ApplyEnvironment();
  rex::InitLoggingEarly();

#if REX_PLATFORM_IOS
  SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
  SDL_AddEventWatch(HandleAppLifecycleEvent, nullptr);
#elif REX_PLATFORM_MAC
  // Use native macOS fullscreen so the menu bar and app switching behave like
  // other Mac apps. MoltenVK presentation is paced separately on macOS.
  SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "1");
  SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_MENU_VISIBILITY, "1");
#endif

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::fprintf(stderr, "Failed to initialize SDL video: %s\n", SDL_GetError());
    return EXIT_FAILURE;
  }

  int result = EXIT_FAILURE;
  {
    rex::ui::SDLWindowedAppContext app_context;
    std::unique_ptr<rex::ui::WindowedApp> app = rex::ui::GetWindowedAppCreator()(app_context);

    const auto& option_names = app->GetPositionalOptions();
    std::map<std::string, std::string> parsed;
    size_t count = std::min(remaining.size(), option_names.size());
    for (size_t i = 0; i < count; ++i) {
      parsed[option_names[i]] = remaining[i];
    }
    app->SetParsedArguments(std::move(parsed));

    bool initialized = app->OnInitialize();
    result = initialized ? app_context.RunMainLoop() : EXIT_FAILURE;
#if REX_PLATFORM_MAC
    // Skip app/runtime teardown entirely: guest threads cannot be reliably
    // stopped on Darwin (pthread_cancel only lands at cancellation points,
    // never in CPU-bound recompiled code), so the destructor chain races
    // still-running guest threads over freed kernel objects and turns a
    // normal quit into the macOS crash-reporter dialog. Flush the logs and
    // leave; the OS reclaims everything else. Flush-only on purpose -
    // ShutdownLogging destroys sinks a straggler thread may still be using.
    rex::FlushLogging();
    std::_Exit(result);
#else
    app->InvokeOnDestroy();
#endif
  }

  rex::ShutdownLogging();
  SDL_Quit();
  return result;
}
