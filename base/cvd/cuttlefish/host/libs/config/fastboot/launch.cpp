/*
 * Copyright (C) 2022 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "cuttlefish/host/libs/config/fastboot/fastboot.h"

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fruit/component.h>
#include <fruit/fruit_forward_decls.h>
#include <fruit/macro.h>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/host/commands/kernel_log_monitor/kernel_log_server.h"
#include "cuttlefish/host/libs/config/boot_flow.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/known_paths.h"
#include "cuttlefish/host/libs/feature/command_source.h"
#include "cuttlefish/host/libs/feature/feature.h"
#include "cuttlefish/host/libs/feature/kernel_log_pipe_provider.h"

namespace cuttlefish {
namespace {

class FastbootProxy : public CommandSource, public KernelLogPipeConsumer {
 public:
  INJECT(FastbootProxy(const CuttlefishConfig::InstanceSpecific& instance,
                       const FastbootConfig& fastboot_config,
                       KernelLogPipeProvider& log_pipe_provider))
      : instance_(instance),
        fastboot_config_(fastboot_config),
        log_pipe_provider_(log_pipe_provider) {}

  Result<std::vector<MonitorCommand>> Commands() override {
    const std::string ethernet_host =
        instance_.ethernet_ipv6() + "%" + instance_.ethernet_bridge_name();

    Command tunnel(SocketVsockProxyBinary());
    tunnel.AddParameter("--events_fd=", kernel_log_pipe_);
    tunnel.AddParameter("--start_event_id=", monitor::Event::FastbootStarted);
    tunnel.AddParameter("--stop_event_id=", monitor::Event::AdbdStarted);
    tunnel.AddParameter("--server_type=", "tcp");
    tunnel.AddParameter("--server_tcp_port=", instance_.fastboot_host_port());
    tunnel.AddParameter("--client_type=", "tcp");
    tunnel.AddParameter("--client_tcp_host=", ethernet_host);
    tunnel.AddParameter("--client_tcp_port=", "5554");
    tunnel.AddParameter("--label=", "fastboot");

    std::vector<MonitorCommand> commands;
    commands.emplace_back(std::move(tunnel));
    return commands;
  }

  std::string Name() const override { return "FastbootProxy"; }
  bool Enabled() const override {
    const auto boot_flow = instance_.boot_flow();
    const bool is_android_boot = boot_flow == BootFlow::Android ||
                                 boot_flow == BootFlow::AndroidEfiLoader;

    return is_android_boot && fastboot_config_.ProxyFastboot();
  }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override {
    return {static_cast<SetupFeature*>(&log_pipe_provider_)};
  }

  Result<void> ResultSetup() override {
    kernel_log_pipe_ = log_pipe_provider_.KernelLogPipe();
    return {};
  }

  const CuttlefishConfig::InstanceSpecific& instance_;
  const FastbootConfig& fastboot_config_;
  KernelLogPipeProvider& log_pipe_provider_;
  SharedFD kernel_log_pipe_;
};

}  // namespace

fruit::Component<fruit::Required<KernelLogPipeProvider,
                                 const CuttlefishConfig::InstanceSpecific,
                                 const FastbootConfig>>
LaunchFastbootComponent() {
  return fruit::createComponent()
      .addMultibinding<CommandSource, FastbootProxy>()
      .addMultibinding<KernelLogPipeConsumer, FastbootProxy>()
      .addMultibinding<SetupFeature, FastbootProxy>();
}

}  // namespace cuttlefish
