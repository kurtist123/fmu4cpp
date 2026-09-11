
#include "fmu4cpp/fmu_variable.hpp"

#include <stdexcept>

namespace fmu4cpp {

    std::string to_string(const causality_t &c) {
        switch (c) {
            case causality_t::PARAMETER:
                return "parameter";
            case causality_t::INPUT:
                return "input";
            case causality_t::CALCULATED_PARAMETER:
                return "calculatedParameter";
            case causality_t::LOCAL:
                return "local";
            case causality_t::OUTPUT:
                return "output";
            case causality_t::INDEPENDENT:
                return "independent";
        }
        throw std::logic_error("Invalid causality encountered");
    }

    std::string to_string(const variability_t &v) {
        switch (v) {
            case variability_t::CONTINUOUS:
                return "continuous";
            case variability_t::CONSTANT:
                return "constant";
            case variability_t::DISCRETE:
                return "discrete";
            case variability_t::FIXED:
                return "fixed";
            case variability_t::TUNABLE:
                return "tunable";
        }
        throw std::logic_error("Invalid variability encountered");
    }

    std::string to_string(const initial_t &i) {
        switch (i) {
            case initial_t::APPROX:
                return "approx";
            case initial_t::EXACT:
                return "exact";
            case initial_t::CALCULATED:
                return "calculated";
        }
        throw std::logic_error("Invalid initial encountered");
    }

    std::string to_string(const interval_variability_t &iv) {
        switch (iv) {
            case interval_variability_t::CONSTANT:
                return "constant";
            case interval_variability_t::FIXED:
                return "fixed";
            case interval_variability_t::CALCULATED:
                return "calculated";
            case interval_variability_t::TUNABLE:
                return "tunable";
            case interval_variability_t::CHANGING:
                return "changing";
            case interval_variability_t::COUNTDOWN:
                return "countdown";
        }
        throw std::logic_error("Invalid interval_variability encountered");
    }

    std::string to_string(const data_type &dt) {
        switch (dt) {
            case data_type::INT8:
                return "Int8";
            case data_type::UINT8:
                return "UInt8";
            case data_type::INT16:
                return "Int16";
            case data_type::UINT16:
                return "UInt16";
            case data_type::INT32:
                return "Int32";
            case data_type::UINT32:
                return "UInt32";
            case data_type::INT64:
                return "Int64";
            case data_type::UINT64:
                return "UInt64";
            case data_type::FLOAT32:
                return "Float32";
            case data_type::FLOAT64:
                return "Float64";
            case data_type::BOOLEAN:
                return "Boolean";
            case data_type::STRING:
                return "String";
            case data_type::BINARY:
                return "Binary";
            case data_type::CLOCK:
                return "Clock";
        }
        throw std::logic_error("Invalid data_type encountered");
    }

    bool requires_start(const VariableBase &v) {
        // clang-format off
        return v.initial() == initial_t::EXACT
               || v.initial() == initial_t::APPROX
               || v.causality() == causality_t::INPUT
               || v.causality() == causality_t::PARAMETER
               || v.variability() == variability_t::CONSTANT;
        // clang-format on
    }

}// namespace fmu4cpp
