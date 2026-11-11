CC = cc
ARM_CC ?= arm-none-eabi-gcc
ARM_OBJCOPY ?= arm-none-eabi-objcopy
ARM_SIZE ?= arm-none-eabi-size

ROLES := controller actuator
COMMON := common/wire.c
HEADERS := common/app.h common/wire.h
BUILD_FLAGS := -std=c11 -Wall -Wextra -Werror -Icommon
NATIVE_BINS := $(addprefix build/native/,$(ROLES))
TEST_BINS := $(addprefix build/tests/,$(ROLES))
FIRMWARE := $(addsuffix /build/greenhouse.hex,$(ROLES))

HAL := vendor/hal
DEVICE := vendor/device
CORE := vendor/core
ARM_CPU := -mcpu=cortex-m0plus -mthumb
ARM_FLAGS := $(ARM_CPU) -Wno-unused-parameter -DSTM32L031xx -DUSE_HAL_DRIVER -I$(HAL)/Inc -I$(DEVICE)/Include -I$(CORE)/Include -Os -g3 -ffunction-sections -fdata-sections
ARM_LINK := --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections -Wl,--build-id=none -T common/linker.ld
HAL_SOURCES := $(addprefix $(HAL)/Src/stm32l0xx_hal,$(addsuffix .c, _cortex _gpio _rcc _rcc_ex _tim _tim_ex _uart _uart_ex _dma)) $(HAL)/Src/stm32l0xx_hal.c
DEVICE_SOURCES := $(DEVICE)/Source/system_stm32l0xx.c $(DEVICE)/Source/startup_stm32l031xx.s

.PHONY: all native test clean
all: $(FIRMWARE)
native: $(NATIVE_BINS)
test: $(TEST_BINS)
	./build/tests/controller
	./build/tests/actuator

build/native/%: %/app.c common/native.c $(COMMON) $(HEADERS)
	mkdir -p $(@D)
	$(CC) $(BUILD_FLAGS) -O2 $(filter %.c,$^) -o $@

build/tests/controller: TEST_ROLE = -DCONTROLLER
build/tests/%: %/app.c tests/test_core.c $(COMMON) $(HEADERS)
	mkdir -p $(@D)
	$(CC) $(BUILD_FLAGS) $(TEST_ROLE) $(filter %.c,$^) -o $@

controller/build/greenhouse.elf: ROLE = -DCONTROLLER
actuator/build/greenhouse.elf: ROLE = -DACTUATOR
.PRECIOUS: %/build/greenhouse.elf
%/build/greenhouse.elf: %/app.c common/stm32.c common/stm32l0xx_hal_conf.h common/linker.ld $(COMMON) $(HEADERS) $(HAL_SOURCES) $(DEVICE_SOURCES)
	mkdir -p $(@D)
	$(ARM_CC) $(BUILD_FLAGS) $(ARM_FLAGS) $(ROLE) $(filter %.c %.s,$^) $(ARM_LINK) -o $@
	$(ARM_SIZE) $@

%/build/greenhouse.hex: %/build/greenhouse.elf
	$(ARM_OBJCOPY) -O ihex $< $@

clean:
	rm -rf build $(addsuffix /build,$(ROLES))
