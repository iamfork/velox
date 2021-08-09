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

#include "folly/json.h"
#include "velox/common/base/ClassName.h"
#include "velox/common/base/Exceptions.h"
#include "velox/common/serialization/Serializable.h"
#include "velox/type/Type.h"

namespace facebook {
namespace velox {
namespace core {

/* an implicitly-typed expression, such as function call, literal, etc... */
class IExpr : public ISerializable {
 public:
  virtual const std::vector<std::shared_ptr<const IExpr>>& getInputs()
      const = 0;

  std::shared_ptr<const IExpr> getInput() const {
    return getInputs().size() == 1 ? getInputs().at(0) : nullptr;
  }

  virtual ~IExpr() = default;

  virtual std::string toString() const = 0;

  virtual std::shared_ptr<const IExpr> withInputs(
      std::vector<std::shared_ptr<const IExpr>> inputs) const = 0;

  virtual bool equalsNonRecursive(const IExpr& other) const {
    // Defaults to false for safety for the general case.
    return false;
  }

 protected:
  static const std::vector<std::shared_ptr<const IExpr>>& EMPTY() {
    static const std::vector<std::shared_ptr<const IExpr>> empty{};
    return empty;
  }
};

} // namespace core
} // namespace velox
} // namespace facebook