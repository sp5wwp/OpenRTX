##
## OpenRTX - Modular Open Source Radio Firmware
##
project('OpenRTX', 'c',
  version : '0.1',
  default_options : ['warning_level=3'])

##
## Here are listed all the platform independent source files
##

## OpenRTX

openrtx_src = ['openrtx/src/bootstrap.c',
               'openrtx/src/state.c',
               'openrtx/src/ui/ui.c',
               'openrtx/src/ui/ui_main.c',
               'openrtx/src/ui/ui_menu.c',
               'openrtx/src/threads.c',
               'openrtx/src/battery.c',
               'openrtx/src/graphics.c',
               'openrtx/src/input.c',
               'openrtx/src/calibUtils.c',
               'openrtx/src/rtx.c',
               'openrtx/src/gps.c']


## Replace main executable with platform test
if get_option('test') != ''
  openrtx_src += 'tests/platform/'+get_option('test')+'.c'
  openrtx_def  = {'RUN_TESTSUITE': ''}
else
  openrtx_src += 'openrtx/src/main.c'
  openrtx_def  = {}
endif

openrtx_inc = ['openrtx/include',
               'openrtx/include/calibration',
               'platform/drivers/ADC',
               'platform/drivers/NVM',
               'platform/drivers/GPS',
               'platform/drivers/tones',
               'openrtx/include/fonts/adafruit',
               'platform/drivers/tones',
               'platform/drivers/baseband']

## RTOS
rtos_src = ['lib/rtos/uC-OS3/Source/__dbg_uCOS-III.c',
            'lib/rtos/uC-OS3/Source/os_cfg_app.c',
            'lib/rtos/uC-OS3/Source/os_core.c',
            'lib/rtos/uC-OS3/Source/os_dbg.c',
            'lib/rtos/uC-OS3/Source/os_flag.c',
            'lib/rtos/uC-OS3/Source/os_mem.c',
            'lib/rtos/uC-OS3/Source/os_msg.c',
            'lib/rtos/uC-OS3/Source/os_mutex.c',
            'lib/rtos/uC-OS3/Source/os_prio.c',
            'lib/rtos/uC-OS3/Source/os_q.c',
            'lib/rtos/uC-OS3/Source/os_sem.c',
            'lib/rtos/uC-OS3/Source/os_stat.c',
            'lib/rtos/uC-OS3/Source/os_task.c',
            'lib/rtos/uC-OS3/Source/os_tick.c',
            'lib/rtos/uC-OS3/Source/os_time.c',
            'lib/rtos/uC-OS3/Source/os_tmr.c',
            'lib/rtos/uC-OS3/Source/os_var.c',
            'lib/rtos/uC-OS3/Cfg/os_app_hooks.c',
            'lib/rtos/uC-CPU/cpu_core.c',
            'lib/rtos/uC-LIB/lib_ascii.c',
            'lib/rtos/uC-LIB/lib_math.c',
            'lib/rtos/uC-LIB/lib_mem.c',
            'lib/rtos/uC-LIB/lib_str.c']

rtos_inc = ['lib/rtos/uC-OS3/Source',
            'lib/rtos/uC-OS3/Cfg',
            'lib/rtos/uC-CPU',
            'lib/rtos/uC-CPU/Cfg',
            'lib/rtos/uC-LIB',
            'lib/rtos/uC-LIB/Cfg']

## minmea, a lightweight GPS NMEA 0183 parser library

minmea_src = ['lib/minmea/minmea.c']

minmea_inc = ['lib/minmea/include']

src = openrtx_src + rtos_src + minmea_src
inc = openrtx_inc + rtos_inc + minmea_inc

##
## Definitions
##
def = openrtx_def + {'DONT_USE_CMSIS_INIT': ''}

##
## MCU-dependent sources and includes
##

## STM32F405

