# STM32L031K6 build dependencies

- `hal/` is the subset of [STM32L0 HAL v1.10.6](https://github.com/STMicroelectronics/stm32l0xx-hal-driver/tree/v1.10.6) used by this firmware.
- `device/` contains the STM32L031 headers, startup and system files from [CMSIS Device L0 v1.9.4](https://github.com/STMicroelectronics/cmsis-device-l0/tree/v1.9.4).
- `core/` contains the Cortex-M0+ headers from Arm CMSIS Core as included in STM32Cube.

The upstream licenses are included in the respective directories. The files are bundled so the ARM builds can run offline.
