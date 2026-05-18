#include "state_registry.h"
#include "per_state_information.h"
#include "task_proxy.h"

#include "task_utils/task_properties.h"
#include "utils/logging.h"

using namespace std;
//TODO: initialize delta_packer
StateRegistry::StateRegistry(const TaskProxy &task_proxy)
    : task_proxy(task_proxy),
      state_packer(task_properties::g_state_packers[task_proxy]),
      delta_packer(state_packer.get_ranges()),
      axiom_evaluator(g_axiom_evaluators[task_proxy]),
      num_variables(task_proxy.get_variables().size()),
      state_data_pool(get_bins_per_state()),
      delta_state_data_pool(),
      registered_states(
          StateIDSemanticHash(state_data_pool, get_bins_per_state()),
          StateIDSemanticEqual(state_data_pool, get_bins_per_state())),
          registered_delta_states()
{
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

//TODO: registered states arbeitet nicht mit delta_state_data_pool
StateID StateRegistry::insert_id_or_pop_delta_state() {
    /*
      Attempt to insert a StateID for the last state of delta_state_data_pool
      if none is present yet. If this fails (another entry for this state
      is present), we have to remove the duplicate entry from the
      state data pool.
    */
    StateID id(delta_state_data_pool.size() - 1);
    pair<int, bool> result = registered_states.insert(id.value);
    bool is_new_entry = result.second;
    if (!is_new_entry) {
        delta_state_data_pool.pop_back();
    }
    assert(
        registered_states.size() == static_cast<int>(state_data_pool.size()));
    return StateID(result.first);
}

//TODO: löschen falls nicht mehr gebraucht.
State StateRegistry::lookup_state(StateID id) const{
    const PackedStateBin *buffer = nullptr;
    if (id.value >= state_data_pool.size()) {
        std::cout << "need delta_lookup" << std::endl;
        PackedStateBin *buff = nullptr;
        DeltaStateInfo delta = delta_state_data_pool[id.value];
        if (delta.parent_state == StateID::no_state) {
            std::cout << "lookup_state_delta no parent_state for id: " << id.value << std::endl;
        }
        std::vector<std::tuple<int, int>> effs = delta_packer.get_buffer(delta_state_data_pool, id.value);
        State s = task_proxy.create_delta_state(*this, id, delta.parent_state, effs, buffer);
        s.unpack();
        std::vector<int> new_values = s.get_unpacked_values();
        for (size_t i = 0; i < new_values.size(); ++i) {
            state_packer.set(buff, i, new_values[i]);
        }
        return task_proxy.create_state(*this, id, buffer);
    }
    buffer = state_data_pool[id.value];
    if (!buffer) {
        std::cout << "buffer is null!" <<std::endl;
    }
    return task_proxy.create_state(*this, id, buffer);
}

State StateRegistry::lookup_state_delta(StateID id) {
    PackedStateBin *buffer = nullptr;

    //TODO: was machen, da Root node nicht in delta_state_data_pool ist?
    //TODO: state_data_pool verbraucht zu viel speicher
    if (id.value == 0) {
        buffer = state_data_pool[id.value];
        return task_proxy.create_state(*this, id, buffer);
    }


    DeltaStateInfo delta = delta_state_data_pool[id.value];
    std::vector<std::tuple<int, int>> effs = delta_packer.get_buffer(delta_state_data_pool, id.value);
    return task_proxy.create_delta_state(*this, id, delta.parent_state, effs, buffer);
}

State StateRegistry::lookup_state_delta(
StateID id, StateID parent_state, std::vector<std::tuple<int, int>> effs, const PackedStateBin *buffer) const {
    return task_proxy.create_delta_state(*this, id, parent_state, effs, buffer);
}
State StateRegistry::lookup_state(
    StateID id, vector<int> &&state_values) const {
    const PackedStateBin *buffer = state_data_pool[id.value];
    return task_proxy.create_state(*this, id, buffer, move(state_values));
}

//TODO: compute hash from new_values
int_hash_set::HashType compute_hash(const vector<int> &new_values) {
    utils::HashState hash_state;
    for (int value : new_values) {
        hash_state.feed(value);
    }
    return hash_state.get_hash32();
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
        delta_packer.set_effs_range(initial_state.size() + 1);
        auto effs = std::vector<std::tuple<int, int>>();
        StateID parent_state(StateID::no_state);
        PackedStateBin buffer_delta = -1;
        DeltaStateInfo new_delta = {buffer_delta, parent_state};
        delta_state_data_pool.push_back(new_delta);
        state_data_pool.push_back(buffer.get());
        StateID id = insert_id_or_pop_state();
        cached_initial_state = make_unique<State>(lookup_state(id));
        cached_initial_state->unpack();
        int_hash_set::HashType hash = compute_hash(cached_initial_state->get_unpacked_values());
        registered_delta_states.insert(hash, id);
    }
    return *cached_initial_state;
}


