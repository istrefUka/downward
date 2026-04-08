#include "state_registry.h"

#include "per_state_information.h"
#include "task_proxy.h"

#include "task_utils/task_properties.h"
#include "utils/logging.h"

using namespace std;

StateRegistry::StateRegistry(const TaskProxy &task_proxy)
    : task_proxy(task_proxy),
      state_packer(task_properties::g_state_packers[task_proxy]),
      axiom_evaluator(g_axiom_evaluators[task_proxy]),
      num_variables(task_proxy.get_variables().size()),
      state_data_pool(get_bins_per_state()),
      registered_states(
          StateIDSemanticHash(state_data_pool, get_bins_per_state()),
          StateIDSemanticEqual(state_data_pool, get_bins_per_state())) {
}

StateID StateRegistry::insert_id_or_pop_state() {
    /*
      Attempt to insert a StateID for the last state of state_data_pool
      if none is present yet. If this fails (another entry for this state
      is present), we have to remove the duplicate entry from the
      state data pool.
    */
    StateID id(state_data_pool.size() - 1);
    pair<int, bool> result = registered_states.insert(id.value);
    bool is_new_entry = result.second;
    if (!is_new_entry) {
        state_data_pool.pop_back();
    }
    assert(
        registered_states.size() == static_cast<int>(state_data_pool.size()));
    return StateID(result.first);
}

//TODO: löschen falls nicht mehr gebraucht.
State StateRegistry::lookup_state(StateID id) const {
    const PackedStateBin *buffer = state_data_pool[id.value];
    return task_proxy.create_state(*this, id, buffer);
}
//TODO: create here a compressed State (need to pass on parent and effs)
State StateRegistry::lookup_state(
StateID id, std::shared_ptr<State> &parent_state, std::shared_ptr<std::vector<std::tuple<int, int>>> &effs, const PackedStateBin *buffer) const {
    return task_proxy.create_state(*this, id, parent_state, effs, buffer);
}

const State &StateRegistry::get_initial_state() {
    if (!cached_initial_state) {
        int num_bins = get_bins_per_state();
        unique_ptr<PackedStateBin[]> buffer(new PackedStateBin[num_bins]);
        // Avoid garbage values in half-full bins.
        fill_n(buffer.get(), num_bins, 0);

        State initial_state = task_proxy.get_initial_state();
        for (size_t i = 0; i < initial_state.size(); ++i) {
            state_packer.set(buffer.get(), i, initial_state[i].get_value());
        }
        state_data_pool.push_back(buffer.get());
        StateID id = insert_id_or_pop_state();
        cached_initial_state = make_unique<State>(lookup_state(id));
    }
    return *cached_initial_state;
}

// TODO it would be nice to move the actual state creation (and operator
// application)
//      out of the StateRegistry. This could for example be done by global
//      functions operating on state buffers (PackedStateBin *).
//TODO: lookup state anschauen und anschauen was machen wegen unpack, da Lösung wie in successor_generator nicht ganz optimal ist.
//TODO: Nur delta state kreieren, falls sich lohnt.
State StateRegistry::get_successor_state(
    const State &predecessor, const OperatorProxy &op) {
    assert(!op.is_axiom());
    /*
      TODO: ideally, we would not modify state_data_pool here and in
      insert_id_or_pop_state, but only at one place, to avoid errors like
      buffer becoming a dangling pointer. This used to be a bug before being
      fixed in https://issues.fast-downward.org/issue1115.
    */
    //TODO: diese Zeile problematisch, da buffer nicht enthalten in compressed states.
    state_data_pool.push_back(predecessor.get_buffer());
    PackedStateBin *buffer = state_data_pool[state_data_pool.size() - 1];

    predecessor.unpack();
    vector<int> new_values = predecessor.get_unpacked_values();
    auto effs = std::make_shared<std::vector<std::tuple<int, int>>>();
    for (EffectProxy effect : op.get_effects()) {
        if (does_fire(effect, predecessor)) {
            FactPair effect_pair = effect.get_fact().get_pair();
            effs->emplace_back(effect_pair.var, effect_pair.value);
            new_values[effect_pair.var] = effect_pair.value;
        }
    }
    axiom_evaluator.evaluate(new_values);
    for (size_t i = 0; i < new_values.size(); ++i) {
        state_packer.set(buffer, i, new_values[i]);
    }
    /*
      NOTE: insert_id_or_pop_state possibly invalidates buffer, hence
      we use lookup_state to retrieve the state using the correct buffer.
    */
    StateID id = insert_id_or_pop_state();
    auto predecessor_ptr = std::make_shared<State>(predecessor);
    if (predecessor.get_is_delta()) {
        predecessor.set_values_to_null();
    }
    return lookup_state(id, predecessor_ptr,  effs, buffer);
}

int StateRegistry::get_bins_per_state() const {
    return state_packer.get_num_bins();
}

int StateRegistry::get_state_size_in_bytes() const {
    return get_bins_per_state() * sizeof(PackedStateBin);
}

void StateRegistry::print_statistics(utils::LogProxy &log) const {
    log << "Number of registered states: " << size() << endl;
    registered_states.print_statistics(log);
}
