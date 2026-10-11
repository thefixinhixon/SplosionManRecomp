// android_main.cpp - Android entry point for the 'Splosion Man recomp.
//
// Replaces the SDK's windowed_app_main_sdl.cpp (desktop main) for the
// Android build. Differences from desktop:
//   - The app is a shared library (libmain.so) loaded by SDLActivity, so
//     the entry point is SDL_main (SDL_main.h renames main() for us).
//   - On Android, REX_DEFINE_APP registers the app creator in a table
//     (XE_UI_WINDOWED_APPS_IN_LIBRARY) instead of exporting a getter, so
//     we look the creator up by name.
// Everything else mirrors RunWindowedApp: cvar init, app context, message
// loop. All Android configuration (paths, GPU plugin, license, logging)
// happens inside SplosionmanApp's hooks - see splosionman_app.h.
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/platform.h>
#include <rex/thread.h>
#include <rex/ui/windowed_app.h>
#include <rex/ui/windowed_app_context_sdl.h>

#include "tp_mark.h"

namespace {

int RunWindowedApp(int argc, char** argv) {
  tp::Mark("main: entry");
  // Load the dynamically resolved Android libc/libandroid entry points
  // (ASharedMemory_create for guest RAM, pthread_getname_np). The SDK
  // declares these hooks but, unlike Xenia's Android main, nothing in the
  // SDL desktop main ever called them.
  rex::thread::AndroidInitialize();
  rex::memory::AndroidInitialize();
  tp::Mark("main: android subsystems initialized");

  auto remaining = rex::cvar::Init(argc, argv);
  rex::cvar::ApplyEnvironment();
  rex::InitLoggingEarly();

  int result;
  {
    rex::ui::SDLWindowedAppContext app_context;
    if (!app_context.Initialize()) {
      return EXIT_FAILURE;
    }

    auto creator = rex::ui::WindowedApp::GetCreator("splosionman");
    if (!creator) {
      tp::Mark("main: FAIL no app creator registered");
      REXLOG_ERROR("No WindowedApp creator registered for 'splosionman'");
      return EXIT_FAILURE;
    }
    std::unique_ptr<rex::ui::WindowedApp> app = creator(app_context);
    tp::Mark("main: app created, initializing");

    const auto& option_names = app->GetPositionalOptions();
    std::map<std::string, std::string> parsed;
    size_t count = std::min(remaining.size(), option_names.size());
    for (size_t i = 0; i < count; ++i) {
      parsed[option_names[i]] = remaining[i];
    }
    app->SetParsedArguments(std::move(parsed));

    bool initialized = app->OnInitialize();
    tp::Mark(initialized ? "main: OnInitialize OK, entering message loop"
                         : "main: OnInitialize FAILED");
    result = initialized ? app_context.RunMainMessageLoop() : EXIT_FAILURE;

    app->InvokeOnDestroy();
  }

  return result;
}

}  // namespace

int main(int argc, char* argv[]) {
  return RunWindowedApp(argc, argv);
}
