#pragma once

#include <memory>
#include <tuple>
#include <vector>
#include "state_id.h"

class State;

struct DeltaStateInfo {
    unsigned int effs;
    StateID parent_state;
};

struct InsertResult {
    std::vector<StateID>* bucket;
    bool inserted;
};
struct DeltaStateEntry {
    DeltaStateInfo data;
    StateID id;
};