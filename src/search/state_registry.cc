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
State StateRegistry::lookup_state(StateID id) const {
    std::cout<< "in lookup_state: " <<std::endl;
    const PackedStateBin *buffer = nullptr;
    if (id.value >= state_data_pool.size()) {
        std::cout << "need delta_lookup" << std::endl;
        PackedStateBin *buff = nullptr;
        DeltaStateInfo delta = delta_state_data_pool[id.value];
        if (!delta.parent_state) {
            std::cout << "lookup_state_delta no parent_state for id: " << id.value << std::endl;
        }
        std::cout << "deque size: " << delta_state_data_pool.size() << std::endl;
        State s = task_proxy.create_delta_state(*this, id, delta.parent_state, delta.effs, buffer);
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
    std::cout << "in lookup_state_delta for ID: " << id.value << std::endl;
    PackedStateBin *buffer = nullptr;

    //TODO: was machen, da Root node nicht in delta_state_data_pool ist?
    //TODO: state_data_pool verbraucht zu viel speicher
    if (id.value == 0) {
        std::cout << "create initial state lookup" << std::endl;
        buffer = state_data_pool[id.value];
        return task_proxy.create_state(*this, id, buffer);
    }


    DeltaStateInfo delta = delta_state_data_pool[id.value];
    if (!delta.parent_state) {
        std::cout << "lookup_state_delta no parent_state for id: " << id.value << std::endl;
    }
    std::cout << "deque size: " << delta_state_data_pool.size() << std::endl;
    return task_proxy.create_delta_state(*this, id, delta.parent_state, delta.effs, buffer);
}
State StateRegistry::lookup_state(
StateID id, std::shared_ptr<State> &parent_state, std::shared_ptr<std::vector<std::tuple<int, int>>> &effs, const PackedStateBin *buffer) const {
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
    std::cout << "in get_initial_state" << std::endl;
    if (!cached_initial_state) {
        int num_bins = get_bins_per_state();
        unique_ptr<PackedStateBin[]> buffer(new PackedStateBin[num_bins]);
        // Avoid garbage values in half-full bins.
        fill_n(buffer.get(), num_bins, 0);

        State initial_state = task_proxy.get_initial_state();
        for (size_t i = 0; i < initial_state.size(); ++i) {
            state_packer.set(buffer.get(), i, initial_state[i].get_value());
        }
        auto effs = std::make_shared<std::vector<std::tuple<int, int>>>();
        std::shared_ptr<State> predecessor_ptr = nullptr;
        DeltaStateInfo new_delta = {effs, predecessor_ptr};
        delta_state_data_pool.push_back(new_delta);
        std::cout<< "after push in get_initial_state : "<< delta_state_data_pool.size() << std::endl;

        state_data_pool.push_back(buffer.get());
        StateID id = insert_id_or_pop_state();
        cached_initial_state = make_unique<State>(lookup_state(id));
        cached_initial_state->unpack();
        int_hash_set::HashType hash = compute_hash(cached_initial_state->get_unpacked_values());
        registered_delta_states.insert(hash, id);
    }
    return *cached_initial_state;
}

// TODO it would be nice to move the actual state creation (and operator
// application)
//      out of the StateRegistry. This could for example be done by global
//      functions operating on state buffers (PackedStateBin *).
//TODO: lookup state anschauen und anschauen was machen wegen unpack, da Lösung wie in successor_generator nicht ganz optimal ist.
//TODO: Nur delta state kreieren, falls sich lohnt.
//TODO: get_successor_state_ delta and bring back old successor state

State StateRegistry::get_successor_state_delta(
    const State &predecessor, const OperatorProxy &op) {
    cout << "in get successor state" << endl;
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
    std::cout << "predecessor values: ";
    for (int v : new_values) {
        std::cout << v << " ";
    }
    std::cout << std::endl;
    auto effs = std::make_shared<std::vector<std::tuple<int, int>>>();
    for (EffectProxy effect : op.get_effects()) {
        if (does_fire(effect, predecessor)) {
            FactPair effect_pair = effect.get_fact().get_pair();
            effs->emplace_back(effect_pair.var, effect_pair.value);
            new_values[effect_pair.var] = effect_pair.value;
        }
    }
    int_hash_set::HashType hash = compute_hash(new_values);
    std::cout << "new values: ";
    for (int v : new_values) {
        std::cout << v << " ";
    }
    std::cout << std::endl;

    auto predecessor_ptr = std::make_shared<State>(predecessor);

    DeltaStateInfo new_delta{effs, predecessor_ptr};

    StateID id(delta_state_data_pool.size());

    InsertResult res = registered_delta_states.insert(hash, id);
    bool insert = true;
    //bereits vorhanden?
    if (!res.inserted) {
        for (int i = 0; i < res.bucket->size(); ++i) {
            StateID new_id = res.bucket->at(i);
            State s = lookup_state_delta(new_id);
            std::cout << "in !res.inserted" << std::endl;
            s.unpack();
            std::vector<int> unpacked = s.get_unpacked_values();
            if (unpacked == new_values) {
                std::cout << "duplicate detected" << std::endl;
                insert = false;
                id = new_id;
                break;
            }
        }
        if (insert) {
            registered_delta_states.insert_force(hash, id);
            delta_state_data_pool.push_back(new_delta);
        }
    }
    if (res.inserted) {
        delta_state_data_pool.push_back(new_delta);
    }
    assert(delta_state_data_pool.size() == registered_delta_states.get_table().size());
    if (predecessor.get_is_delta()) {
      predecessor.set_values_to_null();
    }
    std::cout << "effects: ";

    for (const auto &[var, value] : *effs) {
        std::cout << "(" << var << " -> " << value << ") ";
    }

    std::cout << std::endl;
    if (!predecessor_ptr) {
        std::cout << "not a valid predecessor_ptr" << std::endl;
    }

    std::cout<< "after push in get_successor_state_delta: "<< delta_state_data_pool.size() << std::endl;
    return lookup_state(id, predecessor_ptr,  effs, buffer);
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
        std::cout << "predecessor values: ";
        for (int v : new_values) {
            std::cout << v << " ";
        }
        for (EffectProxy effect : op.get_effects()) {
            if (does_fire(effect, predecessor)) {
                FactPair effect_pair = effect.get_fact().get_pair();
                new_values[effect_pair.var] = effect_pair.value;
            }
        }
        std::cout << "new values: ";
        for (int v : new_values) {
            std::cout << v << " ";
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
