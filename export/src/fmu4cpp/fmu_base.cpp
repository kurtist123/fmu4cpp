
#include "fmu4cpp/fmu_base.hpp"
#include "fmu4cpp/fmu_except.hpp"
#include "fmu4cpp/model_info.hpp"
#include "fmu4cpp/util.hpp"

#include "hash.hpp"

#include <algorithm>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <utility>


using namespace fmu4cpp;

fmu_base::fmu_base(fmu_data data) : data_(std::move(data)) {

    register_real("time", &time_)
            .setCausality(causality_t::INDEPENDENT)
            .setVariability(variability_t::CONTINUOUS)
            .setDescription("Simulation time");
}


std::optional<IntVariable> fmu_base::get_int_variable(const std::string &name) const {
    for (const auto &v: integers_) {
        if (v.name() == name) return v;
    }
    return std::nullopt;
}

std::optional<RealVariable> fmu_base::get_real_variable(const std::string &name) const {
    for (const auto &v: reals_) {
        if (v.name() == name) return v;
    }
    return std::nullopt;
}

std::optional<BoolVariable> fmu_base::get_bool_variable(const std::string &name) const {
    for (const auto &v: booleans_) {
        if (v.name() == name) return v;
    }
    return std::nullopt;
}

std::optional<StringVariable> fmu_base::get_string_variable(const std::string &name) const {
    for (const auto &v: strings_) {
        if (v.name() == name) return v;
    }
    return std::nullopt;
}

std::optional<BinaryVariable> fmu_base::get_binary_variable(const std::string &name) const {
    for (const auto &v: binary_) {
        if (v.name() == name) return v;
    }
    return std::nullopt;
}

std::optional<ClockVariable> fmu_base::get_clock_variable(const std::string &name) const {
    for (const auto &v: clocks_) {
        if (v.name() == name) return v;
    }
    return std::nullopt;
}

std::optional<Float32Variable> fmu_base::get_float32_variable(const std::string &name) const {
    for (const auto &v: float32s_) {
        if (v.name() == name) return v;
    }
    return std::nullopt;
}

std::optional<Int8Variable> fmu_base::get_int8_variable(const std::string &name) const {
    for (const auto &v: int8s_) {
        if (v.name() == name) return v;
    }
    return std::nullopt;
}

std::optional<UInt8Variable> fmu_base::get_uint8_variable(const std::string &name) const {
    for (const auto &v: uint8s_) {
        if (v.name() == name) return v;
    }
    return std::nullopt;
}

std::optional<Int16Variable> fmu_base::get_int16_variable(const std::string &name) const {
    for (const auto &v: int16s_) {
        if (v.name() == name) return v;
    }
    return std::nullopt;
}

std::optional<UInt16Variable> fmu_base::get_uint16_variable(const std::string &name) const {
    for (const auto &v: uint16s_) {
        if (v.name() == name) return v;
    }
    return std::nullopt;
}

std::optional<UInt32Variable> fmu_base::get_uint32_variable(const std::string &name) const {
    for (const auto &v: uint32s_) {
        if (v.name() == name) return v;
    }
    return std::nullopt;
}

std::optional<Int64Variable> fmu_base::get_int64_variable(const std::string &name) const {
    for (const auto &v: int64s_) {
        if (v.name() == name) return v;
    }
    return std::nullopt;
}

std::optional<UInt64Variable> fmu_base::get_uint64_variable(const std::string &name) const {
    for (const auto &v: uint64s_) {
        if (v.name() == name) return v;
    }
    return std::nullopt;
}

bool fmu_base::get_clock(value_ref vr) const {
    const auto it = vrToClockIndices_.find(vr);
    if (it == vrToClockIndices_.end()) {
        throw std::out_of_range("Unknown clock value reference: " + std::to_string(vr));
    }
    return clocks_[it->second].get();
}

