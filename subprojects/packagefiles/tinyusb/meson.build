project('tinyusb', 'c')

#
# HACK
# This is a workaround for meson not propagating the include directories of the
# main project to the subprojects. To avoid double-inclusion we force the
# include paths needed to compile tinyusb and already present in the main
# program via GCC compile arguments. The target MCU is chosen with the 'mcu'
# option.
#
mcu = get_option('mcu')

if mcu == 'stm32f405'
  add_project_arguments('-I../platform/mcu/CMSIS/Device/ST/STM32F4xx/Include',
                        '-I../platform/mcu/CMSIS/Include',
                        '-DSTM32F405xx',
                        '-DHSE_VALUE=8000000',
                        language : 'c')
elif mcu == 'stm32u575'
  add_project_arguments('-I../platform/mcu/CMSIS/Device/ST/STM32U5xx/Include',
                        '-I../platform/mcu/CMSIS/Core_v5/Include',
                        '-DSTM32U575xx',
                        language : 'c')
endif

add_project_arguments('-w', language : 'c')

tinyusb_inc = include_directories('openrtx', 'src', 'hw')

tinyusb_src = ['src/tusb.c',
               'src/device/usbd.c',
               'src/common/tusb_fifo.c',
               'src/device/usbd_control.c',
               'src/class/cdc/cdc_device.c',
               'src/portable/synopsys/dwc2/dcd_dwc2.c',
               'src/portable/synopsys/dwc2/dwc2_common.c']

tinyusb = static_library('tinyusb',
                         tinyusb_src,
                         include_directories : tinyusb_inc,
                         install : false)

tinyusb_dep = declare_dependency(include_directories : tinyusb_inc,
                                 link_with : tinyusb)
