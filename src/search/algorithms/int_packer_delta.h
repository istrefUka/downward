#ifndef FAST_DOWNWARD_INT_PACKER_DELTA_H
#define FAST_DOWNWARD_INT_PACKER_DELTA_H

#include <vector>
#include <memory>
#include "/home/istref-uka/Dokumente/BA/downward/src/search/delta_state_info.h"

class DeltaPacker {
    class VariableInfo;

    std::vector<VariableInfo> var_infos;
    int num_bins;
    int effs_range = 0;

    int pack_one_bin(
        const std::vector<int> &ranges,
        std::vector<std::vector<int>> &bits_to_vars);
    void pack_bins(const std::vector<int> &ranges);
public:
    typedef unsigned int Bin;
    std::vector<Bin> create_buffer(std::vector<std::tuple<int, int>> &effs);

    std::vector<std::tuple<int, int>> get_buffer(std::vector<DeltaStateInfo> buffer, int StateID) const;
    /*
      The constructor takes the range for each variable. The domain of
      variable i is {0, ..., ranges[i] - 1}. Because we are using signed
      ints for the ranges (and genenerally for the values of variables),
      a variable can take up at most 31 bits if int is 32-bit.
    */
    explicit DeltaPacker(const std::vector<int> &ranges);
    ~DeltaPacker();

    int get(const Bin *buffer, int var) const;
    void set(Bin *buffer, int var, int value) const;

    //TODO: Probably delete this
    int get_num_bins() const {
        return num_bins;
    }
    void set_effs_range(int range){
        effs_range = range;
    }
};














#endif //FAST_DOWNWARD_INT_PACKER_DELTA_H
