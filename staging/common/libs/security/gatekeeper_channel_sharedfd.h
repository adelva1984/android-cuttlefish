/*
 * Copyright 2020 The Android Open Source Project
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

#pragma once

#include "common/libs/security/gatekeeper_channel.h"

#include "gatekeeper/gatekeeper_messages.h"

#include "common/libs/transport/channel_sharedfd.h"

namespace cuttlefish {
/*
 * Interface for communication channels that synchronously communicate
 * Gatekeeper IPC/RPC calls. Sends messages over a file descriptor.
 */
class SharedFdGatekeeperChannel : public GatekeeperChannel {
 public:
  SharedFdGatekeeperChannel(SharedFD input, SharedFD output);

  bool SendRequest(uint32_t command,
                   const gatekeeper::GateKeeperMessage& message) override;
  bool SendResponse(uint32_t command,
                    const gatekeeper::GateKeeperMessage& message) override;
  transport::ManagedMessage ReceiveMessage() override;

 private:
  transport::SharedFdChannel channel_;
  bool SendMessage(uint32_t command, bool response,
                   const gatekeeper::GateKeeperMessage& message);
};

}  // namespace cuttlefish