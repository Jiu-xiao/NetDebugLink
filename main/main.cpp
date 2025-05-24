/* Blink Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "libxr.hpp"
#include "esp_timebase.hpp"
#include "esp_gpio.hpp"
#include "esp_usb.hpp"
#include "esp_wifi_client.hpp"
#include "nvs_flash.h"
#include "esp_pwm.hpp"
#include "esp_adc.hpp"
#include "esp_uart.hpp"

extern "C" void app_main(void)
{
    nvs_flash_init();

    LibXR::ESP32Timebase timebase;
    LibXR::PlatformInit(static_cast<uint32_t>(LibXR::Thread::Priority::MEDIUM), 8192);

    LibXR::ESP32VirtualUART<1024> uart_cdc(20, 20, 10, 4096, 10, 4096);

    LibXR::ESP32UART uart(UART_NUM_1, 3, 4);

    LibXR::Semaphore write_sem;

    LibXR::WriteOperation write_op(write_sem);

    LibXR::STDIO::write_ = uart_cdc.write_port_;
    LibXR::STDIO::read_ = uart_cdc.read_port_;

    LibXR::RamFS ramfs;

    LibXR::Terminal terminal(ramfs);
    LibXR::Terminal terminal_1(ramfs, nullptr, uart.read_port_, uart.write_port_);

    LibXR::Thread term_thread, term_thread1;
    term_thread.Create(&terminal, LibXR::Terminal<>::ThreadFun, "terminal", 4096,
                       LibXR::Thread::Priority::MEDIUM);

    term_thread1.Create(&terminal_1, LibXR::Terminal<>::ThreadFun, "ttttt", 4096,
                        LibXR::Thread::Priority::MEDIUM);

    LibXR::ESP32WifiClient wifi;
    while (1)
    {

        LibXR::Thread::Sleep(10);
    }
}
