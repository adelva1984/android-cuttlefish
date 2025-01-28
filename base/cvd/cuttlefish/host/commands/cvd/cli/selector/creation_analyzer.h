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

#pragma once

#include <sys/types.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/libs/utils/result.h"
#include "common/libs/utils/unique_resource_allocator.h"
#include "cuttlefish/host/commands/cvd/instances/cvd_persistent_data.pb.h"
#include "host/commands/cvd/instances/lock/instance_lock.h"
#include "host/commands/cvd/cli/selector/start_selector_parser.h"

namespace cuttlefish {
namespace selector {

struct PerInstanceInfo {
  // for the sake of std::vector::emplace_back
  PerInstanceInfo(const unsigned id, const std::string& per_instance_name,
                  cvd::InstanceState initial_state,
                  InstanceLockFile&& instance_file_lock)
      : instance_id_(id),
        per_instance_name_(per_instance_name),
        initial_state_(initial_state),
        instance_file_lock_(std::move(instance_file_lock)) {}

  PerInstanceInfo(const unsigned id, const std::string& per_instance_name,
                  cvd::InstanceState initial_state)
      : instance_id_(id),
        per_instance_name_(per_instance_name),
        initial_state_(initial_state) {}

  const unsigned instance_id_;
  const std::string per_instance_name_;
  const cvd::InstanceState initial_state_;
  std::optional<InstanceLockFile> instance_file_lock_;
};

/**
 * Creation is currently group by group
 *
 * If you want one instance, you should create a group with one instance.
 */
struct GroupCreationInfo {
  std::string home;
  std::string host_artifacts_path;  ///< e.g. out/host/linux-x86
  // set to host_artifacts_path if no ANDROID_PRODUCT_OUT
  std::string product_out_path;
  std::string group_name;
  std::vector<PerInstanceInfo> instances;
};

/**
 * Instance IDs:
 *  Use the InstanceNumCalculator's logic
 *
 * HOME directory:
 *  If given in envs and is different from the system-wide home, use it
 *  If not, try ParentOfAutogeneratedHomes()/uid/${group_name}
 *
 * host_artifacts_path:
 *  ANDROID_HOST_OUT must be given.
 *
 * Group name:
 *  if a group name is not given, automatically generate:
 *   default_prefix + "_" + one_of_ids
 *
 * Per-instance name:
 *  When not given, use std::string(id) as the per instance name of each
 *
 * Number of instances:
 *  Controlled by --instance_nums, --num_instances, etc.
 *  Also controlled by --instance_name
 *
 * p.s.
 *  dependency: (a-->b means b depends on a)
 *    group_name --> HOME
 *    instance ids --> per_instance_name
 *
 */
class CreationAnalyzer {
 public:
  struct CreationAnalyzerParam {
    const std::vector<std::string>& cmd_args;
    const std::unordered_map<std::string, std::string>& envs;
    const SelectorOptions& selectors;
  };

  struct GroupInfo {
    std::string group_name;
    const bool default_group;
  };

  static Result<CreationAnalyzer> Create(
      const CreationAnalyzerParam& param,
      InstanceLockFileManager& instance_lock_file_manager);

  Result<GroupCreationInfo> ExtractGroupInfo(bool acquire_file_locks);

 private:
  using IdAllocator = UniqueResourceAllocator<unsigned>;

  CreationAnalyzer(const CreationAnalyzerParam& param,
                   StartSelectorParser&& selector_options_parser,
                   InstanceLockFileManager& instance_lock_file_manager);

  /**
   * calculate n_instances_ and instance_ids_
   */
  Result<std::vector<PerInstanceInfo>> AnalyzeInstanceIds(
      bool acquire_file_locks);

  /*
   * When group name is nil, it is auto-generated using instance ids
   *
   * If the instanc group is the default one, the group name is cvd. Otherwise,
   * for given instance ids, {i}, the group name will be cvd_i.
   */
  Result<GroupInfo> ExtractGroup(const std::vector<PerInstanceInfo>&) const;

  /**
   * Figures out the HOME directory
   *
   * The issue is that many times, HOME is anyway implicitly given. Thus, only
   * if the HOME value is not equal to the HOME directory recognized by the
   * system, it can be safely regarded as overridden by the user.
   *
   * If that is not the case, we use a automatically generated value as HOME.
   * If the group instance is the default one, we still use the user's system-
   * widely recognized home. If not, we populate them user /tmp/.cf/<uid>/
   *
   */
  Result<std::string> AnalyzeHome() const;

  Result<std::vector<PerInstanceInfo>> AnalyzeInstanceIdsInternal(
      bool acquire_file_locks);
  Result<std::vector<PerInstanceInfo>> AnalyzeInstanceIdsInternal(
      const std::vector<unsigned>& requested_instance_ids,
      bool acquire_file_locks);

  // inputs
  std::unordered_map<std::string, std::string> envs_;

  // internal, temporary
  StartSelectorParser selector_options_parser_;
  InstanceLockFileManager& instance_lock_file_manager_;
};

}  // namespace selector
}  // namespace cuttlefish
