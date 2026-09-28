NDI_SDK ?= ndi-sdk
SDK_INC := $(NDI_SDK)/include
SDK_LIB := $(NDI_SDK)/lib/x86_64-linux-gnu

CXXFLAGS := -O2 -I$(SDK_INC)
LDFLAGS := -L$(SDK_LIB) -lndi -ldl -lpthread -Wl,-rpath,$(abspath $(SDK_LIB))
SDL_FLAGS := $(shell pkg-config --cflags --libs sdl2)

all: ndi_view ndi_grab

ndi_view: src/ndi_view.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS) $(SDL_FLAGS) -lm

ndi_grab: src/ndi_grab.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f ndi_view ndi_grab

.PHONY: all clean
