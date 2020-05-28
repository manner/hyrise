#pragma once

#include <memory>
#include <optional>
#include <queue>
#include <set>
#include <unordered_set>

#include "operators/abstract_operator.hpp"

namespace opossum {

enum class PQPVisitation { VisitInputs, DoNotVisitInputs };

/**
 * Calls the passed @param visitor on @param pqp and recursively on its INPUTS.
 * The visitor returns `PQPVisitation`, indicating whether the current nodes's input should be visited
 * as well. The algorithm is breadth-first search.
 * Each node is visited exactly once.
 *
 * @tparam Visitor      Functor called with every node as a param.
 *                      Returns `PQPVisitation`
 */
template <typename Operator, typename Visitor>
void visit_pqp(const std::shared_ptr<Operator>& lqp, Visitor visitor) {
  using AbstractOperatorType = std::conditional_t<std::is_const_v<Operator>, const AbstractOperator, AbstractOperator>;

  std::queue<std::shared_ptr<AbstractOperatorType>> operator_queue;
  operator_queue.push(lqp);

  std::unordered_set<std::shared_ptr<AbstractOperatorType>> visited_operators;

  while (!operator_queue.empty()) {
    auto op = operator_queue.front();
    operator_queue.pop();

    if (!visited_operators.emplace(op).second) continue;

    if (visitor(op) == PQPVisitation::VisitInputs) {
      if (op->input_left()) operator_queue.push(op->input_left());
      if (op->input_right()) operator_queue.push(op->input_right());
    }
  }
}

}  // namespace opossum