void fmu_base::set_clock(value_ref vr, bool val) {
    const auto it = vrToClockIndices_.find(vr);
    if (it == vrToClockIndices_.end()) {
        throw std::out_of_range("Unknown clock value reference: " + std::to_string(vr));
    }
    clocks_[it->second].set(val);
}

void fmu_base::notify_output_event(value_ref clock_vr) {
    const auto it = vrToClockIndices_.find(clock_vr);
    if (it == vrToClockIndices_.end()) {
        throw std::invalid_argument("Unknown clock value reference: " + std::to_string(clock_vr));
    }
    auto &clock = clocks_[it->second];
    if (clock.causality() != causality_t::OUTPUT) {
        throw std::logic_error("Cannot notify output event on non-output clock: " + clock.name());
    }
    clock.set(true);
}

bool fmu_base::has_pending_output_events() const noexcept {
    for (const auto &c: clocks_) {
        if (c.causality() == causality_t::OUTPUT && c.get()) {
            return true;
        }
    }
    return false;
}

void fmu_base::clear_triggered_clocks() noexcept {
    for (auto &c: clocks_) {
        c.set(false);
    }
}

void fmu_base::enter_initialisation_mode(double start, std::optional<double> stop, std::optional<double> tolerance) {
    time_ = start;
    stop_ = stop;
    tolerance_ = tolerance;
    enter_initialisation_mode();
}

void fmu_base::enter_initialisation_mode() {}
void fmu_base::exit_initialisation_mode() {}

bool fmu_base::step(double currentTime, double dt) {

    if (stop_ && currentTime >= *stop_) {
        debugLog(fmiWarning, "Stop time reached");
        return false;
    }

    constexpr double TIME_TOLERANCE = 1e-9;
    if (std::abs(currentTime - time_) > TIME_TOLERANCE) {
        throw std::runtime_error("Current time does not match the internal time (within tolerance)");
    }

    if (do_step(dt)) {
        time_ += dt;

        return true;
    }

    return false;
}

void fmu_base::terminate() {}

void fmu_base::reset() {
    if (!state_ops_ || !get_state_ptr_) {
        throw fatal_error("Reset not implemented by FMU");
    }
    void *dst = get_state_ptr_(this);
    state_ops_->reset_inplace(dst);
}

void fmu_base::get_integer(const unsigned int vr[], size_t nvr, int value[]) const {
    for (unsigned i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        const auto idx = vrToIntegerIndices_.at(ref);
        value[i] = integers_[idx].get();
    }
}

void fmu_base::get_real(const unsigned int vr[], size_t nvr, double value[]) const {
    for (unsigned i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        const auto idx = vrToRealIndices_.at(ref);
        value[i] = reals_[idx].get();
    }
}

void fmu_base::get_float32(const unsigned int vr[], size_t nvr, float value[]) const {
    for (size_t i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        const auto idx = vrToFloat32Indices_.at(ref);
        value[i] = float32s_[idx].get();
    }
}

void fmu_base::get_int8(const unsigned int vr[], size_t nvr, int8_t value[]) const {
    for (size_t i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        const auto idx = vrToInt8Indices_.at(ref);
        value[i] = int8s_[idx].get();
    }
}

void fmu_base::get_uint8(const unsigned int vr[], size_t nvr, uint8_t value[]) const {
    for (size_t i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        const auto idx = vrToUInt8Indices_.at(ref);
        value[i] = uint8s_[idx].get();
    }
}

void fmu_base::get_int16(const unsigned int vr[], size_t nvr, int16_t value[]) const {
    for (size_t i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        const auto idx = vrToInt16Indices_.at(ref);
        value[i] = int16s_[idx].get();
    }
}

void fmu_base::get_uint16(const unsigned int vr[], size_t nvr, uint16_t value[]) const {
    for (size_t i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        const auto idx = vrToUInt16Indices_.at(ref);
        value[i] = uint16s_[idx].get();
    }
}

