#ifndef ALGORITHMS_INT_HASH_SET_DELTA_H
#define ALGORITHMS_INT_HASH_SET_DELTA_H

#include "../utils/collections.h"
#include "../utils/language.h"
#include "../utils/logging.h"
#include "../utils/system.h"
#include "../state_id.h"
#include "segmented_vector.h"


#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>
#include <cstddef>


// Forward declarations — no #include needed here
class StateRegistry;
class DeltaPacker;
class TaskProxy;
struct DeltaStateInfo;


using KeyType = int;
using HashType = unsigned int;
using PackedStateBin = unsigned int;
static_assert(sizeof(KeyType) == 4, "KeyType does not use 4 bytes");
static_assert(sizeof(HashType) == 4, "HashType does not use 4 bytes");

class IntHashSetDelta {
    static const int MAX_DISTANCE = 32;
    static const unsigned int MAX_BUCKETS =
        std::numeric_limits<unsigned int>::max();

    std::vector<DeltaStateInfo> &delta_state_data_pool;
    DeltaPacker &delta_packer;
    segmented_vector::SegmentedArrayVector<PackedStateBin> &state_data_pool;
    const TaskProxy &task_proxy;
    StateRegistry &registry;

    struct Bucket {
        KeyType key;
        HashType hash;

        static const KeyType empty_bucket_key = -1;

        Bucket() : key(empty_bucket_key), hash(0) {}
        Bucket(KeyType key, HashType hash) : key(key), hash(hash) {}

        bool full() const {
            return key != empty_bucket_key;
        }
    };

    std::vector<Bucket> buckets;
    int num_entries;
    int num_resizes;

    // Private methods — nur deklariert, implementiert in .cpp
    int capacity() const;
    bool equal(KeyType lhs, KeyType rhs) const;  // ruft registry.lookup_state_delta auf -> in .cpp
    void rehash(int new_capacity);
    void enlarge();
    int get_bucket(HashType hash) const;
    int get_distance(int index1, int index2) const;
    int find_next_free_bucket_index(int index) const;
    KeyType find_equal_key(KeyType key, HashType hash) const;
    std::pair<KeyType, bool> insert_intern(KeyType key, HashType hash);

public:
    IntHashSetDelta(
    std::vector<DeltaStateInfo> &delta_state_data_pool,
    DeltaPacker &delta_packer,
    segmented_vector::SegmentedArrayVector<PackedStateBin> &state_data_pool,
    const TaskProxy &task_proxy,
    StateRegistry &registry);

    int size() const;
    std::pair<KeyType, bool> insert(KeyType key, HashType hash);
    void dump(utils::LogProxy &log) const;
    void print_statistics(utils::LogProxy &log) const;
    std::size_t memory_estimate() const;
};

#endif