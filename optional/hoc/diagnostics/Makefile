DEVKITPRO ?= /opt/devkitpro
include $(DEVKITPRO)/libnx/switch_rules
TARGET := nexus-hoc-status
BUILD := build
APP_TITLE := Nexus HOC Status HS2
APP_AUTHOR := TechRepairs4U
APP_VERSION := 0.2.0
ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CXXFLAGS := $(ARCH) -O2 -g -std=gnu++17 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -fno-rtti -fno-exceptions -I../common -isystem $(LIBNX)/include -D__SWITCH__
LDFLAGS := $(ARCH) -specs=$(LIBNX)/switch.specs -Wl,--gc-sections,-Map,$(BUILD)/$(TARGET).map -L$(LIBNX)/lib
SOURCES := $(wildcard source/*.cpp)
OBJECTS := $(patsubst source/%.cpp,$(BUILD)/%.o,$(SOURCES))
.PHONY: all
all: $(TARGET).nro
$(BUILD):
	mkdir -p $(BUILD)
$(BUILD)/%.o: source/%.cpp $(wildcard ../common/*.hpp) | $(BUILD)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(BUILD)/$(TARGET).elf: $(OBJECTS)
	$(CXX) $(LDFLAGS) $(OBJECTS) -lnx -lm -o $@
$(BUILD)/$(TARGET).nacp: Makefile | $(BUILD)
	nacptool --create "$(APP_TITLE)" "$(APP_AUTHOR)" "$(APP_VERSION)" $@
$(TARGET).nro: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).nacp
	elf2nro $< $@ --nacp=$(BUILD)/$(TARGET).nacp --icon=$(LIBNX)/default_icon.jpg
-include $(OBJECTS:.o=.d)
