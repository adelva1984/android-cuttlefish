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

#include "host/commands/cvd/server_command/components.h"

#include "host/commands/cvd/server_command/fetch.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command_fleet_impl.h"
#include "host/commands/cvd/server_command_generic_impl.h"
#include "host/commands/cvd/server_command_start_impl.h"

namespace cuttlefish {

fruit::Component<fruit::Required<InstanceManager>> cvdCommandComponent() {
  return fruit::createComponent()
      .addMultibinding<CvdServerHandler, cvd_cmd_impl::CvdCommandHandler>()
      .addMultibinding<CvdServerHandler, cvd_cmd_impl::CvdStartCommandHandler>()
      .addMultibinding<CvdServerHandler, cvd_cmd_impl::CvdFleetCommandHandler>()
      .install(cvdFetchCommandComponent);
}

}  // namespace cuttlefish
