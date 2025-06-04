#pragma once
// clang-format off
/* === MODULE MANIFEST ===
module_name: NetDebugLink
module_description: 
  通过 WiFi 实现远程调试、日志查看与控制的多串口桥接模块 /
  A multi-port bridge module enabling remote debugging, logging, and control via WiFi
constructor_args:
  - tcp_port: 5000                # TCP 端口 / TCP port
  - udp_port: 5001                # UDP 端口 / UDP port
  - thread_stack_size: 8192
  - usb: uart_cdc
  - uarts:
    - UART0
required_hardware: 
  - wifi_client/WiFiClient        # WiFi 客户端 / WiFi client
  - usb/uart_cdc                  # USB CDC 主模式连接 Linux 主机 / USB CDC host mode to control Linux
  - uart/UART0
  - uart/UART1
  - button                 # 配置按钮 / Configuration button
repository: https://github.com/xrobot-org/NetDebugLink
=== END MANIFEST === */
// clang-format on

#include <lwip/sockets.h>

#include "app_framework.hpp"
#include "gpio.hpp"
#include "libxr.hpp"
#include "logger.hpp"
#include "net/wifi_client.hpp"
#include "pwm.hpp"
#include "uart.hpp"

class NetDebugLink : public LibXR::Application {
 public:
  enum class Mode { Init, SMART_CONFIG, SCANING, CONNECTED };

  enum class Command : uint8_t {
    PING = 0,
    REBOOT = 1,
  };

  typedef struct {
    LibXR::UART *uart;
    LibXR::Topic topic;
  } UartInfo;

  NetDebugLink(LibXR::HardwareContainer &hw, LibXR::ApplicationManager &app,
               uint32_t tcp_port, uint32_t udp_port, uint32_t thread_stack_size,
               const char *usb,
               const std::initializer_list<const char *> &uarts)
      : tcp_port_(tcp_port),
        udp_port_(udp_port),
        uart_cdc_topic_(LibXR::Topic("uart_cdc", 4096)),
        wifi_config_topic_("wifi_config", sizeof(LibXR::WifiClient::Config)),
        command_topic_("command", sizeof(Command)),
        to_net_data_queue_(1, 4096),
        to_cdc_data_queue_(1, 4096),
        form_net_server_(4096) {
    instance_ = this;

    BlufiInit();

    led_ = hw.template FindOrExit<LibXR::PWM>({"led", "LED", "led1", "LED1"});
    button_ = hw.template FindOrExit<LibXR::GPIO>({"button"});
    wifi_ = hw.template FindOrExit<LibXR::WifiClient>({"wifi_client"});
    uart_cdc_ = hw.template FindOrExit<LibXR::UART>({usb});

    void (*from_net_data_cb_fun)(
        bool in_isr, LibXR::Topic::TopicHandle tp,
        LibXR::RawData &data) = [](bool in_isr, LibXR::Topic::TopicHandle tp,
                                   LibXR::RawData &data) {
      auto foreach_fun = [&](UartInfo &info) {
        if (info.topic.GetKey() == instance_->uart_cdc_topic_.GetKey()) {
          LibXR::Mutex::LockGuard guard(instance_->to_cdc_data_queue_mutex_);
          instance_->to_cdc_data_queue_.PushBatch(data.addr_, data.size_);
          return ErrorCode::FAILED;
        }

        if (info.topic.GetKey() == tp->data_.crc32) {
          LibXR::WriteOperation write_op(instance_->write_sem_, 20);
          info.uart->Write(data, write_op);
          return ErrorCode::FAILED;
        }
        return ErrorCode::OK;
      };

      instance_->uarts_.Foreach<UartInfo>(foreach_fun);
    };

    auto cdc_node =
        new LibXR::LockFreeList::Node<UartInfo>({uart_cdc_, uart_cdc_topic_});
    form_net_server_.Register(cdc_node->data_.topic);
    auto from_net_data_cb_cdc = LibXR::Topic::Callback::Create(
        from_net_data_cb_fun, LibXR::Topic::TopicHandle(cdc_node->data_.topic));
    cdc_node->data_.topic.RegisterCallback(from_net_data_cb_cdc);
    uarts_.Add(*cdc_node);

    for (auto uart_name : uarts) {
      auto node = new LibXR::LockFreeList::Node<UartInfo>(
          {hw.template FindOrExit<LibXR::UART>({uart_name}),
           LibXR::Topic(uart_name, 4096)});
      form_net_server_.Register(node->data_.topic);
      auto from_net_data_cb = LibXR::Topic::Callback::Create(
          from_net_data_cb_fun, LibXR::Topic::TopicHandle(node->data_.topic));
      node->data_.topic.RegisterCallback(from_net_data_cb);
      uarts_.Add(*node);
    }

    PeripheralInit();

    thread_.Create(this, ThreadFun, "NetDebugLink", thread_stack_size,
                   LibXR::Thread::Priority::MEDIUM);

    InitDataLink();

    InitPingTask();

    app.Register(*this);
  }

