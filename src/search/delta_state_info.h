#pragma once

#include <memory>
#include <tuple>
#include <vector>
#include "state_id.h"

class State;

struct DeltaStateInfo {
    std::shared_ptr<std::vector<std::tuple<int, int>>> effs;
    std::shared_ptr<State> parent_state;
};

struct InsertResult {
    std::vector<StateID>* bucket;
    bool inserted;
};
struct DeltaStateEntry {
    DeltaStateInfo data;
    StateID id;
};