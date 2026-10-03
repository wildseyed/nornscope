NDI_SDK ?= ndi-sdk
SDK_INC := $(NDI_SDK)/include
SDK_LIB := $(NDI_SDK)/lib/x86_64-linux-gnu

CXXFLAGS := -O2 -I$(SDK_INC)
LDFLAGS := -ldl -lpthread
SDL_FLAGS := $(shell pkg-config --cflags --libs sdl2)

# Windows cross-build (mingw-w64 + official SDL2 mingw dev package)
MINGW_CXX ?= x86_64-w64-mingw32-g++
SDL2_MINGW ?= sdl2-mingw/SDL2-2.32.10/x86_64-w64-mingw32
WIN_CXXFLAGS := -O2 -I$(SDK_INC) -I$(SDL2_MINGW)/include/SDL2
WIN_STATIC := -static-libgcc -static-libstdc++ -Wl,-Bstatic -lstdc++ -lwinpthread -lpthread -Wl,-Bdynamic

all: nornscope ndi_grab

nornscope: src/nornscope.cpp src/ndi_loader.h
	$(CXX) $(CXXFLAGS) -o $@ src/nornscope.cpp $(LDFLAGS) $(SDL_FLAGS) -lm

ndi_grab: src/ndi_grab.cpp src/ndi_loader.h
	$(CXX) $(CXXFLAGS) -o $@ src/ndi_grab.cpp $(LDFLAGS)

windows: nornscope.exe ndi_grab.exe

nornscope.exe: src/nornscope.cpp src/ndi_loader.h
	$(MINGW_CXX) $(WIN_CXXFLAGS) -o $@ src/nornscope.cpp \
	  $(WIN_STATIC) -L$(SDL2_MINGW)/lib -lSDL2 -lws2_32

ndi_grab.exe: src/ndi_grab.cpp src/ndi_loader.h
	$(MINGW_CXX) $(WIN_CXXFLAGS) -o $@ src/ndi_grab.cpp $(WIN_STATIC)

# zip with everything a Windows user needs except the NDI runtime itself
dist-windows: windows
	mkdir -p dist
	cp $(SDL2_MINGW)/bin/SDL2.dll dist/
	cp /usr/x86_64-w64-mingw32/sys-root/mingw/bin/libwinpthread-1.dll dist/
	cp nornscope.exe ndi_grab.exe dist/
	cd dist && zip -q nornscope-windows-x64.zip nornscope.exe ndi_grab.exe SDL2.dll libwinpthread-1.dll

# tarball with the Linux binaries (libndi is dlopened at runtime, so the
# NDI SDK is not needed to build or ship these — users install it separately)
dist-linux: all
	mkdir -p dist
	tar -czf dist/nornscope-linux-x64.tar.gz nornscope ndi_grab README.md LICENSE

clean:
	rm -f nornscope ndi_grab nornscope.exe ndi_grab.exe

.PHONY: all windows dist-windows dist-linux clean