void fmu_base::get_uint32(const unsigned int vr[], size_t nvr, uint32_t value[]) const {
    for (size_t i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        const auto idx = vrToUInt32Indices_.at(ref);
        value[i] = uint32s_[idx].get();
    }
}

void fmu_base::get_int64(const unsigned int vr[], size_t nvr, int64_t value[]) const {
    for (size_t i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        const auto idx = vrToInt64Indices_.at(ref);
        value[i] = int64s_[idx].get();
    }
}

void fmu_base::get_uint64(const unsigned int vr[], size_t nvr, uint64_t value[]) const {
    for (size_t i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        const auto idx = vrToUInt64Indices_.at(ref);
        value[i] = uint64s_[idx].get();
    }
}

void fmu_base::get_boolean(const unsigned int vr[], size_t nvr, int value[]) const {
    for (unsigned i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        const auto idx = vrToBooleanIndices_.at(ref);
        value[i] = static_cast<int>(booleans_[idx].get());
    }
}

void fmu_base::get_boolean(const unsigned int vr[], size_t nvr, bool value[]) const {
    for (unsigned i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        const auto idx = vrToBooleanIndices_.at(ref);
        value[i] = booleans_[idx].get();
    }
}

void fmu_base::get_string(const unsigned int vr[], size_t nvr, const char *value[]) {
    if (stringBuffer_.size() < strings_.size()) {
        stringBuffer_.resize(strings_.size());
    }
    for (unsigned i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        const auto idx = vrToStringIndices_.at(ref);
        bool already_fetched = false;
        for (unsigned prev = 0; prev < i; prev++) {
            if (vr[prev] == ref) {
                already_fetched = true;
                break;
            }
        }
        if (!already_fetched) {
            auto val = strings_[idx].get();
            if (stringBuffer_[idx] != val) {
                stringBuffer_[idx] = std::move(val);
            }
        }
        value[i] = stringBuffer_[idx].c_str();
    }
}

void fmu_base::get_binary(const unsigned int vr[], size_t nvr, size_t valueSizes[], const uint8_t *values[]) {
    if (binaryBuffer_.size() < binary_.size()) {
        binaryBuffer_.resize(binary_.size());
    }
    for (size_t i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        const auto idx = vrToBinaryIndices_.at(ref);
        bool already_fetched = false;
        for (size_t prev = 0; prev < i; prev++) {
            if (vr[prev] == ref) {
                already_fetched = true;
                break;
            }
        }
        if (!already_fetched) {
            auto val = binary_[idx].get();
            if (binaryBuffer_[idx] != val) {
                binaryBuffer_[idx] = std::move(val);
            }
        }
        valueSizes[i] = binaryBuffer_[idx].size();
        values[i] = binaryBuffer_[idx].empty() ? nullptr : binaryBuffer_[idx].data();
    }
}

void fmu_base::set_integer(const unsigned int vr[], size_t nvr, const int value[]) {
    for (unsigned i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        const auto idx = vrToIntegerIndices_.at(ref);
        integers_[idx].set(value[i]);
    }
}

void fmu_base::set_real(const unsigned int vr[], size_t nvr, const double value[]) {
    for (unsigned i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        const auto idx = vrToRealIndices_.at(ref);
        reals_[idx].set(value[i]);
    }
}

void fmu_base::set_float32(const unsigned int vr[], size_t nvr, const float value[]) {
    for (size_t i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        const auto idx = vrToFloat32Indices_.at(ref);
        float32s_[idx].set(value[i]);
    }
}

void fmu_base::set_int8(const unsigned int vr[], size_t nvr, const int8_t value[]) {
    for (size_t i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        const auto idx = vrToInt8Indices_.at(ref);
        int8s_[idx].set(value[i]);
    }
}

