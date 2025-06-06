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

#include <algorithm>
#include <string>
#include <vector>

#include "cuttlefish/host/commands/cvd/cli/parser/cf_flags_validator.h"
#include "cuttlefish/host/commands/cvd/cli/parser/launch_cvd_parser.h"
#include "cuttlefish/host/commands/cvd/cli/parser/test_common.h"

namespace cuttlefish {

bool ParseJsonString(std::string& json_text, Json::Value& root) {
  Json::Reader reader;  //  Reader
  return reader.parse(json_text, root);
}

bool FindConfig(const std::vector<std::string>& vec,
                const std::string& element) {
  auto it = find(vec.begin(), vec.end(), element);
  return it != vec.end();
}
bool FindConfigIgnoreSpaces(const std::vector<std::string>& vec,
                            const std::string& str) {
  std::string target = str;
  target.erase(std::remove(target.begin(), target.end(), ' '), target.end());
  target.erase(std::remove(target.begin(), target.end(), '\t'), target.end());

  for (const auto& s : vec) {
    std::string current = s;
    current.erase(std::remove(current.begin(), current.end(), ' '),
                  current.end());
    current.erase(std::remove(current.begin(), current.end(), '\t'),
                  current.end());
    if (current == target) {
      return true;
    }
  }
  return false;
}

Result<std::vector<std::string>> LaunchCvdParserTester(
    const Json::Value& root) {
  auto config = CF_EXPECT(ValidateCfConfigs(root), "Json validation failed");
  return ParseLaunchCvdConfigs(config);
}

}  // namespace cuttlefish
