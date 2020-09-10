/******************************************************************************
 * Copyright 2020 The Firmament Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/
#include <firmament.h>

#include <bsp.h>
#include <shell.h>
#include <string.h>
#ifdef FMT_USING_CM_BACKTRACE
#include <cm_backtrace.h>
#endif

#include "driver/gpio.h"
#include "driver/gps.h"
#include "driver/l3gd20h.h"
#include "driver/lsm303d.h"
#include "driver/mpu6000.h"
#include "driver/ms5611.h"
#include "driver/pwm_drv.h"
#include "driver/spi_drv.h"
#include "driver/systick_drv.h"
#include "driver/tca62724.h"
#include "driver/usart.h"
#include "hal/cdcacm.h"
#include "hal/fmtio_dev.h"
#include "module/controller/controller_model.h"
#include "module/fms/fms_model.h"
#include "module/fs_manager/fs_manager.h"
#include "module/ins/ins_model.h"
#include "module/param/param.h"
#include "module/sensor/sensor_manager.h"
#include "module/sysio/actuator_cmd.h"
#include "module/sysio/pilot_cmd.h"
#include "module/system/statistic.h"
#include "module/system/systime.h"

static void _print_item(const char* name, const char* content, uint32_t len)
{
    int pad_len = len - strlen(name) - strlen(content);

    if (pad_len < 0) {
        pad_len = 0;
    }

    console_printf("%s", name);

    while (pad_len--)
        console_write(".", 1);

    console_printf("%s\n", content);
}

void NVIC_Configuration(void)
{
#ifdef VECT_TAB_RAM
    /* Set the Vector Table base location at 0x20000000 */
    NVIC_SetVectorTable(NVIC_VectTab_RAM, INT_VECTOR_OFFSET);
#else /* VECT_TAB_FLASH  */
    /* Set the Vector Table base location at 0x8004000 */
    /* first 0x4000 is reserved for bootloader, so the vectortab offset is 0x4000 */
    NVIC_SetVectorTable(NVIC_VectTab_FLASH, INT_VECTOR_OFFSET);
#endif

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
}

void bsp_show_version(void)
{
    char buffer[50];

    console_printf("\n");
    console_println("   _____                               __ ");
    console_println("  / __(_)_____ _  ___ ___ _  ___ ___  / /_");
    console_println(" / _// / __/  ' \\/ _ `/  ' \\/ -_) _ \\/ __/");
    console_println("/_/ /_/_/ /_/_/_/\\_,_/_/_/_/\\__/_//_/\\__/ ");

    sprintf(buffer, "FMT v%d.%d.%d", FMT_VERSION, FMT_SUBVERSION, FMT_REVISION);
    _print_item("Firmware", buffer, 42);

    sprintf(buffer, "RT-Thread v%ld.%ld.%ld", RT_VERSION, RT_SUBVERSION, RT_REVISION);
    //console_println("RTOS: RT-Thread v%d.%d.%d", RT_VERSION, RT_SUBVERSION, RT_REVISION);
    _print_item("Kernel", buffer, 42);

    sprintf(buffer, "%d KB", SYSTEM_TOTAL_MEM_SIZE / 1024);
    //console_println("RAM: %d KB", SYSTEM_TOTAL_MEM_SIZE / 1024);
    _print_item("RAM", buffer, 42);

    _print_item("Board", BOARD_NAME, 42);
    //console_println("Board: %s", BOARD_NAME);

    _print_item("Vehicle", VEHICLE_TYPE, 42);
    //console_println("Vehicle Type: %s", VEHICLE_TYPE);

    _print_item("INS Model", (char*)INS_EXPORT.model_info, 42);
    //console_println("INS Model: %s", INS_EXPORT.model_info);

    _print_item("FMS Model", (char*)FMS_EXPORT.model_info, 42);
    //console_println("FMS Model: %s", FMS_EXPORT.model_info);

    _print_item("Control Model", (char*)CONTROL_EXPORT.model_info, 42);
    //console_println("Control Model: %s", CONTROL_EXPORT.model_info);


    console_println("Task Initialize:");
    //console_println("  vehicle: OK");
    _print_item("  vehicle", "OK", 42);
    //console_println("    fmtio: OK");
    _print_item("  fmtio", "OK", 42);
    //console_println("     comm: OK");
    _print_item("  comm", "OK", 42);
    //console_println("   logger: OK");
    _print_item("  logger", "OK", 42);
    //console_println("   status: OK");
    _print_item(" status", "OK", 42);
}

/* this function will be called before rtos start, which is not thread context */
void bsp_early_initialize(void)
{
    /* interrupt controller init */
    NVIC_Configuration();

    /* system timer init */
    RTT_CHECK(systick_drv_init());

    /* system usart init */
    RTT_CHECK(usart_drv_init());

    /* system time module init */
    FMT_CHECK(systime_init());

    /* init console to enable console output */
    FMT_CHECK(console_init(CONSOLE_DEVICE_NAME));

    /* init gpio, bus, etc. */
    RTT_CHECK(gpio_drv_init());

    RTT_CHECK(spi_drv_init());

    RTT_CHECK(pwm_drv_init());
}

/* this function will be called after rtos start, which is in thread context */
void bsp_initialize(void)
{
    /* init boot log */
    FMT_CHECK(boot_log_init());

    /* init uMCN */
    FMT_CHECK(mcn_init());

    /* init file manager */
    FMT_CHECK(fs_manager_init(FS_DEVICE_NAME, "/"));

    /* init usb device */
    FMT_CHECK(usb_cdc_init());

    /* init imu0 */
    RTT_CHECK(mpu6000_drv_init(MPU6000_SPI_DEVICE_NAME));

    /* init imu1 + mag0 */
    RTT_CHECK(l3gd20h_drv_init(L3GD20H_SPI_DEVICE_NAME));
    RTT_CHECK(lsm303d_drv_init(LSM303D_SPI_DEVICE_NAME));

    /* init barometer */
    RTT_CHECK(ms5611_drv_init(MS5611_SPI_DEVICE_NAME));

    /* init gps */
    RTT_CHECK(drv_gps_init(GPS_SERIAL_DEVICE_NAME));

    /* init other device */
    RTT_CHECK(tca62724_drv_init());

    /* init parameter system */
    FMT_CHECK(param_init());

    /* init sensor manager */
    FMT_CHECK(sensor_manager_init());

    //     /* GDB STUB */
    // #ifdef RT_USING_GDB
    //     gdb_set_device(GDB_DEVICE_NAME);
    //     gdb_start();
    // #endif

#ifdef RT_USING_FINSH
    /* init finsh */
    finsh_system_init();
    /* Mount finsh to console after finsh system init */
    FMT_CHECK(console_mount_shell(NULL));
#endif

    /* system statistic module */
    FMT_CHECK(sys_stat_init());

#ifdef FMT_USING_CM_BACKTRACE
    // cortex-m backtrace
    cm_backtrace_init("fmt_fmu", BOARD_NAME, "v0.1");
#endif
}

void bsp_post_initialize(void)
{
    FMT_CHECK(pilot_cmd_init());

#if defined(FMT_HIL_WITH_ACTUATOR) || !defined(FMT_USING_HIL)
#ifdef FMT_USING_AUX_MOTOR
    FMT_CHECK(actuator_init("motor_aux"));
#else
    FMT_CHECK(actuator_init("motor_main"));
#endif
#endif

    bsp_show_version();

    /* dump boot log to file */
    boot_log_dump();
}

/**
 * This function will initial STM32 board.
 */
void rt_hw_board_init()
{
    bsp_early_initialize();
}