stm32f405_src = ['platform/mcu/STM32F4xx/boot/startup.c',
                 'platform/mcu/STM32F4xx/boot/libc_integration.c',
                 'platform/mcu/STM32F4xx/drivers/usb/usb_bsp.c',
                 'platform/mcu/STM32F4xx/drivers/usb/usb_core.c',
                 'platform/mcu/STM32F4xx/drivers/usb/usb_dcd.c',
                 'platform/mcu/STM32F4xx/drivers/usb/usb_dcd_int.c',
                 'platform/mcu/STM32F4xx/drivers/usb/usbd_desc.c',
                 'platform/mcu/STM32F4xx/drivers/usb/usbd_core.c',
                 'platform/mcu/STM32F4xx/drivers/usb/usbd_ioreq.c',
                 'platform/mcu/STM32F4xx/drivers/usb/usbd_req.c',
                 'platform/mcu/STM32F4xx/drivers/usb/usbd_usr.c',
                 'platform/mcu/STM32F4xx/drivers/gpio.c',
                 'platform/mcu/STM32F4xx/drivers/usb_vcom.c',
                 'platform/mcu/STM32F4xx/drivers/delays.c',
                 'platform/mcu/STM32F4xx/drivers/rtc.c',
                 'platform/mcu/CMSIS/Device/ST/STM32F4xx/Source/system_stm32f4xx.c',
                 'lib/rtos/uC-OS3/Ports/ARM-Cortex-M/ARMv7-M/os_cpu_c.c',
                 'lib/rtos/uC-OS3/Ports/ARM-Cortex-M/ARMv7-M/os_cpu_a.s',
                 'lib/rtos/uC-CPU/ARM-Cortex-M/ARMv7-M/cpu_c.c',
                 'lib/rtos/uC-CPU/ARM-Cortex-M/ARMv7-M/cpu_a.s']

stm32f405_inc = ['platform/mcu/CMSIS/Include',
                 'platform/mcu/CMSIS/Device/ST/STM32F4xx/Include',
                 'platform/mcu/STM32F4xx',
                 'platform/mcu/STM32F4xx/drivers',
                 'platform/mcu/STM32F4xx/drivers/usb',
                 'lib/rtos/uC-OS3/Ports/ARM-Cortex-M/ARMv7-M',
                 'lib/rtos/uC-CPU/ARM-Cortex-M/ARMv7-M']

stm32f405_def = {'STM32F40_41xxx': '', 'HSE_VALUE':'8000000'}

## MK22FN512

mk22fn512_src = ['platform/mcu/MK22FN512xxx12/boot/startup.c',
                 'platform/mcu/MK22FN512xxx12/boot/libc_integration.c',
                 'platform/mcu/MK22FN512xxx12/drivers/gpio.c',
                 'platform/mcu/MK22FN512xxx12/drivers/delays.c',
                 'platform/mcu/MK22FN512xxx12/drivers/rtc.c',
                 'platform/mcu/MK22FN512xxx12/drivers/I2C0.c',
                 'platform/mcu/MK22FN512xxx12/drivers/usb/usb_device_cdc_acm.c',
                 'platform/mcu/MK22FN512xxx12/drivers/usb/usb_device_ch9.c',
                 'platform/mcu/MK22FN512xxx12/drivers/usb/usb_device_dci.c',
                 'platform/mcu/MK22FN512xxx12/drivers/usb/usb_device_descriptor.c',
                 'platform/mcu/MK22FN512xxx12/drivers/usb/usb_device_khci.c',
                 'platform/mcu/MK22FN512xxx12/drivers/usb/usb_osa_bm.c',
                 'platform/mcu/MK22FN512xxx12/drivers/usb_vcom.c',
                 'platform/mcu/CMSIS/Device/NXP/MK22FN512xxx12/Source/system_MK22F51212.c',
                 'lib/rtos/uC-OS3/Ports/ARM-Cortex-M/ARMv7-M/os_cpu_c.c',
                 'lib/rtos/uC-OS3/Ports/ARM-Cortex-M/ARMv7-M/os_cpu_a.s',
                 'lib/rtos/uC-CPU/ARM-Cortex-M/ARMv7-M/cpu_c.c',
                 'lib/rtos/uC-CPU/ARM-Cortex-M/ARMv7-M/cpu_a.s']

mk22fn512_inc = ['platform/mcu/CMSIS/Include',
                 'platform/mcu/CMSIS/Device/NXP/MK22FN512xxx12/Include',
                 'platform/mcu/MK22FN512xxx12/drivers',
                 'lib/rtos/uC-OS3/Ports/ARM-Cortex-M/ARMv7-M',
                 'lib/rtos/uC-CPU/ARM-Cortex-M/ARMv7-M']

