#include "int_hash_set_delta.h"
#include "../utils/collections.h"
#include "../utils/language.h"
#include "../utils/logging.h"
#include "../utils/system.h"
#include "../state_id.h"
#include "../state_registry.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>
#include <cstddef>

IntHashSetDelta::IntHashSetDelta(
    std::vector<DeltaStateInfo> &delta_state_data_pool,
    DeltaPacker &delta_packer,
    segmented_vector::SegmentedArrayVector<PackedStateBin> &state_data_pool,
    const TaskProxy &task_proxy,
    StateRegistry &registry)
    : delta_state_data_pool(delta_state_data_pool),
      delta_packer(delta_packer),
      state_data_pool(state_data_pool),
      task_proxy(task_proxy),
      registry(registry),
      buckets(1),
      num_entries(0),
      num_resizes(0) {
}

int IntHashSetDelta::capacity() const {
    return buckets.size();
}

bool IntHashSetDelta::equal(KeyType lhs, KeyType rhs) const {
    StateID lhs_id(lhs);
    State lhs_state = registry.lookup_state_delta(lhs_id);
    lhs_state.unpack();
    std::vector<int> lhs_vals = lhs_state.get_unpacked_values();
    StateID rhs_id(rhs);
    State rhs_state = registry.lookup_state_delta(rhs_id);
    rhs_state.unpack();
    std::vector<int> rhs_vals = rhs_state.get_unpacked_values();
    return lhs_vals == rhs_vals;
}

void IntHashSetDelta::rehash(int new_capacity) {
    assert(new_capacity >= 1);
    int num_entries_before = num_entries;
    std::vector<Bucket> old_buckets = std::move(buckets);
    assert(buckets.empty());
    num_entries = 0;
    buckets.resize(new_capacity);
    for (const Bucket &bucket : old_buckets) {
        if (bucket.full()) {
            insert_intern(bucket.key, bucket.hash);
        }
    }
    utils::unused_variable(num_entries_before);
    assert(num_entries == num_entries_before);
    ++num_resizes;
}

void IntHashSetDelta::enlarge() {
    unsigned int num_buckets = buckets.size();
    assert((num_buckets & (num_buckets - 1)) == 0);
    if (num_buckets > MAX_BUCKETS / 2) {
        std::cerr << "IntHashSet surpassed maximum capacity. This means"
                     " you either use IntHashSet for high-memory"
                     " applications for which it was not designed, or there"
                     " is an unexpectedly high number of hash collisions"
                     " that should be investigated. Aborting."
                  << std::endl;
        utils::exit_with(utils::ExitCode::SEARCH_CRITICAL_ERROR);
    }
    rehash(num_buckets * 2);
}

int IntHashSetDelta::get_bucket(HashType hash) const {
    assert(!buckets.empty());
    unsigned int num_buckets = buckets.size();
    assert((num_buckets & (num_buckets - 1)) == 0);
    return hash & (num_buckets - 1);
}

int IntHashSetDelta::get_distance(int index1, int index2) const {
    assert(utils::in_bounds(index1, buckets));
    assert(utils::in_bounds(index2, buckets));
    if (index2 >= index1) {
        return index2 - index1;
    } else {
        return capacity() + index2 - index1;
    }
}

int IntHashSetDelta::find_next_free_bucket_index(int index) const {
    assert(num_entries < capacity());
    assert(utils::in_bounds(index, buckets));
    while (buckets[index].full()) {
        index = get_bucket(index + 1);
    }
    return index;
}

KeyType IntHashSetDelta::find_equal_key(KeyType key, HashType hash) const {
    int ideal_index = get_bucket(hash);
    for (int i = 0; i < MAX_DISTANCE; ++i) {
        int index = get_bucket(ideal_index + i);
        const Bucket &bucket = buckets[index];
        if (bucket.full() && bucket.hash == hash && equal(bucket.key, key)) {
            return bucket.key;
        }
    }
    return Bucket::empty_bucket_key;
}

std::pair<KeyType, bool> IntHashSetDelta::insert_intern(KeyType key, HashType hash) {
    KeyType equal_key = find_equal_key(key, hash);
    if (equal_key != Bucket::empty_bucket_key) {
        return std::make_pair(equal_key, false);
    }

    assert(num_entries <= capacity());
    if (num_entries == capacity()) {
        enlarge();
    }
    assert(num_entries < capacity());

    int ideal_index = get_bucket(hash);
    int free_index = find_next_free_bucket_index(ideal_index);

    while (get_distance(ideal_index, free_index) >= MAX_DISTANCE) {
        bool swapped = false;
        int num_buckets = capacity();
        int max_offset = std::min(MAX_DISTANCE, num_buckets) - 1;
        for (int offset = max_offset; offset >= 1; --offset) {
            assert(offset < num_buckets);
            int candidate_index = free_index + num_buckets - offset;
            assert(candidate_index >= 0);
            candidate_index = get_bucket(candidate_index);
            HashType candidate_hash = buckets[candidate_index].hash;
            int candidate_ideal_index = get_bucket(candidate_hash);
            if (get_distance(candidate_ideal_index, free_index) < MAX_DISTANCE) {
                std::swap(buckets[candidate_index], buckets[free_index]);
                free_index = candidate_index;
                swapped = true;
                break;
            }
        }
        if (!swapped) {
            enlarge();
            return insert_intern(key, hash);
        }
    }
    assert(utils::in_bounds(free_index, buckets));
    assert(!buckets[free_index].full());
    buckets[free_index] = Bucket(key, hash);
    ++num_entries;
    return std::make_pair(key, true);
}

int IntHashSetDelta::size() const {
    return num_entries;
}

std::pair<KeyType, bool> IntHashSetDelta::insert(KeyType key, HashType hash) {
    assert(key >= 0);
    return insert_intern(key, hash);
}

void IntHashSetDelta::dump(utils::LogProxy &log) const {
    int num_buckets = capacity();
    log << "[";
    for (int i = 0; i < num_buckets; ++i) {
        const Bucket &bucket = buckets[i];
        if (bucket.full()) {
            log << bucket.key;
        } else {
            log << "_";
        }
        if (i < num_buckets - 1) {
            log << ", ";
        }
    }
    log << "]" << std::endl;
}

void IntHashSetDelta::print_statistics(utils::LogProxy &log) const {
    assert(!buckets.empty());
    int num_buckets = capacity();
    assert(num_buckets != 0);
    log << "Int hash set load factor: " << num_entries << "/" << num_buckets
        << " = " << static_cast<double>(num_entries) / num_buckets
        << std::endl;
    log << "Int hash set resizes: " << num_resizes << std::endl;
}

std::size_t IntHashSetDelta::memory_estimate() const {
    return sizeof(*this) + buckets.capacity() * sizeof(Bucket);
}