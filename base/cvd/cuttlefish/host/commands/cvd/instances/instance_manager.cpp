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

#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"

#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <android-base/file.h>
#include <android-base/scopeguard.h>
#include <android-base/strings.h>
#include <fmt/format.h>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/host/commands/cvd/cli/commands/host_tool_target.h"
#include "cuttlefish/host/commands/cvd/instances/config_path.h"
#include "cuttlefish/host/commands/cvd/instances/instance_record.h"
#include "cuttlefish/host/commands/cvd/legacy/cvd_server.pb.h"
#include "cuttlefish/host/commands/cvd/utils/common.h"
#include "cuttlefish/host/libs/config/config_constants.h"
#include "cuttlefish/host/libs/config/config_utils.h"

namespace cuttlefish {
namespace {

// Returns true only if command terminated normally, and returns 0
Result<void> RunCommand(Command&& command) {
  auto subprocess = command.Start();
  siginfo_t infop{};
  // This blocks until the process exits, but doesn't reap it.
  auto result = subprocess.Wait(&infop, WEXITED);
  CF_EXPECT(result != -1, "Lost track of subprocess pid");
  CF_EXPECT(infop.si_code == CLD_EXITED && infop.si_status == 0);
  return {};
}

Result<void> RemoveGroupDirectory(const LocalInstanceGroup& group) {
  std::string per_user_dir = PerUserDir();
  if (!android::base::StartsWith(group.HomeDir(), per_user_dir)) {
    LOG(WARNING)
        << "Instance group home directory not under user specific directory("
        << per_user_dir << "), artifacts not deleted";
    return {};
  }
  CF_EXPECT(
      RecursivelyRemoveDirectory(CF_EXPECT(GroupDirFromHome(group.HomeDir()))),
      "Failed to remove group directory");
  return {};
}

}  // namespace

InstanceManager::InstanceManager(InstanceLockFileManager& lock_manager,
                                 InstanceDatabase& instance_db)
    : lock_manager_(lock_manager), instance_db_(instance_db) {}

Result<void> InstanceManager::SetAcloudTranslatorOptout(bool optout) {
  CF_EXPECT(instance_db_.SetAcloudTranslatorOptout(optout));
  return {};
}

Result<bool> InstanceManager::GetAcloudTranslatorOptout() const {
  return CF_EXPECT(instance_db_.GetAcloudTranslatorOptout());
}

Result<std::pair<LocalInstance, LocalInstanceGroup>>
InstanceManager::FindInstanceWithGroup(
    const InstanceDatabase::Filter& filter) const {
  return instance_db_.FindInstanceWithGroup(filter);
}

Result<bool> InstanceManager::HasInstanceGroups() const {
  return !CF_EXPECT(instance_db_.IsEmpty());
}

Result<LocalInstanceGroup> InstanceManager::CreateInstanceGroup(
    const selector::GroupCreationInfo& group_info) {
  cvd::InstanceGroup new_group;
  new_group.set_name(group_info.group_name);
  new_group.set_home_directory(group_info.home);
  new_group.set_host_artifacts_path(group_info.host_artifacts_path);
  new_group.set_product_out_path(group_info.product_out_path);
  for (const auto& instance : group_info.instances) {
    auto& new_instance = *new_group.add_instances();
    new_instance.set_id(instance.instance_id_);
    new_instance.set_name(instance.per_instance_name_);
    new_instance.set_state(instance.initial_state_);
  }
  return CF_EXPECT(instance_db_.AddInstanceGroup(new_group));
}

Result<bool> InstanceManager::RemoveInstanceGroupByHome(
    const std::string& dir) {
  LocalInstanceGroup group = CF_EXPECT(instance_db_.FindGroup({.home = dir}));
  CF_EXPECT(!group.HasActiveInstances(),
            "Group still contains active instances");
  for (auto& instance : group.Instances()) {
    if (instance.id() == 0) {
      continue;
    }
    auto remove_res = lock_manager_.RemoveLockFile(instance.id());
    if (!remove_res.ok()) {
      LOG(ERROR) << "Failed to remove instance id lock: "
                 << remove_res.error().FormatForEnv();
    }
  }
  CF_EXPECT(RemoveGroupDirectory(group));

  return CF_EXPECT(instance_db_.RemoveInstanceGroup(group.GroupName()));
}

Result<std::string> InstanceManager::StopBin(
    const std::string& host_android_out) {
  return CF_EXPECT(HostToolTarget(host_android_out).GetStopBinName());
}

Result<void> InstanceManager::UpdateInstanceGroup(
    const LocalInstanceGroup& group) {
  CF_EXPECT(instance_db_.UpdateInstanceGroup(group));
  return {};
}

Result<void> InstanceManager::IssueStopCommand(
    const CommandRequest& request, const std::string& config_file_path,
    LocalInstanceGroup& group) {
  const auto stop_bin = CF_EXPECT(StopBin(group.HostArtifactsPath()));
  Command command(group.HostArtifactsPath() + "/bin/" + stop_bin);
  command.AddParameter("--clear_instance_dirs");
  command.AddEnvironmentVariable(kCuttlefishConfigEnvVarName, config_file_path);
  auto wait_result = RunCommand(std::move(command));
  /**
   * --clear_instance_dirs may not be available for old branches. This causes
   * the stop_cvd to terminates with a non-zero exit code due to the parsing
   * error. Then, we will try to re-run it without the flag.
   */
  if (!wait_result.ok()) {
    std::stringstream error_msg;
    std::cout << stop_bin << " was executed internally, and failed. It might "
              << "be failing to parse the new --clear_instance_dirs. Will try "
              << "without the flag.\n";
    Command no_clear_instance_dir_command(group.HostArtifactsPath() + "/bin/" +
                                          stop_bin);
    wait_result = RunCommand(std::move(no_clear_instance_dir_command));
  }

  if (!wait_result.ok()) {
    std::cerr << "Warning: error stopping instances for dir \"" +
                     group.HomeDir() +
                     "\".\nThis can happen if instances are already stopped.\n";
  }
  group.SetAllStates(cvd::INSTANCE_STATE_STOPPED);
  instance_db_.UpdateInstanceGroup(group);
  for (const auto& instance : group.Instances()) {
    auto lock = lock_manager_.TryAcquireLock(instance.id());
    if (lock.ok() && (*lock)) {
      (*lock)->Status(InUseState::kNotInUse);
      continue;
    }
    std::cerr << "InstanceLockFileManager failed to acquire lock";
  }
  return {};
}

cvd::Status InstanceManager::CvdClear(const CommandRequest& request) {
  cvd::Status status;
  const std::string config_json_name = android::base::Basename(GetGlobalConfigFileLink());
  auto instance_groups_res = instance_db_.Clear();
  if (!instance_groups_res.ok()) {
    fmt::print(std::cerr, "Failed to clear instance database: {}",
               instance_groups_res.error().Message());
    status.set_code(cvd::Status::INTERNAL);
    return status;
  }
  auto instance_groups = *instance_groups_res;
  for (auto& group : instance_groups) {
    // Only stop running instances.
    if (group.HasActiveInstances()) {
      auto config_path = GetCuttlefishConfigPath(group.HomeDir());
      if (config_path.ok()) {
        auto stop_result = IssueStopCommand(request, *config_path, group);
        if (!stop_result.ok()) {
          LOG(ERROR) << stop_result.error().FormatForEnv();
        }
      }
    }
    for (auto instance : group.Instances()) {
      if (instance.id() <= 0) {
        continue;
      }
      auto res = lock_manager_.RemoveLockFile(instance.id());
      if (!res.ok()) {
        LOG(ERROR) << "Failed to remove lock file for instance: "
                   << res.error().FormatForEnv();
      }
    }
    RemoveFile(group.HomeDir() + "/cuttlefish_runtime");
    RemoveFile(group.HomeDir() + config_json_name);
    RemoveGroupDirectory(group);
  }
  // TODO(kwstephenkim): we need a better mechanism to make sure that
  // we clear all run_cvd processes.
  std::cerr << "Stopped all known instances\n";
  status.set_code(cvd::Status::OK);
  return status;
}

Result<std::optional<InstanceLockFile>> InstanceManager::TryAcquireLock(
    int instance_num) {
  return CF_EXPECT(lock_manager_.TryAcquireLock(instance_num));
}

Result<std::vector<LocalInstanceGroup>> InstanceManager::FindGroups(
    const InstanceDatabase::Filter& filter) const {
  return instance_db_.FindGroups(filter);
}

Result<LocalInstanceGroup> InstanceManager::FindGroup(
    const InstanceDatabase::Filter& filter) const {
  auto output = CF_EXPECT(instance_db_.FindGroups(filter));
  CF_EXPECT_EQ(output.size(), 1ul);
  return *(output.begin());
}

}  // namespace cuttlefish