  void InitPingTask() {
    void (*ping_task_fun)(NetDebugLink *) = [](NetDebugLink *self) {
      LibXR::Topic::PackedData<Command> ping;
      Command cmd = Command::PING;
      LibXR::Topic::PackData(self->command_topic_.GetKey(), ping, cmd);
      self->to_cdc_data_queue_mutex_.Lock();
      self->to_cdc_data_queue_.PushBatch(&ping, sizeof(ping));
      self->to_cdc_data_queue_mutex_.Unlock();
    };

    auto ping_task = LibXR::Timer::CreateTask(ping_task_fun, this, 1000);
    LibXR::Timer::Add(ping_task);
    LibXR::Timer::Start(ping_task);
  }

  void InitDataLink() {
    static uint8_t read_buf[4096];
    static uint8_t pack_buf[4096 + LibXR::Topic::PACK_BASE_SIZE];
    void (*push_uart_data_task_fun)(NetDebugLink *) = [](NetDebugLink *self) {
      LibXR::ReadOperation read_op(self->read_sem_, 20);
      self->uarts_.Foreach<UartInfo>([&](UartInfo &info) {
        auto &uart = info.uart;
        auto topic = LibXR::Topic::TopicHandle(info.topic);
        auto read_able_size =
            LibXR::min(uart->read_port_->Size(), sizeof(read_buf));
        if (read_able_size > 0) {
          uart->Read({read_buf, read_able_size}, read_op);
          LibXR::Topic::PackData(topic->data_.crc32, {read_buf, read_able_size},
                                 pack_buf);

          LibXR::Mutex::LockGuard guard(self->to_net_data_queue_mutex_);
          self->to_net_data_queue_.PushBatch(
              pack_buf, read_able_size + LibXR::Topic::PACK_BASE_SIZE);
        }

        return ErrorCode::OK;
      });
    };

    auto push_uart_data_task =
        LibXR::Timer::CreateTask(push_uart_data_task_fun, this, 2);
    LibXR::Timer::Add(push_uart_data_task);
    LibXR::Timer::Start(push_uart_data_task);

    void (*write_cdc_task_fun)(NetDebugLink *) = [](NetDebugLink *self) {
      static uint8_t buf[4096];
      LibXR::WriteOperation write_op(self->write_sem_, 20);
      LibXR::Mutex::LockGuard guard(self->to_cdc_data_queue_mutex_);
      auto write_able_size = self->to_cdc_data_queue_.Size();
      if (write_able_size > 0) {
        self->to_cdc_data_queue_.PopBatch(buf, write_able_size);
        self->uart_cdc_->Write({buf, write_able_size}, write_op);
      }
    };

    auto write_cdc_task = LibXR::Timer::CreateTask(write_cdc_task_fun, this, 2);
    LibXR::Timer::Add(write_cdc_task);
    LibXR::Timer::Start(write_cdc_task);
  }

