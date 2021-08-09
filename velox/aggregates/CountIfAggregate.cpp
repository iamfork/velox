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

#include "velox/aggregates/AggregateNames.h"
#include "velox/exec/Aggregate.h"
#include "velox/vector/DecodedVector.h"
#include "velox/vector/FlatVector.h"
#include "velox/vector/SimpleVector.h"

namespace facebook::velox::aggregate {

class CountIfAggregate : public exec::Aggregate {
 public:
  explicit CountIfAggregate(core::AggregationNode::Step step)
      : exec::Aggregate(step, BIGINT()) {}

  int32_t accumulatorFixedWidthSize() const override {
    return sizeof(int64_t);
  }

  void initializeNewGroups(
      char** groups,
      folly::Range<const vector_size_t*> indices) override {
    for (auto i : indices) {
      *value<int64_t>(groups[i]) = 0;
    }
  }

  void finalize(char** /* groups */, int32_t /* numGroups */) override {}

  void extractAccumulators(char** groups, int32_t numGroups, VectorPtr* result)
      override {
    extractValues(groups, numGroups, result);
  }

  void extractValues(char** groups, int32_t numGroups, VectorPtr* result)
      override {
    auto* vector = (*result)->as<FlatVector<int64_t>>();
    VELOX_CHECK(vector);
    vector->resize(numGroups);

    auto* rawValues = vector->mutableRawValues();
    for (vector_size_t i = 0; i < numGroups; ++i) {
      rawValues[i] = *value<int64_t>(groups[i]);
    }
  }

  void updatePartial(
      char** groups,
      const SelectivityVector& rows,
      const std::vector<VectorPtr>& args,
      bool /*mayPushdown*/) override {
    DecodedVector decoded(*args[0], rows);

    if (decoded.isConstantMapping()) {
      if (decoded.isNullAt(0)) {
        return;
      }
      if (decoded.valueAt<bool>(0)) {
        rows.applyToSelected(
            [&](vector_size_t i) { addToGroup(groups[i], 1); });
      }
    } else if (decoded.mayHaveNulls()) {
      rows.applyToSelected([&](vector_size_t i) {
        if (decoded.isNullAt(i)) {
          return;
        }
        if (decoded.valueAt<bool>(i)) {
          addToGroup(groups[i], 1);
        }
      });
    } else {
      rows.applyToSelected([&](vector_size_t i) {
        if (decoded.valueAt<bool>(i)) {
          addToGroup(groups[i], 1);
        }
      });
    }
  }

  void updateFinal(
      char** groups,
      const SelectivityVector& rows,
      const std::vector<VectorPtr>& args,
      bool /*mayPushdown*/) override {
    DecodedVector decoded(*args[0], rows);

    if (decoded.isConstantMapping()) {
      auto numTrue = decoded.valueAt<int64_t>(0);
      rows.applyToSelected(
          [&](vector_size_t i) { addToGroup(groups[i], numTrue); });
      return;
    }

    rows.applyToSelected([&](vector_size_t i) {
      auto numTrue = decoded.valueAt<int64_t>(i);
      addToGroup(groups[i], numTrue);
    });
  }

  void updateSingleGroupPartial(
      char* group,
      const SelectivityVector& allRows,
      const std::vector<VectorPtr>& args,
      bool /*mayPushdown*/) override {
    DecodedVector decoded(*args[0], allRows);

    if (decoded.isConstantMapping()) {
      if (!decoded.isNullAt(0)) {
        auto isTrue = decoded.valueAt<bool>(0);
        if (isTrue) {
          addToGroup(group, allRows.size());
        }
      }
      return;
    }

    if (!decoded.mayHaveNulls()) {
      addToGroup(group, allRows.size());
      return;
    }

    int64_t numTrue = countTrue(allRows, decoded);
    addToGroup(group, numTrue);
  }

  void updateSingleGroupFinal(
      char* group,
      const SelectivityVector& allRows,
      const std::vector<VectorPtr>& args,
      bool /*mayPushdown*/) override {
    VELOX_CHECK_EQ(1, args[0]->size());
    auto numTrue = args[0]->as<SimpleVector<int64_t>>()->valueAt(0);
    addToGroup(group, numTrue);
  }

 private:
  inline void addToGroup(char* group, int64_t numTrue) {
    *value<int64_t>(group) += numTrue;
  }

  inline int64_t countTrue(
      const SelectivityVector& allRows,
      DecodedVector& decoded) {
    int64_t numTrue = 0;
    for (vector_size_t i = 0; i < allRows.end(); ++i) {
      if (decoded.isNullAt(i)) {
        continue;
      }
      if (decoded.valueAt<bool>(i)) {
        ++numTrue;
      }
    }
    return numTrue;
  }
};

bool registerCountIfAggregate(const std::string& name) {
  exec::AggregateFunctions().Register(
      name,
      [name](
          core::AggregationNode::Step step,
          std::vector<TypePtr> argTypes,
          const TypePtr&
          /*resultType*/) -> std::unique_ptr<exec::Aggregate> {
        VELOX_CHECK_EQ(argTypes.size(), 1, "{} takes one argument", name);

        auto isPartial = exec::isRawInput(step);
        if (isPartial) {
          VELOX_CHECK_EQ(
              argTypes[0]->kind(),
              TypeKind::BOOLEAN,
              "{} function only accepts boolean parameter",
              name);
        }

        return std::make_unique<CountIfAggregate>(step);
      });
  return true;
}

static bool FB_ANONYMOUS_VARIABLE(g_AggregateFunction) =
    registerCountIfAggregate(kCountIf);

} // namespace facebook::velox::aggregate