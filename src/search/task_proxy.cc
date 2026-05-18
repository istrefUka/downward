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
    StateID &parent_state, std::vector<std::tuple<int, int>> &effs, const PackedStateBin *buffer)
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
    const StateID &parent_state, const std::vector<std::tuple<int, int>> &effs, const PackedStateBin *buffer)
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
    const StateID &parent_state, const std::vector<std::tuple<int, int>> &effs,
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


//TODO: calculate effs -1
std::shared_ptr<std::vector<int>> State::create_variables_from_delta() const {
    if (parent_state == StateID::no_state) {
        throw std::runtime_error("Already in root node because no parent_state exists!");
    }

    State current = *this;

    std::vector<std::tuple<int, int>> effects;
    std::unordered_set<int> seen_indices;

    effects.reserve(current.effs.size());
    seen_indices.reserve(current.effs.size());

    auto add_effects_if_missing = [&](const std::vector<std::tuple<int, int>>& effs) {
        for (const auto& eff : effs) {
            const int idx = std::get<0>(eff);

            if (seen_indices.insert(idx).second) {
                effects.push_back(eff);
            }
        }
    };

    add_effects_if_missing(current.effs);

    while (current.is_delta) {
        current = const_cast<StateRegistry*>(current.get_registry())
                      ->lookup_state_delta(current.parent_state);

        add_effects_if_missing(current.effs);
    }

    if (!current.is_delta) {
        current.fill_variables();

        auto calculated_values =
            std::make_shared<std::vector<int>>(*current.values);

        for (const auto& [idx, value] : effects) {
            (*calculated_values)[idx - 1] = value;
        }

        return calculated_values;
    }

    throw std::runtime_error("No Initial state found in order to reconstruct state!");
}