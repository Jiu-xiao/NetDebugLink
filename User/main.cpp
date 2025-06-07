#include "esp32_nvs_flash_database.hpp"
#include "esp_adc.hpp"
#include "esp_gpio.hpp"
#include "esp_pwm.hpp"
#include "esp_timebase.hpp"
#include "esp_uart.hpp"
#include "esp_usb.hpp"
#include "esp_wifi_client.hpp"
#include "libxr.hpp"
#include "nvs_flash.h"
#include "xrobot_main.hpp"

extern "C" void app_main(void) {
  LibXR::ESP32Timebase timebase;

  LibXR::ESP32NvsFlashDatabase db;

  LibXR::PlatformInit(static_cast<uint32_t>(LibXR::Thread::Priority::REALTIME),
                      16000);

  LibXR::ESP32PWM led_pwm(GPIO_NUM_8, LEDC_CHANNEL_0, LEDC_TIMER_0,
                          LEDC_TIMER_14_BIT);

  led_pwm.SetConfig({.frequency = 8});
  led_pwm.SetDutyCycle(0.1f);

  LibXR::ESP32GPIO button_gpio(GPIO_NUM_9);

  LibXR::ESP32VirtualUART<2048> uart_cdc(20, 10, 2048, 10, 2048);

  LibXR::ESP32UART uart_1(UART_NUM_0, 3, 4, 256, 2048, 20);

  LibXR::ESP32UART uart_2(UART_NUM_1, 5, 6, 256, 2048, 20);

  LibXR::STDIO::write_ = uart_cdc.write_port_;
  LibXR::STDIO::read_ = uart_cdc.read_port_;

  LibXR::ESP32WifiClient wifi;

  LibXR::HardwareContainer hw(
      LibXR::Entry<LibXR::PWM>{.object = led_pwm, .aliases = {"led"}},
      LibXR::Entry<LibXR::GPIO>{.object = button_gpio, .aliases = {"button"}},
      LibXR::Entry<LibXR::UART>{.object = uart_1, .aliases = {"uart1"}},
      LibXR::Entry<LibXR::UART>{.object = uart_2, .aliases = {"uart2"}},
      LibXR::Entry<LibXR::UART>{.object = uart_cdc, .aliases = {"uart_cdc"}},
      LibXR::Entry<LibXR::WifiClient>{.object = wifi,
                                      .aliases = {"wifi_client"}},
      LibXR::Entry<LibXR::Database>{.object = db, .aliases = {"database"}});

  XRobotMain(hw);
}
