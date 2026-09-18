// The game renders its real-time mirrors through CMirrors::BeforeMainRender
// into a texture no larger than the mirror itself, with no anti-aliasing, so
// every edge in the reflection shimmers. Multisampling the mirror target does
// not work: an off-screen camera pass into a multisampled surface loses the
// depth comparison. The plugin instead redirects the RsCameraBeginUpdate and
// RwCameraEndUpdate calls inside BeforeMainRender so the pass renders into a
// plain surface two, four or eight times the mirror size, then halves the
// result step by step back into the mirror texture. The game reads the same
// texture it always did; only its contents are smoother.

#include "config.h"
#include "supersample.h"

#include <windows.h>

namespace {

DWORD WINAPI Initialize(void* parameter) {
    const Settings settings = LoadSettings(static_cast<HMODULE>(parameter));
    // The RenderWare device exists only after the game has finished its own
    // startup, which the loader does not wait for.
    Sleep(1000);
    InstallSupersampling(settings.supersample);
    return 0;
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void*) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        if (HANDLE thread = CreateThread(nullptr, 0, Initialize, instance, 0, nullptr)) {
            CloseHandle(thread);
        }
    }
    return TRUE;
}
