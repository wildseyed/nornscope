// ndi_loader.h — runtime NDI loading for both platforms.
// Windows: resolves NDIlib_v6_load from the NDI runtime DLL (NDIRedistV6 must
//          be installed — same requirement as DistroAV), no import lib needed.
// Linux:   calls NDIlib_v6_load from the libndi we already link against.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <Processing.NDI.Lib.h>
#include <Processing.NDI.DynamicLoad.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static const NDIlib_v6* ndi_load_runtime() {
#ifdef _WIN32
    const char* dll = "Processing.NDI.Lib.x64.dll";
    HMODULE h = LoadLibraryA(dll);
    if (!h) {  // NDI runtime installer sets this env var
        const char* dir = getenv("NDI_RUNTIME_DIR_V6");
        if (dir) {
            char path[1024];
            snprintf(path, sizeof path, "%s\\%s", dir, dll);
            h = LoadLibraryA(path);
        }
    }
    if (!h) return nullptr;
    void* fn = (void*)GetProcAddress(h, "NDIlib_v6_load");
    return fn ? ((const NDIlib_v6* (*)(void))fn)() : nullptr;
#else
    return NDIlib_v6_load();
#endif
}
