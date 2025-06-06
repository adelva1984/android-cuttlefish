/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "cuttlefish/host/commands/process_sandboxer/policies.h"

#include <sys/syscall.h>

#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/util/path.h"

namespace cuttlefish::process_sandboxer {

using sapi::file::JoinPath;

sandbox2::PolicyBuilder TombstoneReceiverPolicy(const HostInfo& host) {
  return BaselinePolicy(host, host.HostToolExe("tombstone_receiver"))
      .AddDirectory(host.log_dir, /* is_ro= */ false)
      .AddDirectory(JoinPath(host.runtime_dir, "tombstones"),
                    /* is_ro= */ false)
      .AddFile(host.cuttlefish_config_path)
      .AllowSafeFcntl()
      .AllowSelect()
      .AllowSyscall(__NR_accept)
      .AllowSyscall(__NR_getsockname)
      .AllowSyscall(__NR_recvfrom)
      .AllowTCGETS();
}

}  // namespace cuttlefish::process_sandboxer