void fmu_base::set_uint8(const unsigned int vr[], size_t nvr, const uint8_t value[]) {
    for (size_t i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        const auto idx = vrToUInt8Indices_.at(ref);
        uint8s_[idx].set(value[i]);
    }
}

void fmu_base::set_int16(const unsigned int vr[], size_t nvr, const int16_t value[]) {
    for (size_t i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        const auto idx = vrToInt16Indices_.at(ref);
        int16s_[idx].set(value[i]);
    }
}

void fmu_base::set_uint16(const unsigned int vr[], size_t nvr, const uint16_t value[]) {
    for (size_t i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        const auto idx = vrToUInt16Indices_.at(ref);
        uint16s_[idx].set(value[i]);
    }
}

void fmu_base::set_uint32(const unsigned int vr[], size_t nvr, const uint32_t value[]) {
    for (size_t i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        const auto idx = vrToUInt32Indices_.at(ref);
        uint32s_[idx].set(value[i]);
    }
}

void fmu_base::set_int64(const unsigned int vr[], size_t nvr, const int64_t value[]) {
    for (size_t i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        const auto idx = vrToInt64Indices_.at(ref);
        int64s_[idx].set(value[i]);
    }
}

void fmu_base::set_uint64(const unsigned int vr[], size_t nvr, const uint64_t value[]) {
    for (size_t i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        const auto idx = vrToUInt64Indices_.at(ref);
        uint64s_[idx].set(value[i]);
    }
}
void fmu_base::set_boolean(const unsigned int vr[], size_t nvr, const int value[]) {
    for (unsigned i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        const auto idx = vrToBooleanIndices_.at(ref);
        booleans_[idx].set(static_cast<bool>(value[i]));
    }
}
void fmu_base::set_boolean(const unsigned int vr[], size_t nvr, const bool value[]) {
    for (unsigned i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        const auto idx = vrToBooleanIndices_.at(ref);
        booleans_[idx].set(value[i]);
    }
}

void fmu_base::set_string(const unsigned int vr[], size_t nvr, const char *const value[]) {
    for (unsigned i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        const auto idx = vrToStringIndices_.at(ref);
        strings_[idx].set(value[i]);
    }
}

void fmu_base::set_binary(const unsigned int vr[], size_t nvr, const size_t valueSizes[], const uint8_t *const value[]) {

    for (unsigned i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        const auto idx = vrToBinaryIndices_.at(ref);
        const auto &var = binary_[idx];
        const size_t len = valueSizes[i];
        if (var.max_size().has_value() && len > *var.max_size()) {
            throw std::invalid_argument("Binary variable size " + std::to_string(len) +
                                       " exceeds maxSize " + std::to_string(*var.max_size()) +
                                       " for variable " + var.name());
        }
        const uint8_t *ptr = value ? value[i] : nullptr;
        if (len == 0) {
            binary_[idx].set(std::vector<uint8_t>());
        } else {
            if (!ptr) {
                throw std::invalid_argument("Null data pointer for non-empty binary variable " + var.name());
            }
            binary_[idx].set(std::vector<uint8_t>(ptr, ptr + len));
        }
    }
}


IntVariable &fmu_base::register_integer(const std::string &name, int *ptr, const std::function<void()> &onChange) {
    const auto vr = static_cast<unsigned int>(numVariables_++);
    auto &v = integers_.emplace_back(name, vr, numVariables_, ptr, onChange);
    vrToIntegerIndices_.emplace(v.value_reference(), integers_.size() - 1);
    return v;
}

IntVariable &fmu_base::register_integer(const std::string &name, const std::function<int()> &getter, const std::optional<std::function<void(int)>> &setter) {
    const auto vr = static_cast<unsigned int>(numVariables_++);
    auto &v = integers_.emplace_back(name, vr, numVariables_, getter, setter);
    vrToIntegerIndices_.emplace(v.value_reference(), integers_.size() - 1);
    return v;
}

