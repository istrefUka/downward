#include "task_proxy.h"

#include "axioms.h"
#include "state_registry.h"

#include "task_utils/causal_graph.h"
#include "task_utils/task_properties.h"

#include <iostream>

using namespace std;

State::State(
    const AbstractTask &task, const StateRegistry &registry, StateID id,
    const PackedStateBin *buffer)
    : task(&task),
      registry(&registry),
      id(id),
      buffer(buffer),
      values(nullptr),
      state_packer(&registry.get_state_packer()),
      num_variables(registry.get_num_variables()) {
    assert(id != StateID::no_state);
    assert(buffer);
    assert(num_variables == task.get_num_variables());
    is_delta = false;
    std::cout << "create wrong state: 4" << std::endl;
}

State::State(
    const AbstractTask &task, const StateRegistry &registry, StateID id,
    const PackedStateBin *buffer, vector<int> &&values)
    : State(task, registry, id, buffer) {
    assert(num_variables == static_cast<int>(values.size()));
    this->values = make_shared<vector<int>>(move(values));
    is_delta = false;
    std::cout << "create wrong state: 5 " << std::endl;
}

State::State(const AbstractTask &task, vector<int> &&values)
    : task(&task),
      registry(nullptr),
      id(StateID::no_state),
      buffer(nullptr),
      values(make_shared<vector<int>>(move(values))),
      state_packer(nullptr),
      num_variables(this->values->size()) {
    assert(num_variables == task.get_num_variables());
    is_delta = false;
    std::cout << "create wrong state: 2" << std::endl;
}

//TODO: How to initialize num_variables?
State::State(
    const AbstractTask &task, const StateRegistry &registry, StateID id,
    std::shared_ptr<State> &parent_state, std::shared_ptr<std::vector<std::tuple<int, int>>> &effs, const PackedStateBin *buffer)
    : parent_state(parent_state),
      effs(effs),
      task(&task),
      registry(&registry),
      id(id),
      buffer(buffer),
      values(nullptr),
      state_packer(nullptr),
      num_variables(0) {
    assert(id != StateID::no_state);
    is_delta = true;
    std::cout << "is_delta: " << is_delta << std::endl;
    std::cout << "parent_state: " << parent_state << std::endl;
}
State::State(
    const AbstractTask &task, const StateRegistry &registry, StateID id,
    const std::shared_ptr<State> &parent_state, const std::shared_ptr<std::vector<std::tuple<int, int>>> &effs, const PackedStateBin *buffer)
    : parent_state(parent_state),
      effs(effs),
      task(&task),
      registry(&registry),
      id(id),
      buffer(buffer),
      values(nullptr),
      state_packer(nullptr),
      num_variables(0) {
    assert(id != StateID::no_state);
    is_delta = true;
    std::cout << "is_delta: " << is_delta << std::endl;
    std::cout << "parent_state: " << parent_state << std::endl;
}

State::State(
    const AbstractTask &task, const StateRegistry &registry, StateID id,
    const std::shared_ptr<State> &parent_state, const std::shared_ptr<std::vector<std::tuple<int, int>>> &effs,
    const PackedStateBin *buffer, vector<int> &&values)
    : parent_state(parent_state),
      effs(effs),
      task(&task),
      registry(&registry),
      id(id),
      buffer(buffer),
      state_packer(nullptr),
      values(make_shared<vector<int>>(move(values))),
      num_variables(0) {
    assert(id != StateID::no_state);
    is_delta = true;
    std::cout << "is_delta: " << is_delta << std::endl;
    std::cout << "parent_state: " << parent_state << std::endl;
}

State State::get_unregistered_successor(const OperatorProxy &op) const {
    assert(!op.is_axiom());
    assert(task_properties::is_applicable(op, *this));
    assert(values);
    vector<int> new_values = get_unpacked_values();

    for (EffectProxy effect : op.get_effects()) {
        if (does_fire(effect, *this)) {
            FactPair effect_fact = effect.get_fact().get_pair();
            new_values[effect_fact.var] = effect_fact.value;
        }
    }

    if (task->get_num_axioms() > 0) {
        AxiomEvaluator &axiom_evaluator = g_axiom_evaluators[TaskProxy(*task)];
        axiom_evaluator.evaluate(new_values);
    }
    return State(*task, move(new_values));
}

const causal_graph::CausalGraph &TaskProxy::get_causal_graph() const {
    return causal_graph::get_causal_graph(task);
}
