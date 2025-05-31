#include "NetDebugLink.hpp"

#include "esp_blufi.h"
#include "esp_blufi_api.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_smartconfig.h"
#include "esp_system.h"
#include "esp_wifi.h"
extern "C" {
#include "blufi_user.h"
}

EventGroupHandle_t s_wifi_event_group;

void NetDebugLink::BlufiInit() { s_wifi_event_group = xEventGroupCreate(); }

void NetDebugLink::PeripheralInit() {
  void (*led_task_fun)(NetDebugLink *) = [](NetDebugLink *self) {
    static Mode mode = Mode::Init;
    if (mode == self->mode_) {
      return;
    }

    static const char *modes[] = {"Init", "SMART_CONFIG", "SCANING",
                                  "CONNECTED"};
    XR_LOG_DEBUG("Mode: %s", modes[(int)self->mode_]);
    switch (self->mode_) {
      case Mode::Init:
      case Mode::SMART_CONFIG:
        self->led_->SetConfig({.frequency = 10});
        self->led_->SetDutyCycle(0.5f);
        break;
      case Mode::SCANING:
        self->led_->SetConfig({.frequency = 4});
        self->led_->SetDutyCycle(0.75f);
        break;
      case Mode::CONNECTED:
        self->led_->SetConfig({.frequency = 2});
        self->led_->SetDutyCycle(0.25f);
        break;
    }
    mode = self->mode_;
  };

  auto led_task = LibXR::Timer::CreateTask(led_task_fun, this, 50);
  LibXR::Timer::Add(led_task);
  LibXR::Timer::Start(led_task);

  void (*cb_fun)(bool, NetDebugLink *) = [](bool in_isr, NetDebugLink *self) {
    self->OnButton();
  };

  auto cb = LibXR::GPIO::Callback::Create(cb_fun, this);
  button_->RegisterCallback(cb);

  button_->SetConfig({.direction = LibXR::GPIO::Direction::FALL_INTERRUPT,
                      .pull = LibXR::GPIO::Pull::UP});

  button_->EnableInterrupt();

  wifi_->Enable();

  auto mac = wifi_->GetMACAddress();
  LibXR::MACAddressStr mac_str = LibXR::MACAddressStr::FromRaw(mac);
  XR_LOG_INFO("MAC address: %s", mac_str);

  if (!wifi_->IsConnected()) {
    smartconfig_requested_ = true;
  } else {
    mode_ = Mode::SCANING;
  }
}

void BlufiEventCallback(esp_blufi_cb_event_t event,
                        esp_blufi_cb_param_t *param) {
  auto *self = NetDebugLink::instance_;

  switch (event) {
    case ESP_BLUFI_EVENT_INIT_FINISH:
      XR_LOG_INFO("BLUFI init finish");
      esp_blufi_adv_start();
      break;

    case ESP_BLUFI_EVENT_BLE_CONNECT:
      XR_LOG_INFO("BLE connected");
      esp_blufi_adv_stop();
      blufi_security_init();
      break;

    case ESP_BLUFI_EVENT_BLE_DISCONNECT:
      XR_LOG_INFO("BLE disconnected");
      blufi_security_deinit();
      esp_blufi_adv_start();
      break;

    case ESP_BLUFI_EVENT_RECV_STA_SSID:
      strncpy((char *)self->sta_cfg_.ssid, (const char *)param->sta_ssid.ssid,
              param->sta_ssid.ssid_len);
      self->sta_cfg_.ssid[param->sta_ssid.ssid_len] = '\0';
      break;

    case ESP_BLUFI_EVENT_RECV_STA_PASSWD:
      strncpy((char *)self->sta_cfg_.password,
              (const char *)param->sta_passwd.passwd,
              param->sta_passwd.passwd_len);
      self->sta_cfg_.password[param->sta_passwd.passwd_len] = '\0';
      break;

    case ESP_BLUFI_EVENT_REQ_CONNECT_TO_AP: {
      XR_LOG_INFO("BLUFI requests connect to AP");
      xEventGroupSetBits(s_wifi_event_group, NetDebugLink::GOT_CREDENTIAL_BIT);
      break;
    }

    case ESP_BLUFI_EVENT_GET_WIFI_LIST: {
      static LibXR::WifiClient::ScanResult results[10];
      static esp_blufi_ap_record_t list[10];
      size_t found = 0;
      self->wifi_->Scan(results, 10, found);

      for (size_t i = 0; i < found; i++) {
        memcpy(list[i].ssid, results[i].ssid, 32);
        list[i].rssi = results[i].rssi;
      }
      esp_blufi_send_wifi_list(found, list);
      break;
    }

    case ESP_BLUFI_EVENT_GET_WIFI_STATUS: {
      esp_blufi_extra_info_t info = {};
      uint8_t unsupport_str[] = {"Unsupported"};
      info.softap_ssid = unsupport_str;
      esp_blufi_send_wifi_conn_report(WIFI_MODE_STA, ESP_BLUFI_STA_NO_IP, 0,
                                      &info);

      break;
    }

    default:
      break;
  }
}

ErrorCode NetDebugLink::StartBlufiBlocking(uint32_t timeout_ms) {
  XR_LOG_INFO("Starting BLUFI...");

  wifi_->Disconnect();

  esp_blufi_callbacks_t cbs = {
      .event_cb = BlufiEventCallback,
      .negotiate_data_handler = blufi_dh_negotiate_data_handler,
      .encrypt_func = blufi_aes_encrypt,
      .decrypt_func = blufi_aes_decrypt,
      .checksum_func = blufi_crc_checksum,
  };

  ESP_ERROR_CHECK(esp_blufi_controller_init());
  ESP_ERROR_CHECK(esp_blufi_host_and_cb_init(&cbs));

  TickType_t start = xTaskGetTickCount();
  TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

  XR_LOG_INFO("Waiting for credentials...");

  while (true) {
    EventBits_t bits =
        xEventGroupWaitBits(s_wifi_event_group, GOT_CREDENTIAL_BIT, pdTRUE,
                            pdFALSE, pdMS_TO_TICKS(500));

    if (bits & GOT_CREDENTIAL_BIT) {
      XR_LOG_INFO("Received WiFi SSID: %s", sta_cfg_.ssid);
      XR_LOG_INFO("Received WiFi PASS: %s", sta_cfg_.password);
      esp_blufi_disconnect();
      wifi_->Connect(sta_cfg_);
      esp_blufi_host_deinit();
      esp_blufi_controller_deinit();

      static LibXR::Topic::PackedData<LibXR::WifiClient::Config> buf;
      if (wifi_->IsConnected()) {
        LibXR::Topic::PackData(
            LibXR::Topic::TopicHandle(wifi_config_topic_)->data_.crc32, buf,
            sta_cfg_);
        to_cdc_data_queue_mutex_.Lock();
        to_cdc_data_queue_.PushBatch(&buf, sizeof(buf));
        to_cdc_data_queue_mutex_.Unlock();
      }
      return ErrorCode::OK;
    }

    if ((xTaskGetTickCount() - start) > timeout_ticks) {
      XR_LOG_WARN("BLUFI credential timeout.");
      esp_blufi_disconnect();
      esp_blufi_host_deinit();
      esp_blufi_controller_deinit();
      return ErrorCode::TIMEOUT;
    }
  }
}
