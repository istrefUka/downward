#include "int_packer_delta.h"

#include <cassert>
#include <iostream>
#include <cmath>
#include <memory>
#include <algorithm>


using namespace std;


static const int BITS_PER_BIN = sizeof(DeltaPacker::Bin) * 8;

static DeltaPacker::Bin get_bit_mask(int from, int to) {
    // Return mask with all bits in the range [from, to) set to 1.
    assert(from >= 0 && to >= from && to <= BITS_PER_BIN);
    int length = to - from;
    if (length == BITS_PER_BIN) {
        // 1U << BITS_PER_BIN has undefined behaviour in C++; e.g.
        // 1U << 32 == 1 (not 0) on 32-bit Intel platforms. Hence this
        // special case.
        assert(from == 0 && to == BITS_PER_BIN);
        return ~DeltaPacker::Bin(0);
    } else {
        return ((DeltaPacker::Bin(1) << length) - 1) << from;
    }
}

static int get_bit_size_for_range(int range) {
    assert(range >= 1);
    // Domains in domain-abstracted tasks may have size one.
    if (range == 1) {
        return 1;
    }
    int num_bits = 0;
    while ((1U << num_bits) < static_cast<unsigned int>(range))
        ++num_bits;
    return num_bits;
}

class DeltaPacker::VariableInfo {
    int range;
    int bin_index;
    int shift;
    Bin read_mask;
    Bin clear_mask;
public:
    VariableInfo(int range_, int bin_index_, int shift_)
        : range(range_), bin_index(bin_index_), shift(shift_) {
        int bit_size = get_bit_size_for_range(range);
        read_mask = get_bit_mask(shift, shift + bit_size);
        clear_mask = ~read_mask;
    }

    VariableInfo(): range(0), bin_index(-1), shift(0), read_mask(0), clear_mask(0) {
        // Default constructor needed for resize() in pack_bins.
    }


    ~VariableInfo() {
    }
    int get_range() const{
        return range;
    }
};



DeltaPacker::DeltaPacker(const vector<int> &ranges) : num_bins(0) {
    pack_bins(ranges);
}

DeltaPacker::~DeltaPacker() {
}







int create_mask(int bit_size) {
    return (1 << bit_size) - 1;
}

unsigned int read_bits(int shift, int bit_size, DeltaPacker::Bin buffer){
    int mask = create_mask(bit_size);
    return (buffer >> shift) & mask;
}


void write_bits(int value, int shift, int bit_size, DeltaPacker::Bin &packed_value) {
    int mask = create_mask(bit_size) << shift;

    // Alte Bits an dieser Stelle löschen
    packed_value = packed_value & ~mask;

    // Neuen Wert an die richtige Stelle schieben und setzen
    packed_value = packed_value | (value << shift);
}

//This method should set the first bit to 0 or 1, the second pair of
//bits is the position of the change the third pair of bits is the value that
//it was being changed with and so on.
std::vector<DeltaPacker::Bin> DeltaPacker::create_buffer(std::vector<std::tuple<int, int>> &effs) {
    std::vector<std::tuple<int, int>> eff_ordered(effs); // Kopie von effs
    std::sort(eff_ordered.begin(), eff_ordered.end(),
        [this](const std::tuple<int,int> &a, const std::tuple<int,int> &b) {
            int bits_a = get_bit_size_for_range(var_infos[std::get<0>(a) -1].get_range());
            int bits_b = get_bit_size_for_range(var_infos[std::get<0>(b) - 1].get_range());
            return bits_a > bits_b; // absteigend: meiste Bits zuerst
        }
    );
    int intSizeBits = sizeof(int) * 8;
    int shift = 1;
    std::vector<Bin> buffer;
    int effs_bits = get_bit_size_for_range(this->effs_range);
    int var_bits = 0;
    Bin currentBuffer = 0;

    for (auto &[first, second] : eff_ordered) {
        if (shift + effs_bits > intSizeBits) {
            write_bits(1,0,1,currentBuffer);
            buffer.push_back(currentBuffer);
            currentBuffer = 0;
            shift = 1;
        }
        if (first-1 < 0) {
            std::cout << "effs wrongly used, first is: "<< first << std::endl;
        }
        write_bits(first, shift, effs_bits, currentBuffer);
        shift += effs_bits;
        var_bits = get_bit_size_for_range(var_infos[first-1].get_range());

        if (shift + var_bits > intSizeBits) {
            write_bits(1,0,1,currentBuffer);
            buffer.push_back(currentBuffer);
            currentBuffer = 0;
            shift = 1;
        }
        write_bits(second, shift, var_bits, currentBuffer);
        shift += var_bits;
    }
    buffer.push_back(currentBuffer);
    return buffer;
}




