#include <atomic>
#include <format>
#include <hyprland/src/debug/Log.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprlang.hpp>
#include <sstream>
#include <unistd.h>

#include "globals.hpp"
#include "src/plugins/PluginAPI.hpp"

namespace {
constexpr std::string kTag = "[hyprlid]";
constexpr std::string kTrue = "true";
constexpr std::string kFallbackMonitorName = "FALLBACK";

SP<HOOK_CALLBACK_FN> g_monitorAddedCallback = nullptr;
SP<HOOK_CALLBACK_FN> g_monitorRemovedCallback = nullptr;
SP<HOOK_CALLBACK_FN> g_configReloadedCallback = nullptr;

std::string g_mainMonitorConfig = "";
std::string g_mainMonitorName = "";
std::string g_lockCommand = "";
bool g_isDebug = false;

std::atomic_bool g_isLidClosed = false;
std::atomic_int g_secondaryMonitorsCount = 0;

void notifyInfo(const std::string &text) {
  HyprlandAPI::addNotification(PHANDLE, text, CColor{0.2, 1.0, 0.2, 1.0}, 5000);
}

void notifyErr(const std::string &text) {
  HyprlandAPI::addNotification(PHANDLE, text, CColor{0.1, 0.2, 0.2, 1.0}, 5000);
}

void logInfo(const std::string &message) {
  auto const m = std::format("{} {}", kTag, message);
  Debug::log(INFO, message);
  if (g_isDebug) {
    notifyInfo(message);
  }
}

void logErr(const std::string &message) {
  auto const m = std::format("{} {}", kTag, message);
  Debug::log(ERR, message);
  notifyErr(message);
}

// trim from right
inline std::string &rtrim(std::string &s, const char *t = " \t\n\r\f\v") {
  s.erase(s.find_last_not_of(t) + 1);
  return s;
}

void updateState() {
  // No main monitor is set
  if (g_mainMonitorConfig.empty()) {
    return;
  }

  if (g_isLidClosed) {
    if (g_secondaryMonitorsCount > 0) {
      logInfo("Disabling main monitor");
      HyprlandAPI::invokeHyprctlCommand(
          "keyword", std::format("monitor {}, disable", g_mainMonitorName));
    } else if (!g_lockCommand.empty()) {
      logInfo("Locking session");
      HyprlandAPI::invokeHyprctlCommand(
          "keyword", std::format("monitor {}", g_mainMonitorConfig));
      HyprlandAPI::invokeHyprctlCommand("dispatch", g_lockCommand);
      HyprlandAPI::invokeHyprctlCommand(
          "dispatch", std::format("dpms off {}", g_mainMonitorName));
    }
  } else {
    logInfo("Enabling main monitor");
    HyprlandAPI::invokeHyprctlCommand(
        "keyword", std::format("monitor {}", g_mainMonitorConfig));
    HyprlandAPI::invokeHyprctlCommand(
        "dispatch", std::format("dpms on {}", g_mainMonitorName));
  }
}

void monitorAdded(void *, SCallbackInfo &, std::any any) {
  auto monitor = std::any_cast<PHLMONITOR>(any);
  logInfo(std::format("Monitor[{}] added: [{}]", monitor->ID, monitor->szName));
  if (monitor->szName != g_mainMonitorName &&
      monitor->szName != kFallbackMonitorName) {
    if (++g_secondaryMonitorsCount > 0) {
      updateState();
    }
  }
}

void monitorRemoved(void *, SCallbackInfo &, std::any any) {
  PHLMONITOR monitor = std::any_cast<PHLMONITOR>(any);
  logInfo(
      std::format("Monitor[{}] removed: [{}]", monitor->ID, monitor->szName));
  if (monitor->szName != g_mainMonitorName &&
      monitor->szName != kFallbackMonitorName) {
    if (--g_secondaryMonitorsCount < 1) {
      updateState();
    }
  }
}

void configReloaded(void *, SCallbackInfo &, std::any any) {
  auto *const mainMonitorConfig =
      (Hyprlang::STRING const *)HyprlandAPI::getConfigValue(
          PHANDLE, "plugin:hyprlid:laptop_monitor")
          ->getDataStaticPtr();

  if (mainMonitorConfig == nullptr) {
    logInfo("Failed to read laptop_monitor config value");
    return;
  }

  auto *const lockCommadConfig =
      (Hyprlang::STRING const *)HyprlandAPI::getConfigValue(
          PHANDLE, "plugin:hyprlid:lock_command")
          ->getDataStaticPtr();

  if (lockCommadConfig != nullptr) {
    g_lockCommand = std::string(*lockCommadConfig);
    logInfo(std::format("Lock command: {}", g_lockCommand));
  }

  auto *const DEBUG_FLAG =
      (Hyprlang::STRING const *)HyprlandAPI::getConfigValue(
          PHANDLE, "plugin:hyprlid:debug")
          ->getDataStaticPtr();

  if (DEBUG_FLAG != nullptr) {
    g_isDebug = std::string(*DEBUG_FLAG) == kTrue;
  }

  g_mainMonitorConfig = std::string(*mainMonitorConfig);
  std::stringstream ss(g_mainMonitorConfig);
  std::getline(ss, g_mainMonitorName, ',');
  g_mainMonitorName = rtrim(g_mainMonitorName);
  updateState();
  logInfo(std::format("Main monitor[{}] = {}", g_mainMonitorName,
                      g_mainMonitorConfig));
}

void lidClosed(std::string params) {
  g_isLidClosed = true;
  logInfo("Lid closed");
  updateState();
}

void lidOpen(std::string params) {
  g_isLidClosed = false;
  logInfo("Lid opened");
  updateState();
}

} // namespace