RealVariable &fmu_base::register_real(const std::string &name, double *ptr, const std::function<void()> &onChange) {
    const auto vr = static_cast<unsigned int>(numVariables_++);
    auto &v = reals_.emplace_back(name, vr, numVariables_, ptr, onChange);
    vrToRealIndices_.emplace(v.value_reference(), reals_.size() - 1);
    return v;
}

RealVariable &fmu_base::register_real(const std::string &name, const std::function<double()> &getter, const std::optional<std::function<void(double)>> &setter) {
    const auto vr = static_cast<unsigned int>(numVariables_++);
    auto &v = reals_.emplace_back(name, vr, numVariables_, getter, setter);
    vrToRealIndices_.emplace(v.value_reference(), reals_.size() - 1);
    return v;
}

BoolVariable &fmu_base::register_boolean(const std::string &name, bool *ptr, const std::function<void()> &onChange) {
    const auto vr = static_cast<unsigned int>(numVariables_++);
    auto &v = booleans_.emplace_back(name, vr, numVariables_, ptr, onChange);
    vrToBooleanIndices_.emplace(v.value_reference(), booleans_.size() - 1);
    return v;
}

BoolVariable &fmu_base::register_boolean(const std::string &name, const std::function<bool()> &getter, const std::optional<std::function<void(bool)>> &setter) {
    const auto vr = static_cast<unsigned int>(numVariables_++);
    auto &v = booleans_.emplace_back(name, vr, numVariables_, getter, setter);
    vrToBooleanIndices_.emplace(v.value_reference(), booleans_.size() - 1);
    return v;
}

StringVariable &fmu_base::register_string(const std::string &name, std::string *ptr, const std::function<void()> &onChange) {
    const auto vr = static_cast<unsigned int>(numVariables_++);
    auto &v = strings_.emplace_back(name, vr, numVariables_, ptr, onChange);
    vrToStringIndices_.emplace(v.value_reference(), strings_.size() - 1);
    stringBuffer_.resize(strings_.size());
    return v;
}

StringVariable &fmu_base::register_string(const std::string &name, const std::function<std::string()> &getter, const std::optional<std::function<void(std::string)>> &setter) {
    const auto vr = static_cast<unsigned int>(numVariables_++);
    auto &v = strings_.emplace_back(name, vr, numVariables_, getter, setter);
    vrToStringIndices_.emplace(v.value_reference(), strings_.size() - 1);
    stringBuffer_.resize(strings_.size());
    return v;
}

BinaryVariable &fmu_base::register_binary(const std::string &name, BinaryType *ptr, const std::function<void()> &onChange) {
    const auto vr = static_cast<unsigned int>(numVariables_++);
    auto &v = binary_.emplace_back(name, vr, numVariables_, ptr, onChange);
    vrToBinaryIndices_.emplace(v.value_reference(), binary_.size() - 1);
    binaryBuffer_.resize(binary_.size());
    return v;
}

BinaryVariable &fmu_base::register_binary(const std::string &name, const std::function<BinaryType()> &getter, const std::optional<std::function<void(BinaryType)>> &setter) {
    const auto vr = static_cast<unsigned int>(numVariables_++);
    auto &v = binary_.emplace_back(name, vr, numVariables_, getter, setter);
    vrToBinaryIndices_.emplace(v.value_reference(), binary_.size() - 1);
    binaryBuffer_.resize(binary_.size());
    return v;
}

Float32Variable &fmu_base::register_float32(const std::string &name, float *ptr, const std::function<void()> &onChange) {
    const auto vr = static_cast<unsigned int>(numVariables_++);
    auto &v = float32s_.emplace_back(name, vr, numVariables_, ptr, onChange);
    vrToFloat32Indices_.emplace(v.value_reference(), float32s_.size() - 1);
    return v;
}

