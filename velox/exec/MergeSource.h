/*
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include "velox/exec/Driver.h"

namespace facebook::velox::exec {

class MergeExchange;

class MergeSource {
 public:
  virtual ~MergeSource() {}
  virtual BlockingReason next(ContinueFuture* future, char** row) = 0;
  virtual BlockingReason enqueue(
      RowVectorPtr input,
      ContinueFuture* future) = 0;

  // Factory methods to create MergeSources.
  static std::shared_ptr<MergeSource> createLocalMergeSource(
      const std::shared_ptr<const RowType>& rowType,
      memory::MappedMemory* mappedMemory);
  static std::shared_ptr<MergeSource> createMergeExchangeSource(
      MergeExchange* mergeExchange,
      const std::string& taskId);
};

} // namespace facebook::velox::exec