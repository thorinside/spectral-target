PLUGIN_NAME := spectral_target
SOURCES := src/plugin.cpp src/spectral_target.cpp
TEST_SOURCES := tests/spectral_target_tests.cpp src/spectral_target.cpp

DISTINGNT_API_DIR ?= distingNT_API
USE_CMSIS ?= 0
CMSIS_DSP_DIR ?= /Users/nealsanche/nosuch/archive/nt_enosc/enosc/lib/CMSIS
CMSIS_CORE_DIR ?=
CMSIS_DSP_LIB ?=
API_HEADER := $(DISTINGNT_API_DIR)/include/distingnt/api.h

UNAME_S := $(shell uname -s)
BUILD_DIR := build
PLUGIN_DIR := plugins

COMMON_CXXFLAGS := -std=c++11 -Wall -Wextra -Wno-missing-field-initializers -fno-rtti -fno-exceptions -Iinclude
# Match the hardware flags used by the official distingNT_API examples.
HARDWARE_CXXFLAGS := -std=c++11 -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -fno-rtti -fno-exceptions -Os -fPIC -Wall
API_CXXFLAGS := -I$(DISTINGNT_API_DIR)/include
CMSIS_CXXFLAGS := -DSPECTRAL_TARGET_USE_CMSIS=1 -DARM_MATH_CM7 -D__FPU_PRESENT=1 $(foreach d,$(CMSIS_DSP_DIR) $(CMSIS_CORE_DIR),-I$(d) -I$(d)/Include)
DSP_CXXFLAGS := -DSPECTRAL_TARGET_FORCE_FALLBACK=1
ifeq ($(USE_CMSIS),1)
    DSP_CXXFLAGS := $(CMSIS_CXXFLAGS)
endif

ifeq ($(UNAME_S),Darwin)
    SHARED_LDFLAGS := -dynamiclib -undefined dynamic_lookup
    SHARED_EXT := dylib
else
    SHARED_LDFLAGS := -shared
    SHARED_EXT := so
endif

all: hardware

hardware: CXX := arm-none-eabi-c++
hardware: CXXFLAGS := $(HARDWARE_CXXFLAGS) -Iinclude $(API_CXXFLAGS) $(DSP_CXXFLAGS)
hardware: LDFLAGS := -Wl,--relocatable -nostdlib
hardware: $(PLUGIN_DIR)/$(PLUGIN_NAME).o

submodules deps:
	git submodule update --init --recursive

$(API_HEADER):
	@echo "Missing disting NT API headers at $(API_HEADER)."
	@echo "Run: git submodule update --init --recursive"
	@exit 1

$(PLUGIN_DIR)/$(PLUGIN_NAME).o: $(patsubst src/%.cpp,$(BUILD_DIR)/hardware/%.o,$(SOURCES)) | $(PLUGIN_DIR)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(CMSIS_DSP_LIB)
	@echo Built $@

$(BUILD_DIR)/hardware/%.o: src/%.cpp $(API_HEADER) | $(BUILD_DIR)/hardware
	$(CXX) $(CXXFLAGS) -c -o $@ $<

emu: CXX ?= clang++
emu: CXXFLAGS := $(COMMON_CXXFLAGS) $(API_CXXFLAGS) -DSPECTRAL_TARGET_FORCE_FALLBACK=1 -fPIC -Os
emu: $(PLUGIN_DIR)/$(PLUGIN_NAME).$(SHARED_EXT)

$(PLUGIN_DIR)/$(PLUGIN_NAME).$(SHARED_EXT): $(SOURCES) $(API_HEADER) | $(PLUGIN_DIR)
	$(CXX) $(CXXFLAGS) $(SHARED_LDFLAGS) -o $@ $(SOURCES)
	@echo Built $@

test: $(BUILD_DIR)/spectral_target_tests
	./$(BUILD_DIR)/spectral_target_tests

$(BUILD_DIR)/spectral_target_tests: $(TEST_SOURCES) | $(BUILD_DIR)
	$(CXX) $(COMMON_CXXFLAGS) -DSPECTRAL_TARGET_FORCE_FALLBACK=1 -O0 -g -o $@ $^ -lm

check: hardware
	arm-none-eabi-nm $(PLUGIN_DIR)/$(PLUGIN_NAME).o | grep ' U ' || true

size: hardware
	arm-none-eabi-size -A $(PLUGIN_DIR)/$(PLUGIN_NAME).o

$(BUILD_DIR) $(BUILD_DIR)/hardware $(PLUGIN_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR) $(PLUGIN_DIR)

.PHONY: all hardware emu test check size clean submodules deps
