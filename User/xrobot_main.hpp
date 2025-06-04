#include "app_framework.hpp"
#include "libxr.hpp"

// Module headers
#include "NetDebugLink.hpp"
#include "BlinkLED.hpp"

static void XRobotMain(LibXR::HardwareContainer &hw) {
  using namespace LibXR;
  ApplicationManager appmgr;

  // Auto-generated module instantiations
  static NetDebugLink netdebuglink(hw, appmgr, 5000, 5001, 11000, "uart_cdc", {"uart1", "uart2"});

  while (true) {
    appmgr.MonitorAll();
    Thread::Sleep(1000);
  }
}