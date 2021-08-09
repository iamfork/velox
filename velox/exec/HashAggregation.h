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

#include "velox/exec/Aggregate.h"
#include "velox/exec/HashTable.h"
#include "velox/exec/Operator.h"
#include "velox/exec/VectorHasher.h"

namespace facebook::velox::exec {

class GroupingSet {
 public:
  GroupingSet(
      std::vector<std::unique_ptr<VectorHasher>>&& hashers,
      std::vector<std::unique_ptr<Aggregate>>&& aggregates,
      std::vector<std::vector<ChannelIndex>>&& channelLists,
      std::vector<std::vector<VectorPtr>>&& constantLists,
      bool ignoreNullKeys,
      OperatorCtx* driverCtx);

  void addInput(RowVectorPtr input, bool mayPushdown);

  bool getOutput(
      int32_t batchSize,
      bool isPartial,
      RowContainerIterator* iterator,
      RowVectorPtr result);

  uint64_t allocatedBytes() const;

  void resetPartial();

  const HashLookup& hashLookup() const;

 private:
  void initializeGlobalAggregation();

  void populateTempVectors(int32_t aggregateIndex, const RowVectorPtr& input);

  std::vector<ChannelIndex> keyChannels_;
  std::vector<std::unique_ptr<VectorHasher>> hashers_;
  const bool isGlobal_;
  std::vector<std::unique_ptr<Aggregate>> aggregates_;
  // Argument list for the corresponding element of 'aggregates_'.
  const std::vector<std::vector<ChannelIndex>> channelLists_;
  // Constant arguments to aggregates. Corresponds pairwise to
  // 'channelLists_'. This is used when channelLists_[i][j] ==
  // kConstantChannel.
  const std::vector<std::vector<VectorPtr>> constantLists_;
  const bool ignoreNullKeys_;
  DriverCtx* const driverCtx_;
  memory::MappedMemory* const mappedMemory_;

  std::vector<bool> mayPushdown_;

  // Place for the arguments of the aggregate being updated.
  std::vector<VectorPtr> tempVectors_;
  std::unique_ptr<BaseHashTable> table_;
  std::unique_ptr<HashLookup> lookup_;
  uint64_t numAdded_ = 0;
  SelectivityVector activeRows_;

  // Used to allocate memory for a single row accumulating results of global
  // aggregation
  HashStringAllocator stringAllocator_;
  AllocationPool rows_;
  const bool isAdaptive_;
};

class HashAggregation : public Operator {
 public:
  HashAggregation(
      int32_t operatorId,
      DriverCtx* driverCtx,
      std::shared_ptr<const core::AggregationNode> aggregationNode);

  void addInput(RowVectorPtr input) override;

  RowVectorPtr getOutput() override;

  bool needsInput() const override {
    return !isFinishing_ && !partialFull_;
  }

  void finish() override {
    Operator::finish();
  }

  BlockingReason isBlocked(ContinueFuture* /* unused */) override {
    return BlockingReason::kNotBlocked;
  }

  void close() override {}

 private:
  static constexpr int32_t kOutputBatchSize = 10'000;

  std::unique_ptr<GroupingSet> groupingSet_;
  const bool isPartialOutput_;
  const bool isDistinct_;
  const bool isGlobal_;
  const int64_t maxPartialAggregationMemoryUsage_;
  bool partialFull_ = false;
  bool newDistincts_ = false;
  bool finished_ = false;
  RowContainerIterator resultIterator_;
  bool pushdownChecked_ = false;
  bool mayPushdown_ = false;
};

} // namespace facebook::velox::exec