std::vector<std::tuple<int, int>> DeltaPacker::get_buffer(const std::vector<DeltaStateInfo> &buffer, int StateID) const{
    int effs_bits = get_bit_size_for_range(this->effs_range);
    int eff = 0;
    int index = StateID;
    int shift = 1;
    int intSizeBits = sizeof(int) * 8;
    int var_bits = 0;
    int var = 0;
    std::vector<std::tuple<int, int>> effects;
    while (true) {

        if (shift + effs_bits > intSizeBits) {
            //go next bin or finish
            if (read_bits(0,1,buffer[index].effs) == 0) {
                return effects;
            }
            index++;
            shift = 1;
        }

        eff = read_bits(shift,effs_bits, buffer[index].effs);
        shift += effs_bits;
        //Done reading
        if (eff == 0 ) {
            //finish
            return effects;
        }
        var_bits = get_bit_size_for_range(var_infos[eff-1].get_range());
        if (shift + var_bits > intSizeBits) {
            //go next bin
            index++;
            shift = 1;
        }

        var = read_bits(shift, var_bits, buffer[index].effs);
        shift += var_bits;
        effects.push_back(std::make_tuple(eff, var));
    }
}

void DeltaPacker::pack_bins(const vector<int> &ranges) {
    assert(var_infos.empty());

    int num_vars = ranges.size();
    var_infos.resize(num_vars);

    // bits_to_vars[k] contains all variables that require exactly k
    // bits to encode. Once a variable is packed into a bin, it is
    // removed from this index.
    // Loop over the variables in reverse order to prefer variables with
    // low indices in case of ties. This might increase cache-locality.
    vector<vector<int>> bits_to_vars(BITS_PER_BIN + 1);
    for (int var = num_vars - 1; var >= 0; --var) {
        int bits = get_bit_size_for_range(ranges[var]);
        assert(bits <= BITS_PER_BIN);
        bits_to_vars[bits].push_back(var);
    }

    int packed_vars = 0;
    while (packed_vars != num_vars)
        packed_vars += pack_one_bin(ranges, bits_to_vars);
}





int DeltaPacker::pack_one_bin(
    const vector<int> &ranges, vector<vector<int>> &bits_to_vars) {
    // Returns the number of variables added to the bin. We pack each
    // bin with a greedy strategy, always adding the largest variable
    // that still fits.

    ++num_bins;
    int bin_index = num_bins - 1;
    int used_bits = 0;
    int num_vars_in_bin = 0;

    while (true) {
        // Determine size of largest variable that still fits into the bin.
        int bits = BITS_PER_BIN - used_bits;
        while (bits > 0 && bits_to_vars[bits].empty())
            --bits;

        if (bits == 0) {
            // No more variables fit into the bin.
            // (This also happens when all variables have been packed.)
            return num_vars_in_bin;
        }

        // We can pack another variable of size bits into the current bin.
        // Remove the variable from bits_to_vars and add it to the bin.
        vector<int> &best_fit_vars = bits_to_vars[bits];
        int var = best_fit_vars.back();
        best_fit_vars.pop_back();

        var_infos[var] = VariableInfo(ranges[var], bin_index, used_bits);
        used_bits += bits;
        ++num_vars_in_bin;
    }
}