#include "delta_state_table.h"

InsertResult DeltaStateTable::insert(int_hash_set::HashType hash, StateID id) {
    auto &bucket = table[hash];

    if (bucket.empty()) {
        bucket.push_back(id);
        return {&bucket, true};
    }

    return {&bucket, false};
}
void DeltaStateTable::insert_force(int_hash_set::HashType hash, StateID id) {
    auto &bucket = table[hash];
    bucket.push_back(id);
}

std::unordered_map<int_hash_set::HashType, std::vector<StateID>> DeltaStateTable::get_table() {
    return table;
}