// Do NOT change this function.
APICALL EXPORT std::string PLUGIN_API_VERSION() { return HYPRLAND_API_VERSION; }

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
  PHANDLE = handle;

  const std::string HASH = __hyprland_api_get_hash();

  if (HASH != GIT_COMMIT_HASH) {
    logErr("Failed to initialize: Version mismatch (headers ver is not equal "
           "to running hyprland ver)");
    throw std::runtime_error(std::format("{} {}", kTag, "Version mismatch"));
  }

  HyprlandAPI::addDispatcher(PHANDLE, "lidclosed", lidClosed);
  HyprlandAPI::addDispatcher(PHANDLE, "lidopen", lidOpen);

  if (!HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprlid:laptop_monitor",
                                   Hyprlang::STRING{""})) {
    logErr("Failed to add a config value");
    throw std::runtime_error(
        std::format("{} {}", kTag, "Failed to add config value"));
  }

  if (!HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprlid:debug",
                                   Hyprlang::STRING{"false"})) {
    logErr("Failed to add a config value");
    throw std::runtime_error(
        std::format("{} {}", kTag, "Failed to add config value"));
  }

  if (!HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprlid:lock_command",
                                   Hyprlang::STRING{""})) {
    logErr("Failed to add a config value");
    throw std::runtime_error(
        std::format("{} {}", kTag, "Failed to add config value"));
  }

  g_configReloadedCallback = HyprlandAPI::registerCallbackDynamic(
      PHANDLE, "configReloaded", configReloaded);
  if (g_configReloadedCallback == nullptr) {
    logErr("Failed to subscribe to configReloaded event");
    throw std::runtime_error(std::format(
        "{} {}", kTag, "Failed to subscribe to configReload event"));
  }

  g_monitorAddedCallback = HyprlandAPI::registerCallbackDynamic(
      PHANDLE, "monitorAdded", monitorAdded);
  if (g_monitorAddedCallback == nullptr) {
    logErr("Failed to subscribe to monitorAdded event");
  } else {
    logInfo("Subscribed to monitorAdded event");
  }

  g_monitorRemovedCallback = HyprlandAPI::registerCallbackDynamic(
      PHANDLE, "monitorRemoved", monitorRemoved);
  if (g_monitorRemovedCallback == nullptr) {
    logErr("Failed to subscribe to monitorRemoved event");
  } else {
    logInfo("Subscribed to monitorRemoved event");
  }

  return {"hyprlid",
          "A plugin to manage laptop lid open/close actions in a multi-monitor "
          "setup",
          "vkovtash", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() { ; }
