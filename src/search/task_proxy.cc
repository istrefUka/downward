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
      parent_state(StateID::no_state),
      id(id),
      buffer(buffer),
      values(nullptr),
      state_packer(&registry.get_state_packer()),
      num_variables(registry.get_num_variables()) {
    assert(id != StateID::no_state);
    assert(buffer);
    assert(num_variables == task.get_num_variables());
    is_delta = false;
}

State::State(
    const AbstractTask &task, const StateRegistry &registry, StateID id,
    const PackedStateBin *buffer, vector<int> &&values)
    : State(task, registry, id, buffer) {
    assert(num_variables == static_cast<int>(values.size()));
    this->values = make_shared<vector<int>>(move(values));
    is_delta = false;
}

State::State(const AbstractTask &task, vector<int> &&values)
    : task(&task),
      registry(nullptr),
      parent_state(StateID::no_state),
      id(StateID::no_state),
      buffer(nullptr),
      values(make_shared<vector<int>>(move(values))),
      state_packer(nullptr),
      num_variables(this->values->size()) {
    assert(num_variables == task.get_num_variables());
    is_delta = false;
}

State::State(
    const AbstractTask &task, const StateRegistry &registry, StateID id,
    StateID &parent_state, std::shared_ptr<std::vector<std::tuple<int, int>>> &effs, const PackedStateBin *buffer)
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
}
State::State(
    const AbstractTask &task, const StateRegistry &registry, StateID id,
    const StateID &parent_state, const std::shared_ptr<std::vector<std::tuple<int, int>>> &effs, const PackedStateBin *buffer)
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
}

State::State(
    const AbstractTask &task, const StateRegistry &registry, StateID id,
    const StateID &parent_state, const std::shared_ptr<std::vector<std::tuple<int, int>>> &effs,
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

std::size_t State::memory_estimate_bytes() const {
    std::size_t size = sizeof(State);

    /*
      task, registry, buffer, and state_packer are raw pointers.
      Their pointer values are already included in sizeof(State).
      The objects they point to are not counted because State does not own them.
    */

    if (values) {
        size += sizeof(std::vector<int>);
        size += values->capacity() * sizeof(int);
    }

    if (effs) {
        using Effect = std::tuple<int, int>;

        size += sizeof(std::vector<Effect>);
        size += effs->capacity() * sizeof(Effect);
    }

    return size;
}

std::shared_ptr<std::vector<int>> State::create_variables_from_delta() const{
    //TODO: was wenn parent_state nicht existiert?
    if (parent_state == StateID::no_state ) {
        throw std::runtime_error("Already in root node because no parent_state exists!");
    }
    std::stack<std::shared_ptr<std::vector<std::tuple<int, int>>>> effs_stack;
    State current = *this;
    std::shared_ptr<std::vector<int>> calculated_values;

    while (current.is_delta) {
        effs_stack.push(current.effs);
        current = const_cast<StateRegistry*>(current.get_registry())->lookup_state_delta(current.parent_state);
    }
    //Case for Root State
    if (!current.is_delta) {
        current.fill_variables();
        calculated_values = std::make_shared<std::vector<int>>(*current.values);
        std::shared_ptr<std::vector<std::tuple<int, int>>> current_eff;
        while (!effs_stack.empty()) {
            current_eff = effs_stack.top();
            effs_stack.pop();
            for (const auto &[idx, value] : *current_eff) {
                (*calculated_values)[idx] = value;
            }

        }
        return calculated_values;
    }
    throw std::runtime_error("No Initial state found in order to reconstruct state!");

}