mk22fn512_def = {}

##
## Platform specializations
##

## Linux
linux_src = src + ['platform/targets/linux/emulator/emulator.c',
                   'platform/drivers/display/display_libSDL.c',
                   'platform/drivers/keyboard/keyboard_linux.c',
                   'platform/drivers/NVM/nvmem_linux.c',
                   'platform/drivers/GPS/GPS_linux.c',
                   'platform/mcu/x86_64/drivers/gpio.c',
                   'platform/mcu/x86_64/drivers/delays.c',
                   'platform/mcu/x86_64/drivers/rtc.c',
                   'platform/drivers/baseband/radio_linux.c',
                   'platform/targets/linux/platform.c',
                   'lib/rtos/uC-OS3/Ports/POSIX/os_cpu_c.c',
                   'lib/rtos/uC-CPU/Posix/cpu_c.c']


# GDx family display emulation
#linux_def = def + {'SCREEN_WIDTH': '128', 'SCREEN_HEIGHT': '64', 'PIX_FMT_BW': ''}
# MDx family display emulation
linux_def = def + {'SCREEN_WIDTH': '160', 'SCREEN_HEIGHT': '128', 'PIX_FMT_RGB565': ''}

linux_inc = inc + ['lib/rtos/uC-OS3/Ports/POSIX',
                   'lib/rtos/uC-CPU/Posix',
                   'platform/targets/linux',
                   'platform/targets/linux/emulator']

if not meson.is_cross_build()
  sdl_dep = dependency('SDL2')
  threads_dep = dependency('threads')
  linux_dep = [sdl_dep, threads_dep]
else
  linux_dep = []
endif

## TYT MD-3x0 family
md3x0_src = src + stm32f405_src + ['platform/drivers/display/HX8353_MDx.c',
                                   'platform/drivers/keyboard/keyboard_MDx.c',
                                   'platform/drivers/NVM/W25Qx.c',
                                   'platform/drivers/NVM/spiFlash_MDx.c',
                                   'platform/drivers/NVM/nvmem_MD3x0.c',
                                   'platform/drivers/ADC/ADC1_MDx.c',
                                   'platform/drivers/tones/toneGenerator_MDx.c',
                                   'platform/drivers/baseband/SKY72310.c',
                                   'platform/drivers/baseband/radio_MD3x0.c',
                                   'platform/drivers/baseband/HR_C5000.c',
                                   'platform/drivers/GPS/GPS_MDx.c',
                                   'platform/targets/MD-3x0/platform.c']

md3x0_inc = inc + stm32f405_inc + ['platform/targets/MD-3x0']
md3x0_def = def + stm32f405_def + {'PLATFORM_MD3x0': '',
                                   'timegm': 'mktime'}

## TYT MD-UV380
mduv380_src = src + stm32f405_src + ['platform/drivers/display/HX8353_MDx.c',
                                     'platform/drivers/keyboard/keyboard_MDx.c',
                                     'platform/drivers/NVM/W25Qx.c',
                                     'platform/drivers/NVM/spiFlash_MDx.c',
                                     'platform/drivers/NVM/nvmem_MDUV3x0.c',
                                     'platform/drivers/ADC/ADC1_MDx.c',
                                     'platform/drivers/tones/toneGenerator_MDx.c',
                                     'platform/drivers/baseband/radio_UV3x0.c',
                                     'platform/drivers/GPS/GPS_MDx.c',
                                     'platform/targets/MD-UV380/platform.c']

mduv380_inc = inc + stm32f405_inc + ['platform/targets/MD-UV380']
mduv380_def = def + stm32f405_def + {'PLATFORM_MDUV380': ''}


## TYT MD-9600
md9600_src = src + stm32f405_src + ['platform/targets/MD-9600/platform.c',
                                    'platform/drivers/tones/toneGenerator_MDx.c']

md9600_inc = inc + stm32f405_inc + ['platform/targets/MD-9600']
md9600_def = def + stm32f405_def + {'PLATFORM_MD9600': ''}