Float32Variable &fmu_base::register_float32(const std::string &name, const std::function<float()> &getter, const std::optional<std::function<void(float)>> &setter) {
    const auto vr = static_cast<unsigned int>(numVariables_++);
    auto &v = float32s_.emplace_back(name, vr, numVariables_, getter, setter);
    vrToFloat32Indices_.emplace(v.value_reference(), float32s_.size() - 1);
    return v;
}

Int8Variable &fmu_base::register_int8(const std::string &name, int8_t *ptr, const std::function<void()> &onChange) {
    const auto vr = static_cast<unsigned int>(numVariables_++);
    auto &v = int8s_.emplace_back(name, vr, numVariables_, ptr, onChange);
    vrToInt8Indices_.emplace(v.value_reference(), int8s_.size() - 1);
    return v;
}

Int8Variable &fmu_base::register_int8(const std::string &name, const std::function<int8_t()> &getter, const std::optional<std::function<void(int8_t)>> &setter) {
    const auto vr = static_cast<unsigned int>(numVariables_++);
    auto &v = int8s_.emplace_back(name, vr, numVariables_, getter, setter);
    vrToInt8Indices_.emplace(v.value_reference(), int8s_.size() - 1);
    return v;
}

UInt8Variable &fmu_base::register_uint8(const std::string &name, uint8_t *ptr, const std::function<void()> &onChange) {
    const auto vr = static_cast<unsigned int>(numVariables_++);
    auto &v = uint8s_.emplace_back(name, vr, numVariables_, ptr, onChange);
    vrToUInt8Indices_.emplace(v.value_reference(), uint8s_.size() - 1);
    return v;
}

UInt8Variable &fmu_base::register_uint8(const std::string &name, const std::function<uint8_t()> &getter, const std::optional<std::function<void(uint8_t)>> &setter) {
    const auto vr = static_cast<unsigned int>(numVariables_++);
    auto &v = uint8s_.emplace_back(name, vr, numVariables_, getter, setter);
    vrToUInt8Indices_.emplace(v.value_reference(), uint8s_.size() - 1);
    return v;
}

Int16Variable &fmu_base::register_int16(const std::string &name, int16_t *ptr, const std::function<void()> &onChange) {
    const auto vr = static_cast<unsigned int>(numVariables_++);
    auto &v = int16s_.emplace_back(name, vr, numVariables_, ptr, onChange);
    vrToInt16Indices_.emplace(v.value_reference(), int16s_.size() - 1);
    return v;
}

Int16Variable &fmu_base::register_int16(const std::string &name, const std::function<int16_t()> &getter, const std::optional<std::function<void(int16_t)>> &setter) {
    const auto vr = static_cast<unsigned int>(numVariables_++);
    auto &v = int16s_.emplace_back(name, vr, numVariables_, getter, setter);
    vrToInt16Indices_.emplace(v.value_reference(), int16s_.size() - 1);
    return v;
}

UInt16Variable &fmu_base::register_uint16(const std::string &name, uint16_t *ptr, const std::function<void()> &onChange) {
    const auto vr = static_cast<unsigned int>(numVariables_++);
    auto &v = uint16s_.emplace_back(name, vr, numVariables_, ptr, onChange);
    vrToUInt16Indices_.emplace(v.value_reference(), uint16s_.size() - 1);
    return v;
}

UInt16Variable &fmu_base::register_uint16(const std::string &name, const std::function<uint16_t()> &getter, const std::optional<std::function<void(uint16_t)>> &setter) {
    const auto vr = static_cast<unsigned int>(numVariables_++);
    auto &v = uint16s_.emplace_back(name, vr, numVariables_, getter, setter);
    vrToUInt16Indices_.emplace(v.value_reference(), uint16s_.size() - 1);
    return v;
}

UInt32Variable &fmu_base::register_uint32(const std::string &name, uint32_t *ptr, const std::function<void()> &onChange) {
    const auto vr = static_cast<unsigned int>(numVariables_++);
    auto &v = uint32s_.emplace_back(name, vr, numVariables_, ptr, onChange);
    vrToUInt32Indices_.emplace(v.value_reference(), uint32s_.size() - 1);
    return v;
}