  static void ThreadFun(NetDebugLink *self) {
    static uint8_t buf[1024];

    while (true) {
      self->mode_ = Mode::SCANING;

      struct sockaddr_in addr{};
      addr.sin_family = AF_INET;
      addr.sin_port = htons(self->udp_port_);
      addr.sin_addr.s_addr = htonl(INADDR_ANY);

      int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
      if (sock < 0) {
        XR_LOG_ERROR("socket failed");
        continue;
      }

      if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        XR_LOG_ERROR("bind failed");
        close(sock);
        continue;
      }

      struct timeval timeout = {.tv_sec = 5, .tv_usec = 0};
      setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

      while (true) {
        if (self->smartconfig_requested_ || !self->wifi_->IsConnected()) {
          self->smartconfig_requested_ = false;
          self->mode_ = Mode::SMART_CONFIG;

          auto result = self->StartBlufiBlocking(30000);
          if (result != ErrorCode::OK) {
            XR_LOG_WARN("BLUFI failed or timed out: %d", result);
            self->mode_ = Mode::SMART_CONFIG;
          } else {
            XR_LOG_INFO("BLUFI success");
            self->mode_ = Mode::SCANING;
          }
        }

        struct sockaddr_in sender;
        socklen_t sender_len = sizeof(sender);
        int len = recvfrom(sock, buf, sizeof(buf) - 1, 0,
                           (struct sockaddr *)&sender, &sender_len);
        if (len >= 0) {
          buf[len] = 0;
          XR_LOG_INFO("Received from %s: %s", inet_ntoa(sender.sin_addr), buf);
          self->mode_ = Mode::CONNECTED;
          self->OnConnected(&sender);
        } else {
          XR_LOG_WARN("recvfrom timed out");
          self->mode_ = Mode::SCANING;
        }
      }

      close(sock);
    }
  }

  void OnConnected(struct sockaddr_in *addr) {
    int tcp_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (tcp_sock < 0) {
      XR_LOG_ERROR("TCP socket creation failed");
      return;
    } else {
      XR_LOG_INFO("TCP socket created");
    }

    // 设置非阻塞模式
    int flags = fcntl(tcp_sock, F_GETFL, 0);
    if (flags == -1) {
      XR_LOG_ERROR("fcntl get flags failed");
      close(tcp_sock);
      return;
    }
    fcntl(tcp_sock, F_SETFL, flags | O_NONBLOCK);

    addr->sin_family = AF_INET;
    addr->sin_port = htons(tcp_port_);

    XR_LOG_INFO("Connecting to TCP server %s:%d", inet_ntoa(addr->sin_addr),
                tcp_port_);

    if (connect(tcp_sock, (struct sockaddr *)addr, sizeof(*addr)) != 0) {
      if (errno != EINPROGRESS) {
        XR_LOG_ERROR("TCP connect failed: %d", errno);
        close(tcp_sock);
        return;
      }
    }

    struct tcp_keepalive {
      uint32_t keep_idle;   // 空闲时间
      uint32_t keep_intvl;  // Keep Alive 间隔
      uint32_t keep_count;  // 最大重试次数
    };

    tcp_keepalive ka = {.keep_idle = 5, .keep_intvl = 1, .keep_count = 5};
    setsockopt(tcp_sock, IPPROTO_TCP, TCP_KEEPALIVE, &ka, sizeof(ka));

    while (!smartconfig_requested_) {
      fd_set readfds, writefds;
      FD_ZERO(&readfds);
      FD_ZERO(&writefds);
      FD_SET(tcp_sock, &readfds);
      FD_SET(tcp_sock, &writefds);

      struct timeval timeout = {2, 0};  // 设置 select 超时

      int ret = select(tcp_sock + 1, &readfds, &writefds, NULL, &timeout);
      if (ret < 0) {
        XR_LOG_ERROR("select failed: %d", errno);
        close(tcp_sock);
        return;
      }

      if (FD_ISSET(tcp_sock, &readfds)) {
        // 检查是否有数据可读或连接关闭
        char buf[4096];
        ssize_t bytes_received = recv(tcp_sock, buf, sizeof(buf), 0);
        if (bytes_received < 0) {
          XR_LOG_ERROR("TCP recv failed: %d", errno);
          close(tcp_sock);
          return;
        } else if (bytes_received == 0) {
          // 连接关闭
          XR_LOG_ERROR("Connection closed by server");
          close(tcp_sock);
          return;
        } else {
          XR_LOG_DEBUG("Received %d bytes", bytes_received);
        }
      }

      if (FD_ISSET(tcp_sock, &writefds)) {
        static uint8_t buf[4096];
        to_net_data_queue_mutex_.Lock();
        auto len = to_net_data_queue_.Size();
        if (len > 0) {
          to_net_data_queue_.PopBatch(buf, len);
          to_net_data_queue_mutex_.Unlock();
          ssize_t ans = send(tcp_sock, buf, len, 0);
          if (ans < 0) {
            XR_LOG_ERROR("TCP send failed: %d", errno);
            close(tcp_sock);
            return;
          }
        } else {
          to_net_data_queue_mutex_.Unlock();
        }
      }

      LibXR::Thread::Sleep(1);
    }

    close(tcp_sock);
  }
  void OnMonitor() override {}

  void OnButton() { smartconfig_requested_ = true; }

  void BlufiInit();

  void PeripheralInit();

  ErrorCode StartBlufiBlocking(uint32_t timeout_ms);

  static inline NetDebugLink *instance_ = nullptr;

  Mode mode_ = Mode::Init;
  bool smartconfig_requested_ = false;

  static constexpr int GOT_CREDENTIAL_BIT = 1;
  LibXR::WifiClient::Config sta_cfg_;

  uint32_t tcp_port_;
  uint32_t udp_port_;

  LibXR::GPIO *button_;
  LibXR::PWM *led_;
  LibXR::UART *uart_cdc_;
  LibXR::WifiClient *wifi_;
  LibXR::LockFreeList uarts_;
  LibXR::LockFreeList topics_;
  LibXR::Topic uart_cdc_topic_;
  LibXR::Topic wifi_config_topic_;
  LibXR::Topic command_topic_;

  LibXR::BaseQueue to_net_data_queue_;
  LibXR::Mutex to_net_data_queue_mutex_;
  LibXR::BaseQueue to_cdc_data_queue_;
  LibXR::Mutex to_cdc_data_queue_mutex_;
  LibXR::Semaphore read_sem_;
  LibXR::Semaphore write_sem_;
  LibXR::Topic::Server form_net_server_;

  LibXR::Thread thread_;
};
