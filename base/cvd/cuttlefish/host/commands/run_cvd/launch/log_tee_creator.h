//
// Copyright (C) 2019 The Android Open Source Project
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

#include <fruit/fruit.h>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

class LogTeeCreator {
 public:
  INJECT(LogTeeCreator(const CuttlefishConfig::InstanceSpecific& instance));

  // Creates a log tee command for the stdout and stderr channels of the given
  // command.
  Result<Command> CreateFullLogTee(Command& cmd, std::string process_name);

  // Creates a log tee command for specified channel of the given command.
  Result<Command> CreateLogTee(Command& cmd, std::string process_name,
                               Subprocess::StdIOChannel log_channel);

 private:
  const CuttlefishConfig::InstanceSpecific& instance_;
};

}  // namespace cuttlefish
