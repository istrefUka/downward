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

std::unordered_map<int_hash_set::HashType, std::vector<StateID>> & DeltaStateTable::get_table() {
    return table;
}

std::size_t DeltaStateTable::memory_estimate() const {
    std::size_t total = sizeof(*this);

    // Bucket array of unordered_map.
    total += table.bucket_count() * sizeof(void *);

    // Approximation for each hash bucket entry.
    for (const auto &[hash, ids] : table) {
        total += sizeof(hash);
        total += sizeof(std::vector<StateID>);
        total += ids.capacity() * sizeof(StateID);

        // Approximate unordered_map node overhead.
        total += sizeof(void *);
    }

    return total;
}