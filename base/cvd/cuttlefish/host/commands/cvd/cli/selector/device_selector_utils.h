/*
 * Copyright (C) 2023 The Android Open Source Project
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

/**
 * @file Utils shared by device selectors for non-start operations
 *
 */

#include <sys/types.h>

#include <string>
#include <optional>

#include "cuttlefish/host/commands/cvd/cli/types.h"

namespace cuttlefish {
namespace selector {

std::optional<std::string> OverridenHomeDirectory(const cvd_common::Envs& env);

}  // namespace selector
}  // namespace cuttlefish
