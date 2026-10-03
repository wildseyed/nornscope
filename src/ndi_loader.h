// ndi_loader.h — runtime NDI loading for both platforms.
// Windows: resolves NDIlib_v6_load from the NDI runtime DLL (NDIRedistV6 must
//          be installed — same requirement as DistroAV), no import lib needed.
// Linux:   dlopens libndi.so.6 (from the NDI SDK for Linux or a distro
//          package) at runtime, so the binaries don't link against libndi.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <Processing.NDI.Lib.h>
#include <Processing.NDI.DynamicLoad.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
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
    // honor an explicit override first, then the usual linker search paths
    void* h = nullptr;
    const char* dir = getenv("NDI_RUNTIME_DIR_V6");
    if (dir) {
        char path[1024];
        snprintf(path, sizeof path, "%s/libndi.so.6", dir);
        h = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
    }
    if (!h) h = dlopen("libndi.so.6", RTLD_LAZY | RTLD_LOCAL);
    if (!h) h = dlopen("libndi.so", RTLD_LAZY | RTLD_LOCAL);
    if (!h) return nullptr;
    void* fn = dlsym(h, "NDIlib_v6_load");
    return fn ? ((const NDIlib_v6* (*)(void))fn)() : nullptr;
#endif
}
