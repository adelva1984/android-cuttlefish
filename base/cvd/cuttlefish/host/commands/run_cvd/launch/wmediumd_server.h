//
// Copyright (C) 2023 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include <fruit/fruit.h>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/run_cvd/launch/grpc_socket_creator.h"
#include "cuttlefish/host/commands/run_cvd/launch/log_tee_creator.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/feature/command_source.h"
#include "cuttlefish/host/libs/vm_manager/vm_manager.h"

namespace cuttlefish {

class WmediumdServer : public vm_manager::VmmDependencyCommand {
 public:
  INJECT(
      WmediumdServer(const CuttlefishConfig::EnvironmentSpecific& environment,
                     const CuttlefishConfig::InstanceSpecific& instance,
                     LogTeeCreator& log_tee, GrpcSocketCreator& grpc_socket));

  // CommandSource
  Result<std::vector<MonitorCommand>> Commands() override;

  // SetupFeature
  std::string Name() const override;
  bool Enabled() const override;

  Result<void> WaitForAvailability() const override;

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override;
  Result<void> ResultSetup() override;

  const CuttlefishConfig::EnvironmentSpecific& environment_;
  const CuttlefishConfig::InstanceSpecific& instance_;
  LogTeeCreator& log_tee_;
  GrpcSocketCreator& grpc_socket_;
  std::string config_path_;
};

fruit::Component<fruit::Required<
    const CuttlefishConfig, const CuttlefishConfig::EnvironmentSpecific,
    const CuttlefishConfig::InstanceSpecific, LogTeeCreator, GrpcSocketCreator>>
WmediumdServerComponent();

}  // namespace cuttlefish