//TODO: effs+1
State StateRegistry::get_successor_state_delta(
    const State &predecessor, const OperatorProxy &op) {
    assert(!op.is_axiom());
    /*
      TODO: ideally, we would not modify state_data_pool here and in
      insert_id_or_pop_state, but only at one place, to avoid errors like
      buffer becoming a dangling pointer. This used to be a bug before being
      fixed in https://issues.fast-downward.org/issue1115.
    */
    //TODO: diese Zeile problematisch, da buffer nicht enthalten in compressed states.
    PackedStateBin *buffer = nullptr;

    predecessor.unpack();
    vector<int> new_values = predecessor.get_unpacked_values();
    auto effs = std::vector<std::tuple<int, int>>();
    for (EffectProxy effect : op.get_effects()) {
        if (does_fire(effect, predecessor)) {
            FactPair effect_pair = effect.get_fact().get_pair();
            effs.emplace_back((effect_pair.var+1), effect_pair.value);
            new_values[effect_pair.var] = effect_pair.value;
        }
    }
    int_hash_set::HashType hash = compute_hash(new_values);

    //TODO: only create buffer_delta when needed
    std::vector<PackedStateBin> buffer_delta = delta_packer.create_buffer(effs);
    StateID id(delta_state_data_pool.size());
    InsertResult res = registered_delta_states.insert(hash, id);
    bool insert = true;
    if (!res.inserted) {
        for (int i = 0; i < res.bucket->size(); ++i) {
            StateID new_id = res.bucket->at(i);
            State s = lookup_state_delta(new_id);
            s.unpack();
            std::vector<int> unpacked = s.get_unpacked_values();
            if (unpacked == new_values) {
                insert = false;
                id = new_id;
                break;
            }
        }
        if (insert) {
            registered_delta_states.insert_force(hash, id);
            for (int i = 0; i < buffer_delta.size(); ++i) {
                DeltaStateInfo new_delta = {buffer_delta[i], predecessor.get_id()};
                delta_state_data_pool.push_back(new_delta);
            }
            std::vector<std::tuple<int, int>> effs_calc = delta_packer.get_buffer(delta_state_data_pool, id.value);
        }
    }
    if (res.inserted) {
        for (int i = 0; i < buffer_delta.size(); ++i) {
            DeltaStateInfo new_delta = {buffer_delta[i], predecessor.get_id()};
            delta_state_data_pool.push_back(new_delta);
        }
    }

    assert(delta_state_data_pool.size() == registered_delta_states.get_table().size());
    if (predecessor.get_is_delta()) {
      predecessor.set_values_to_null();
    }

    StateID id_parent(predecessor.get_id().value);

    return lookup_state_delta(id, id_parent,  effs, buffer);
}

State StateRegistry::get_successor_state(
    const State &predecessor, const OperatorProxy &op) {
    assert(!op.is_axiom());
    /*
      TODO: ideally, we would not modify state_data_pool here and in
      insert_id_or_pop_state, but only at one place, to avoid errors like
      buffer becoming a dangling pointer. This used to be a bug before being
      fixed in https://issues.fast-downward.org/issue1115.
    */
    state_data_pool.push_back(predecessor.get_buffer());
    PackedStateBin *buffer = state_data_pool[state_data_pool.size() - 1];
    /* Experiments for issue348 showed that for tasks with axioms it's faster
       to compute successor states using unpacked data. */
    if (task_properties::has_axioms(task_proxy)) {
        predecessor.unpack();
        vector<int> new_values = predecessor.get_unpacked_values();
        for (EffectProxy effect : op.get_effects()) {
            if (does_fire(effect, predecessor)) {
                FactPair effect_pair = effect.get_fact().get_pair();
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
        return lookup_state(id, move(new_values));
    } else {
        for (EffectProxy effect : op.get_effects()) {
            if (does_fire(effect, predecessor)) {
                FactPair effect_pair = effect.get_fact().get_pair();
                state_packer.set(buffer, effect_pair.var, effect_pair.value);
            }
        }
        /*
          NOTE: insert_id_or_pop_state possibly invalidates buffer, hence
          we use lookup_state to retrieve the state using the correct buffer.
        */
        StateID id = insert_id_or_pop_state();
        return lookup_state(id);
    }
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

int StateRegistry::registered_states_no() const{
    return state_data_pool.size();
}
void StateRegistry::print_states() const{
    for (int i = 0; i < registered_states_no(); ++i) {
        StateID id(i);
        State s = lookup_state(id);
        s.unpack();
        std::cout << s.get_unpacked_values();
        std::cout << std::endl;
    }
    std::cout << std::endl;
}

int StateRegistry::registered_delta_states_no() const{
    return delta_state_data_pool.size();
}
void StateRegistry::print_delta_states() const{
    for (int i = 0; i < registered_delta_states_no(); ++i) {
        StateID id(i);
        State s = const_cast<StateRegistry*>(this)->lookup_state_delta(id);
        s.unpack();
        std::cout << s.get_unpacked_values();
        std::cout << std::endl;
    }
    std::cout << std::endl;
}

std::size_t StateRegistry::memory_estimate_delta_states() const {
    if (delta_state_data_pool.empty()) {
        return 0;
    }

    using Effect = std::tuple<int, int>;

    std::size_t total = 0;

    for (const DeltaStateInfo &delta_state : delta_state_data_pool) {
        total += sizeof(DeltaStateInfo);
    }

    return total / delta_state_data_pool.size();
}
//TODO:   compute memory size of DeltaStateTable registered_delta_states;
int StateRegistry::get_memory_size_delta_management() const {
    return static_cast<int>(registered_delta_states.memory_estimate());
}

//TODO: compute memory size of const segmented_vector::SegmentedArrayVector<PackedStateBin> &state_data_pool;
int StateRegistry::get_memory_size_states() const {
    return static_cast<int>(state_data_pool.memory_estimate());
}

//TODO: compute memory size of StateIDSet registered_states;
int StateRegistry::get_memory_size_menagement() const {
    return static_cast<int>(registered_states.memory_estimate());
}
