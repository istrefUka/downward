#pragma once

#include <unordered_map>
#include <vector>
#include "algorithms/int_hash_set.h"
#include "delta_state_info.h"



class DeltaStateTable {
    std::unordered_map<int_hash_set::HashType, std::vector<StateID>> table;

public:
    std::unordered_map<int_hash_set::HashType, std::vector<StateID>> get_table();
    void insert_force(int_hash_set::HashType hash, StateID id);
    InsertResult insert(int_hash_set::HashType hash, StateID id);
};