UInt32Variable &fmu_base::register_uint32(const std::string &name, const std::function<uint32_t()> &getter, const std::optional<std::function<void(uint32_t)>> &setter) {
    const auto vr = static_cast<unsigned int>(numVariables_++);
    auto &v = uint32s_.emplace_back(name, vr, numVariables_, getter, setter);
    vrToUInt32Indices_.emplace(v.value_reference(), uint32s_.size() - 1);
    return v;
}

Int64Variable &fmu_base::register_int64(const std::string &name, int64_t *ptr, const std::function<void()> &onChange) {
    const auto vr = static_cast<unsigned int>(numVariables_++);
    auto &v = int64s_.emplace_back(name, vr, numVariables_, ptr, onChange);
    vrToInt64Indices_.emplace(v.value_reference(), int64s_.size() - 1);
    return v;
}

Int64Variable &fmu_base::register_int64(const std::string &name, const std::function<int64_t()> &getter, const std::optional<std::function<void(int64_t)>> &setter) {
    const auto vr = static_cast<unsigned int>(numVariables_++);
    auto &v = int64s_.emplace_back(name, vr, numVariables_, getter, setter);
    vrToInt64Indices_.emplace(v.value_reference(), int64s_.size() - 1);
    return v;
}

UInt64Variable &fmu_base::register_uint64(const std::string &name, uint64_t *ptr, const std::function<void()> &onChange) {
    const auto vr = static_cast<unsigned int>(numVariables_++);
    auto &v = uint64s_.emplace_back(name, vr, numVariables_, ptr, onChange);
    vrToUInt64Indices_.emplace(v.value_reference(), uint64s_.size() - 1);
    return v;
}

UInt64Variable &fmu_base::register_uint64(const std::string &name, const std::function<uint64_t()> &getter, const std::optional<std::function<void(uint64_t)>> &setter) {
    const auto vr = static_cast<unsigned int>(numVariables_++);
    auto &v = uint64s_.emplace_back(name, vr, numVariables_, getter, setter);
    vrToUInt64Indices_.emplace(v.value_reference(), uint64s_.size() - 1);
    return v;
}

ClockVariable &fmu_base::register_clock(std::string name, value_ref vr, causality_t causality) {
    if (causality != causality_t::INPUT && causality != causality_t::OUTPUT) {
        throw std::invalid_argument("Clock variable must have causality INPUT or OUTPUT");
    }
    if (vrToClockIndices_.find(vr) != vrToClockIndices_.end() ||
        vrToIntegerIndices_.find(vr) != vrToIntegerIndices_.end() ||
        vrToRealIndices_.find(vr) != vrToRealIndices_.end() ||
        vrToBooleanIndices_.find(vr) != vrToBooleanIndices_.end() ||
        vrToStringIndices_.find(vr) != vrToStringIndices_.end() ||
        vrToBinaryIndices_.find(vr) != vrToBinaryIndices_.end() ||
        vrToFloat32Indices_.find(vr) != vrToFloat32Indices_.end() ||
        vrToInt8Indices_.find(vr) != vrToInt8Indices_.end() ||
        vrToUInt8Indices_.find(vr) != vrToUInt8Indices_.end() ||
        vrToInt16Indices_.find(vr) != vrToInt16Indices_.end() ||
        vrToUInt16Indices_.find(vr) != vrToUInt16Indices_.end() ||
        vrToUInt32Indices_.find(vr) != vrToUInt32Indices_.end() ||
        vrToInt64Indices_.find(vr) != vrToInt64Indices_.end() ||
        vrToUInt64Indices_.find(vr) != vrToUInt64Indices_.end()) {
        throw std::invalid_argument("Duplicate value reference: " + std::to_string(vr));
    }

    if (vr >= numVariables_) {
        numVariables_ = static_cast<size_t>(vr + 1);
    }
    const auto index = numVariables_;
    auto &v = clocks_.emplace_back(std::move(name), vr, index, causality);
    vrToClockIndices_.emplace(vr, clocks_.size() - 1);
    return v;
}