## Radioddity GD-77
gd77_src = src + mk22fn512_src + ['platform/targets/GD-77/platform.c',
                                  'platform/drivers/display/UC1701_GD77.c',
                                  'platform/drivers/keyboard/keyboard_GD77.c',
                                  'platform/drivers/NVM/W25Qx.c',
                                  'platform/drivers/NVM/AT24Cx_GDx.c',
                                  'platform/drivers/NVM/spiFlash_GDx.c',
                                  'platform/drivers/NVM/nvmem_GDx.c',
                                  'platform/drivers/ADC/ADC0_GDx.c',
                                  'platform/drivers/baseband/radio_GDx.c',
                                  'platform/drivers/baseband/AT1846S.c',
                                  'platform/drivers/baseband/HR_C6000.c',
                                  'platform/drivers/baseband/interfaces_GDx.c']

gd77_inc = inc + mk22fn512_inc + ['platform/targets/GD-77']
gd77_def = def + mk22fn512_def + {'PLATFORM_GD77': ''}


## Baofeng DM-1801
dm1801_src = src + mk22fn512_src + ['platform/targets/DM-1801/platform.c',
                                    'platform/drivers/display/UC1701_GD77.c',
                                    'platform/drivers/keyboard/keyboard_GD77.c',
                                    'platform/drivers/NVM/W25Qx.c',
                                    'platform/drivers/NVM/AT24Cx_GDx.c',
                                    'platform/drivers/NVM/spiFlash_GDx.c',
                                    'platform/drivers/NVM/nvmem_GDx.c',
                                    'platform/drivers/ADC/ADC0_GDx.c',
                                    'platform/drivers/baseband/radio_GDx.c',
                                    'platform/drivers/baseband/AT1846S.c',
                                    'platform/drivers/baseband/HR_C6000.c',
                                    'platform/drivers/baseband/interfaces_GDx.c']

dm1801_inc = inc + mk22fn512_inc + ['platform/targets/DM-1801']
dm1801_def = def + mk22fn512_def + {'PLATFORM_DM1801': ''}

##
## Compilation defines
##
linux_c_args = []
linux_l_args = ['--entry=entry', '-lm']

# Add AddressSanitizer if required
if get_option('asan')
  linux_c_args += '-fsanitize=address'
  linux_l_args += '-fsanitize=address'
endif
# Add Undefined Behaviour Sanitizer if required
if get_option('ubsan')
  linux_c_args += '-fsanitize=undefined'
  linux_l_args += '-fsanitize=undefined'
endif

foreach k, v : linux_def
  if v == ''
    linux_c_args += '-D@0@'.format(k)
  else
    linux_c_args += '-D@0@=@1@'.format(k, v)
  endif
endforeach

md3x0_args = []
foreach k, v : md3x0_def
  if v == ''
    md3x0_args += '-D@0@'.format(k)
  else
    md3x0_args += '-D@0@=@1@'.format(k, v)
  endif
endforeach

mduv380_args = []
foreach k, v : mduv380_def
  if v == ''
    mduv380_args += '-D@0@'.format(k)
  else
    mduv380_args += '-D@0@=@1@'.format(k, v)
  endif
endforeach

mduv380g_args = []
foreach k, v : mduv380_def
  if v == ''
    mduv380g_args += '-D@0@'.format(k)
  else
    mduv380g_args += '-D@0@=@1@'.format(k, v)
  endif
endforeach

gd77_args = []
foreach k, v : gd77_def
  if v == ''
    gd77_args += '-D@0@'.format(k)
  else
    gd77_args += '-D@0@=@1@'.format(k, v)
  endif
endforeach

dm1801_args = []
foreach k, v : dm1801_def
  if v == ''
    dm1801_args += '-D@0@'.format(k)
  else
    dm1801_args += '-D@0@=@1@'.format(k, v)
  endif
endforeach

md9600_args = []
foreach k, v : md9600_def
  if v == ''
    md9600_args += '-D@0@'.format(k)
  else
    md9600_args += '-D@0@=@1@'.format(k, v)
  endif
endforeach

linux_opts = {'sources': linux_src,
              'c_args': linux_c_args,
              'include_directories': linux_inc,
              'dependencies': linux_dep,
              'link_args' : linux_l_args}

md3x0_opts = {'sources': md3x0_src,
              'c_args': md3x0_args,
              'link_args' : ['-Wl,-T../platform/mcu/STM32F4xx/linker_script.ld',
                             '-Wl,--print-memory-usage'],
              'include_directories': md3x0_inc}

