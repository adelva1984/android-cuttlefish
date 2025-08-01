/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include <stdint.h>

#include "cuttlefish/common/libs/utils/result.h"

namespace cuttlefish {

class DiskImage {
 public:
  virtual ~DiskImage() = default;
  // Avoid slicing a subclass by using the wrong assignment operator.
  DiskImage& operator=(DiskImage&&) = delete;

  /* The size (in bytes) of the disk when mounted inside a virtual machine. */
  virtual Result<uint64_t> VirtualSizeBytes() const = 0;
};

}  // namespace cuttlefish