ClockVariable &fmu_base::register_clock(std::string name, causality_t causality) {
    const auto vr = static_cast<unsigned int>(numVariables_);
    return register_clock(std::move(name), vr, causality);
}

std::vector<const VariableBase *> fmu_base::collect_variables(
        const std::function<bool(const VariableBase &)> &predicate) const {
    std::vector<const VariableBase *> vars;
    const auto add_if = [&vars, &predicate](const auto &vec) {
        for (const auto &v: vec) {
            if (predicate(v)) {
                vars.push_back(&v);
            }
        }
    };
    add_if(integers_);
    add_if(reals_);
    add_if(booleans_);
    add_if(strings_);
    add_if(binary_);
    add_if(clocks_);
    add_if(float32s_);
    add_if(int8s_);
    add_if(uint8s_);
    add_if(int16s_);
    add_if(uint16s_);
    add_if(uint32s_);
    add_if(int64s_);
    add_if(uint64s_);
    return vars;
}

[[maybe_unused]] std::string fmu_base::guid() const {
    const model_info info = get_model_info();
    const std::vector content{
            info.author,
            info.version,
            info.description,
            info.modelName,
            model_identifier()};

    std::stringstream ss;
    for (const auto &str: content) {
        ss << str;
    }

    const auto vars = collect_variables();
    for (const auto &v: vars) {
        ss << v->name();
        ss << std::to_string(v->index());
        ss << std::to_string(v->value_reference());
        ss << to_string(v->causality());
        if (v->variability()) {
            ss << to_string(*v->variability());
        }
        if (v->initial()) {
            ss << to_string(*v->initial());
        }
    }

    return std::to_string(fnv1a(ss.str()));
}

void fmu_base::debugLog(const fmiStatus s, const std::string &message) const {
    if (data_.fmiLogger) {
        data_.fmiLogger->log(s, message);
    }
}

std::vector<unsigned> fmu_base::get_value_refs() const {
    std::vector<unsigned int> indices;
    const auto allVars = collect_variables();
    indices.reserve(allVars.size());
    for (const auto &v: allVars) {
        indices.emplace_back(v->value_reference());
    }

    return indices;
}


void *fmu_base::getFMUState() {
    if (!state_ops_ || !get_state_ptr_) throw fatal_error("getFMUState not implemented");
    const void *in_place = get_state_ptr_(this);
    return state_ops_->create_from_state(in_place);
}

void fmu_base::setFmuState(void *state) {
    if (!state_ops_ || !get_state_ptr_) throw fatal_error("setFmuState not implemented");
    void *dst = get_state_ptr_(this);
    state_ops_->assign_into_state(dst, state);
}

void fmu_base::freeFmuState(void **state) {
    if (!state_ops_ || !state || !*state) throw fatal_error("freeFmuState not implemented");
    state_ops_->destroy(*state);
    *state = nullptr;
}

void fmu_base::serializedFMUStateSize(void *state, size_t &size) {
    if (!state_ops_) throw fatal_error("serializedFMUStateSize not implemented");
    size = state_ops_->serialized_size();
}

void fmu_base::serializeFMUState(void *state, std::vector<uint8_t> &out) {
    if (!state_ops_) throw fatal_error("serializeFMUState not implemented");
    const void *ptr = state ? state : get_state_ptr_(this);
    state_ops_->serialize(ptr, out);
}

void fmu_base::deserializeFMUState(const std::vector<uint8_t> &in, void **out) {
    if (!state_ops_) throw fatal_error("deserializeFMUState not implemented");
    state_ops_->deserialize(in, out);
}