mduv380_opts = {'sources': mduv380_src,
                'c_args': mduv380_args,
                'link_args' : ['-Wl,-T../platform/mcu/STM32F4xx/linker_script.ld',
                               '-Wl,--print-memory-usage'],
                'include_directories': mduv380_inc}

mduv380g_opts = {'sources': mduv380_src,
                 'c_args': mduv380g_args,
                 'link_args' : ['-Wl,-T../platform/mcu/STM32F4xx/linker_script.ld',
                                '-Wl,--print-memory-usage'],
                 'include_directories': mduv380_inc}

gd77_opts = {'sources': gd77_src,
             'c_args': gd77_args,
             'link_args' : ['-Wl,-T../platform/mcu/MK22FN512xxx12/linker_script.ld',
                            '-Wl,--print-memory-usage'],
             'include_directories':gd77_inc}

dm1801_opts = {'sources': dm1801_src,
               'c_args': dm1801_args,
               'link_args' : ['-Wl,-T../platform/mcu/MK22FN512xxx12/linker_script.ld',
                              '-Wl,--print-memory-usage'],
               'include_directories':dm1801_inc}

md9600_opts = {'sources': md9600_src,
               'c_args': md9600_args,
               'link_args' : ['-Wl,-T../platform/mcu/STM32F4xx/linker_script.ld',
                              '-Wl,--print-memory-usage'],
               'include_directories': md9600_inc}

##
## Targets
##
targets = [

  {'name': 'linux',
   'opts': linux_opts,
   'flashable': false},

  {'name': 'md3x0',
   'opts': md3x0_opts,
   'flashable': true,
   'wrap': 'MD380',
   'load_addr': '0x0800C000'},

  {'name': 'mduv380',
   'opts': mduv380_opts,
   'flashable': true,
   'wrap': 'UV3X0',
   'load_addr': '0x0800C000'},

  {'name': 'mduv380g',
   'opts': mduv380g_opts,
   'flashable': true,
   'wrap': 'UV3X0',
   'load_addr': '0x0800C000'},

   {'name': 'gd77',
   'opts': gd77_opts,
   'flashable': true,
   'wrap': 'UV3X0',
   'load_addr': '0x0800C000'},

   {'name': 'dm1801',
   'opts': dm1801_opts,
   'flashable': true,
   'wrap': 'UV3X0',
   'load_addr': '0x0800C000'},

  {'name': 'md9600',
   'opts': md9600_opts,
   'flashable': true,
   'wrap': 'MD9600',
   'load_addr': '0x0800C000'},
  ]

objcopy = find_program('objcopy', required:false, disabler:true)
radio_tool = find_program('radio_tool', required:false, disabler:true)
bin2sgl = find_program('bin2sgl.Linux', required:false, disabler:true)
gd77_loader = find_program('gd-77_firmware_loader.py', required:false, disabler:true)

foreach t : targets

  name = 'openrtx_'+t['name']
  exe = executable(name, kwargs:t['opts'])

  if t['flashable']

    bin = custom_target(name+'_bin',
      output : name+'_bin',
      input : exe,
      command : [objcopy, '-O', 'binary', '@INPUT@', '@OUTPUT@'])

    # Handle GD77 custom wrapping and flashing tools
    if name == 'openrtx_gd77'

      wrap = custom_target(name+'_wrap',
        output : name+'_wrap',
        input : bin,
        command : [bin2sgl,
                   '-f', '@INPUT@',
                   '&&', 'mv', '@INPUT@.sgl', '@OUTPUT@.sgl'])

      custom_target(name+'_flash',
        input : wrap,
        output : name+'_flash',
        command : [gd77_loader, '-f', '@INPUT@.sgl'])

    else

      wrap = custom_target(name+'_wrap',
        output : name+'_wrap',
        input : bin,
        command : [radio_tool,
                   '--wrap',
                   '-o', '@OUTPUT@',
                   '-r', t['wrap'],
                   '-s', t['load_addr']+':@INPUT@'])

      custom_target(name+'_flash',
        input : wrap,
        output : name+'_flash',
        command : [radio_tool, '-d', '0', '-f', '-i', '@INPUT@'])

    endif

  endif
endforeach
