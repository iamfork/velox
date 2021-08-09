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
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/futures/Future.h>
#include "velox/connectors/Connector.h"
#include "velox/core/PlanNode.h"
#include "velox/core/QueryCtx.h"

namespace facebook::velox::exec {

class Driver;
class ExchangeClient;
class Operator;
struct OperatorStats;
class Task;

enum class BlockingReason {
  kNotBlocked,
  kWaitForConsumer,
  kWaitForSplit,
  kWaitForExchange,
  kWaitForJoinBuild
};

using ContinueFuture = folly::SemiFuture<bool>;

class BlockingState {
 public:
  BlockingState(
      std::shared_ptr<Driver> driver,
      ContinueFuture&& future,
      Operator* FOLLY_NONNULL op,
      BlockingReason reason);

  static void setResume(std::shared_ptr<BlockingState> state);

  Operator* FOLLY_NONNULL op() {
    return operator_;
  }

  BlockingReason reason() {
    return reason_;
  }

 private:
  std::shared_ptr<Driver> driver_;
  ContinueFuture future_;
  Operator* FOLLY_NONNULL operator_;
  BlockingReason reason_;
  uint64_t sinceMicros_;
};

struct DriverCtx {
  std::shared_ptr<Task> task;
  std::unique_ptr<core::ExecCtx> execCtx;
  std::unique_ptr<connector::ExpressionEvaluator> expressionEvaluator;
  const int driverId;
  const int pipelineId;
  Driver* FOLLY_NONNULL driver;
  int32_t numDrivers;

  explicit DriverCtx(
      std::shared_ptr<Task> _task,
      int _driverId,
      int _pipelineId,
      int32_t numDrivers);

  velox::memory::MemoryPool* FOLLY_NONNULL addOperatorUserPool() {
    opMemPools_.push_back(execCtx->pool()->addScopedChild("operator_ctx"));
    return opMemPools_.back().get();
  }

  velox::memory::MemoryPool* FOLLY_NONNULL
  addOperatorSystemPool(velox::memory::MemoryPool* FOLLY_NONNULL userPool) {
    auto poolPtr = userPool->addScopedChild("operator_ctx");
    poolPtr->setMemoryUsageTracker(
        userPool->getMemoryUsageTracker()->addChild(true));
    auto pool = poolPtr.get();
    opMemPools_.push_back(std::move(poolPtr));

    return pool;
  }

  std::unique_ptr<connector::ConnectorQueryCtx> createConnectorQueryCtx(
      const std::string& connectorId) const;

 private:
  // Lifetime of operator memory pools is same as the driverCtx, since some
  // buffers allocated within an operator context may still be referenced by
  // other operators.
  std::vector<std::unique_ptr<velox::memory::MemoryPool>> opMemPools_;
};

class Driver {
 public:
  Driver(
      std::unique_ptr<DriverCtx> driverCtx,
      std::vector<std::unique_ptr<Operator>>&& operators);

  ~Driver() {
    close();
  }

  static folly::CPUThreadPoolExecutor* FOLLY_NONNULL
  executor(int32_t threads = 0);

  static void run(std::shared_ptr<Driver> self);

  static void enqueue(std::shared_ptr<Driver> instance);

  // Waits for activity on 'executor_' to finish and then makes a new
  // executor. Testing uses this to ensure that there are no live
  // references to memory pools before deleting the pools.
  static void testingJoinAndReinitializeExecutor(int32_t threads = 0);

  bool isOnThread() const {
    return isOnThread_;
  }

  bool isTerminated() const {
    return isTerminated_;
  }

  // Frees the resources associated with this if this is
  // off-thread. Returns true if resources are freed. If this is on
  // thread, returns false. In this case the Driver's thread will see
  // that the CancelPool is set to terminate and will free the
  // resources on the thread.
  bool terminate();

  void initializeOperatorStats(std::vector<OperatorStats>& stats);

  void addStatsToTask();

  // Returns true if all operators between the source and 'aggregation' are
  // order-preserving and do not increase cardinality.
  bool mayPushdownAggregation(Operator* FOLLY_NONNULL aggregation);

  // Returns the Operator with 'planNodeId.' or nullptr if not
  // found. For example, hash join probe accesses the corresponding
  // build by id.
  Operator* FOLLY_NULLABLE findOperator(std::string_view planNodeId) const;

  void setError(std::exception_ptr exception);

  std::string toString();

  DriverCtx* FOLLY_NONNULL driverCtx() const {
    return ctx_.get();
  }

 private:
  core::StopReason runInternal(
      std::shared_ptr<Driver>& self,
      std::shared_ptr<BlockingState>* FOLLY_NONNULL blockingState);

  void close();

  std::unique_ptr<DriverCtx> ctx_;
  std::shared_ptr<Task> task_;
  core::CancelPoolPtr cancelPool_;

  // Set via cancelPool->enter()
  bool isOnThread_ = false;
  bool isTerminated_ = false;

  std::vector<std::unique_ptr<Operator>> operators_;

  BlockingReason blockingReason_{BlockingReason::kNotBlocked};
};

using OperatorSupplier = std::function<std::unique_ptr<Operator>(
    int32_t operatorId,
    DriverCtx* FOLLY_NONNULL ctx)>;

struct DriverFactory {
  std::vector<std::shared_ptr<const core::PlanNode>> planNodes;
  // Function that will generate the final operator of a driver being
  // constructed.
  OperatorSupplier consumerSupplier;
  uint32_t maxDrivers;

  std::shared_ptr<Driver> createDriver(
      std::unique_ptr<DriverCtx> ctx,
      std::shared_ptr<ExchangeClient> exchangeClient,
      std::function<int(int pipelineId)> numDrivers);

  int numDestinations() const {
    VELOX_CHECK(!planNodes.empty());
    if (auto partitionedOutputNode =
            std::dynamic_pointer_cast<const core::PartitionedOutputNode>(
                planNodes.back())) {
      return partitionedOutputNode->numPartitions();
    } else {
      return 0;
    }
  }

  bool needsExchangeClient() const {
    VELOX_CHECK(!planNodes.empty());
    if (auto exchangeNode = std::dynamic_pointer_cast<const core::ExchangeNode>(
            planNodes.front())) {
      return true;
    }

    return false;
  }

  /// Returns LocalPartition plan node ID if the pipeline gets data from a local
  /// exchange.
  std::optional<core::PlanNodeId> needsLocalExchangeSource() const {
    VELOX_CHECK(!planNodes.empty());
    if (auto exchangeNode =
            std::dynamic_pointer_cast<const core::LocalPartitionNode>(
                planNodes.front())) {
      return exchangeNode->id();
    }

    return std::nullopt;
  }
};

} // namespace facebook::velox::exec