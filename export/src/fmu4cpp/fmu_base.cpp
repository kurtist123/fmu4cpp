
#include "fmu4cpp/fmu_base.hpp"
#include "fmu4cpp/fmu_except.hpp"
#include "fmu4cpp/model_info.hpp"
#include "fmu4cpp/util.hpp"

#include "hash.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <sstream>
#include <utility>


using namespace fmu4cpp;

fmu_base::fmu_base(fmu_data data) : data_(std::move(data)) {

    register_real("time", &time_)
            .setCausality(causality_t::INDEPENDENT)
            .setVariability(variability_t::CONTINUOUS)
            .setDescription("Simulation time");
}

const VariableBase *fmu_base::get_variable(const std::string &name) const {
    auto it = nameToVariable_.find(name);
    return it != nameToVariable_.end() ? it->second : nullptr;
}

const VariableBase *fmu_base::get_variable(unsigned int vr) const {
    auto it = vrToVariable_.find(vr);
    return it != vrToVariable_.end() ? it->second : nullptr;
}

std::optional<IntVariable> fmu_base::get_int_variable(const std::string &name) const {
    return get_variable<IntVariable>(name);
}

std::optional<RealVariable> fmu_base::get_real_variable(const std::string &name) const {
    return get_variable<RealVariable>(name);
}

std::optional<BoolVariable> fmu_base::get_bool_variable(const std::string &name) const {
    return get_variable<BoolVariable>(name);
}

std::optional<StringVariable> fmu_base::get_string_variable(const std::string &name) const {
    return get_variable<StringVariable>(name);
}

std::optional<BinaryVariable> fmu_base::get_binary_variable(const std::string &name) const {
    return get_variable<BinaryVariable>(name);
}

std::optional<ClockVariable> fmu_base::get_clock_variable(const std::string &name) const {
    return get_variable<ClockVariable>(name);
}

bool fmu_base::has_clocks() const {
    for (const auto &v: variables_) {
        if (v->type() == data_type::CLOCK) {
            return true;
        }
    }
    return false;
}

bool fmu_base::has_event_mode() const {
    return get_model_info().hasEventMode || has_clocks();
}

void fmu_base::enter_initialisation_mode(double start, std::optional<double> stop, std::optional<double> tolerance) {
    time_ = start;
    stop_ = stop;
    tolerance_ = tolerance;
    enter_initialisation_mode();
}

void fmu_base::enter_initialisation_mode() {}
void fmu_base::exit_initialisation_mode() {
    for (auto &v: variables_) {
        if (v->type() == data_type::CLOCK) {
            auto *cv = static_cast<ClockVariable *>(v.get());
            cv->compute_initial_tick_time(time_);
        }
    }
}

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

        for (auto &v: variables_) {
            if (v->type() == data_type::CLOCK) {
                auto *cv = static_cast<ClockVariable *>(v.get());
                if (cv->is_time_based() && cv->next_tick_time().has_value()) {
                    double tick = *cv->next_tick_time();
                    if (time_ >= tick - TIME_TOLERANCE) {
                        cv->force_set(true);
                        pending_events_ = true;
                        cv->advance_tick();
                    }
                }
            }
        }

        return true;
    }

    return false;
}

void fmu_base::terminate() {}

void fmu_base::reset() {
    time_ = 0.0;
    stop_ = std::nullopt;
    tolerance_ = std::nullopt;
    pending_events_ = false;
    last_discrete_states_ = discrete_states_info{};
    stringBuffer_.clear();
    binaryBuffer_.clear();

    for (auto &v: variables_) {
        if (v->type() == data_type::CLOCK) {
            auto *cv = dynamic_cast<ClockVariable *>(v.get());
            if (cv) {
                cv->reset();
                cv->compute_initial_tick_time(time_);
            }
        }
    }

    if (state_ops_ && get_state_ptr_) {
        void *dst = get_state_ptr_(this);
        state_ops_->reset_inplace(dst);
    }
}

void fmu_base::get_integer(const unsigned int vr[], size_t nvr, int value[]) const {
    get_values<int32_t>(vr, nvr, value, nvr);
}

void fmu_base::get_real(const unsigned int vr[], size_t nvr, double value[]) const {
    get_values<double>(vr, nvr, value, nvr);
}

void fmu_base::get_boolean(const unsigned int vr[], size_t nvr, int value[]) const {
    for (size_t i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        auto it = vrToVariable_.find(ref);
        if (it == vrToVariable_.end()) {
            throw std::out_of_range("Invalid valueReference: " + std::to_string(ref));
        }
        if (it->second->type() != data_type::BOOLEAN) {
            throw std::invalid_argument("Type mismatch for valueReference " + std::to_string(ref));
        }
        auto *v = static_cast<const BoolVariable *>(it->second);
        value[i] = v->get() ? 1 : 0;
    }
}

void fmu_base::get_boolean(const unsigned int vr[], size_t nvr, bool value[]) const {
    get_values<bool>(vr, nvr, value, nvr);
}

void fmu_base::deactivate_active_clocks() {
    for (auto &v: variables_) {
        if (v->type() == data_type::CLOCK) {
            auto *cv = static_cast<ClockVariable *>(v.get());
            if (cv->get()) {
                cv->force_set(false);
                if (cv->causality() == causality_t::INPUT) {
                    on_clock_deactivated(cv->value_reference());
                }
            }
        }
    }
}

bool fmu_base::is_clock_active(unsigned int vr) const {
    auto it = vrToVariable_.find(vr);
    if (it == vrToVariable_.end() || it->second->type() != data_type::CLOCK) {
        return false;
    }
    return static_cast<const ClockVariable *>(it->second)->get();
}

void fmu_base::activate_clock(unsigned int vr) {
    auto it = vrToVariable_.find(vr);
    if (it != vrToVariable_.end() && it->second->type() == data_type::CLOCK) {
        auto *cv = static_cast<ClockVariable *>(it->second);
        cv->force_set(true);
        if (cv->causality() == causality_t::INPUT) {
            on_clock_activated(vr);
        }
    }
}

std::optional<double> fmu_base::get_next_event_time() const {
    std::optional<double> earliest = std::nullopt;
    for (const auto &v: variables_) {
        if (v->type() == data_type::CLOCK) {
            auto *cv = static_cast<const ClockVariable *>(v.get());
            if (cv->is_time_based() && cv->next_tick_time().has_value()) {
                double t = *cv->next_tick_time();
                if (!earliest.has_value() || t < *earliest) {
                    earliest = t;
                }
            }
        }
    }
    return earliest;
}

void fmu_base::get_clock(const unsigned int vr[], size_t nvr, bool value[]) const {
    for (size_t i = 0; i < nvr; ++i) {
        auto it = vrToVariable_.find(vr[i]);
        if (it == vrToVariable_.end()) {
            throw std::out_of_range("Invalid valueReference: " + std::to_string(vr[i]));
        }
        auto *clockVar = dynamic_cast<const ClockVariable *>(it->second);
        if (!clockVar) {
            throw std::invalid_argument("Variable with valueReference " + std::to_string(vr[i]) + " is not a Clock");
        }
        value[i] = clockVar->get();
    }
}

void fmu_base::set_clock(const unsigned int vr[], size_t nvr, const bool value[]) {
    for (size_t i = 0; i < nvr; ++i) {
        auto it = vrToVariable_.find(vr[i]);
        if (it == vrToVariable_.end()) {
            throw std::out_of_range("Invalid valueReference: " + std::to_string(vr[i]));
        }
        auto *clockVar = dynamic_cast<ClockVariable *>(it->second);
        if (!clockVar) {
            throw std::invalid_argument("Variable with valueReference " + std::to_string(vr[i]) + " is not a Clock");
        }
        if (clockVar->causality() != causality_t::INPUT) {
            throw std::logic_error("Cannot set non-input Clock variable with valueReference " + std::to_string(vr[i]));
        }
        bool prev = clockVar->get();
        clockVar->set(value[i]);
        if (!prev && value[i]) {
            on_clock_activated(vr[i]);
        } else if (prev && !value[i]) {
            on_clock_deactivated(vr[i]);
        }
    }
}

void fmu_base::get_interval_decimal(const unsigned int vr[], size_t nvr, double intervals[], interval_qualifier_t qualifiers[]) {
    for (size_t i = 0; i < nvr; ++i) {
        auto it = vrToVariable_.find(vr[i]);
        if (it == vrToVariable_.end()) {
            throw std::out_of_range("Invalid valueReference: " + std::to_string(vr[i]));
        }
        auto *clockVar = dynamic_cast<ClockVariable *>(it->second);
        if (!clockVar) {
            throw std::invalid_argument("Variable with valueReference " + std::to_string(vr[i]) + " is not a Clock");
        }
        auto d = clockVar->getIntervalDecimal();
        intervals[i] = d.value_or(0.0);
        qualifiers[i] = clockVar->getIntervalQualifier();
        clockVar->resetIntervalQualifier();
    }
}

void fmu_base::set_interval_decimal(const unsigned int vr[], size_t nvr, const double intervals[]) {
    for (size_t i = 0; i < nvr; ++i) {
        auto it = vrToVariable_.find(vr[i]);
        if (it == vrToVariable_.end()) {
            throw std::out_of_range("Invalid valueReference: " + std::to_string(vr[i]));
        }
        auto *clockVar = dynamic_cast<ClockVariable *>(it->second);
        if (!clockVar) {
            throw std::invalid_argument("Variable with valueReference " + std::to_string(vr[i]) + " is not a Clock");
        }
        clockVar->setIntervalDecimal(intervals[i]);
        if (clockVar->is_time_based()) {
            clockVar->compute_initial_tick_time(time_);
        }
        on_interval_changed(vr[i], intervals[i]);
    }
}

void fmu_base::get_string(const unsigned int vr[], size_t nvr, const char *value[]) {
    stringBuffer_.clear();
    for (size_t i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        auto it = vrToVariable_.find(ref);
        if (it == vrToVariable_.end()) {
            throw std::out_of_range("Invalid valueReference: " + std::to_string(ref));
        }
        if (it->second->type() != data_type::STRING) {
            throw std::invalid_argument("Type mismatch for valueReference " + std::to_string(ref));
        }
        auto *v = static_cast<const StringVariable *>(it->second);
        stringBuffer_.push_back(v->get());
        value[i] = stringBuffer_.back().c_str();
    }
}

void fmu_base::get_binary(const unsigned int vr[], size_t nvr, size_t valueSizes[], const uint8_t *values[]) {
    binaryBuffer_.clear();
    for (size_t i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        auto it = vrToVariable_.find(ref);
        if (it == vrToVariable_.end()) {
            throw std::out_of_range("Invalid valueReference: " + std::to_string(ref));
        }
        if (it->second->type() != data_type::BINARY) {
            throw std::invalid_argument("Type mismatch for valueReference " + std::to_string(ref));
        }
        auto *v = static_cast<const BinaryVariable *>(it->second);
        binaryBuffer_.push_back(v->get());
        const auto &bin = binaryBuffer_.back();
        valueSizes[i] = bin.size();
        values[i] = bin.data();
    }
}

void fmu_base::set_integer(const unsigned int vr[], size_t nvr, const int value[]) {
    set_values<int32_t>(vr, nvr, value, nvr);
}

void fmu_base::set_real(const unsigned int vr[], size_t nvr, const double value[]) {
    set_values<double>(vr, nvr, value, nvr);
}

void fmu_base::set_boolean(const unsigned int vr[], size_t nvr, const int value[]) {
    for (size_t i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        auto it = vrToVariable_.find(ref);
        if (it == vrToVariable_.end()) {
            throw std::out_of_range("Invalid valueReference: " + std::to_string(ref));
        }
        if (it->second->type() != data_type::BOOLEAN) {
            throw std::invalid_argument("Type mismatch for valueReference " + std::to_string(ref));
        }
        auto *v = static_cast<BoolVariable *>(it->second);
        v->set(value[i] != 0);
    }
}

void fmu_base::set_boolean(const unsigned int vr[], size_t nvr, const bool value[]) {
    set_values<bool>(vr, nvr, value, nvr);
}

void fmu_base::set_string(const unsigned int vr[], size_t nvr, const char *const value[]) {
    for (size_t i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        auto it = vrToVariable_.find(ref);
        if (it == vrToVariable_.end()) {
            throw std::out_of_range("Invalid valueReference: " + std::to_string(ref));
        }
        if (it->second->type() != data_type::STRING) {
            throw std::invalid_argument("Type mismatch for valueReference " + std::to_string(ref));
        }
        auto *v = static_cast<StringVariable *>(it->second);
        v->set(value[i]);
    }
}

void fmu_base::set_binary(const unsigned int vr[], size_t nvr, const size_t valueSizes[], const uint8_t *const value[]) {
    for (size_t i = 0; i < nvr; i++) {
        const auto ref = vr[i];
        auto it = vrToVariable_.find(ref);
        if (it == vrToVariable_.end()) {
            throw std::out_of_range("Invalid valueReference: " + std::to_string(ref));
        }
        if (it->second->type() != data_type::BINARY) {
            throw std::invalid_argument("Type mismatch for valueReference " + std::to_string(ref));
        }
        auto *v = static_cast<BinaryVariable *>(it->second);
        v->set(std::vector<uint8_t>(value[i], value[i] + valueSizes[i]));
    }
}

IntVariable &fmu_base::register_integer(const std::string &name, int *ptr, const std::function<void()> &onChange) {
    numVariables_++;
    return add_variable<IntVariable>(name, numVariables_ - 1, numVariables_, ptr, onChange);
}

IntVariable &fmu_base::register_integer(const std::string &name, const std::function<int()> &getter, const std::optional<std::function<void(int)>> &setter) {
    numVariables_++;
    return add_variable<IntVariable>(name, numVariables_ - 1, numVariables_, getter, setter);
}

RealVariable &fmu_base::register_real(const std::string &name, double *ptr, const std::function<void()> &onChange) {
    numVariables_++;
    return add_variable<RealVariable>(name, numVariables_ - 1, numVariables_, ptr, onChange);
}

RealVariable &fmu_base::register_real(const std::string &name, const std::function<double()> &getter, const std::optional<std::function<void(double)>> &setter) {
    numVariables_++;
    return add_variable<RealVariable>(name, numVariables_ - 1, numVariables_, getter, setter);
}

BoolVariable &fmu_base::register_boolean(const std::string &name, bool *ptr, const std::function<void()> &onChange) {
    numVariables_++;
    return add_variable<BoolVariable>(name, numVariables_ - 1, numVariables_, ptr, onChange);
}

BoolVariable &fmu_base::register_boolean(const std::string &name, const std::function<bool()> &getter, const std::optional<std::function<void(bool)>> &setter) {
    numVariables_++;
    return add_variable<BoolVariable>(name, numVariables_ - 1, numVariables_, getter, setter);
}

StringVariable &fmu_base::register_string(const std::string &name, std::string *ptr, const std::function<void()> &onChange) {
    numVariables_++;
    return add_variable<StringVariable>(name, numVariables_ - 1, numVariables_, ptr, onChange);
}

StringVariable &fmu_base::register_string(const std::string &name, const std::function<std::string()> &getter, const std::optional<std::function<void(std::string)>> &setter) {
    numVariables_++;
    return add_variable<StringVariable>(name, numVariables_ - 1, numVariables_, getter, setter);
}

BinaryVariable &fmu_base::register_binary(const std::string &name, BinaryType *ptr, const std::function<void()> &onChange) {
    numVariables_++;
    return add_variable<BinaryVariable>(name, numVariables_ - 1, numVariables_, ptr, onChange);
}

BinaryVariable &fmu_base::register_binary(const std::string &name, const std::function<BinaryType()> &getter, const std::optional<std::function<void(BinaryType)>> &setter) {
    numVariables_++;
    return add_variable<BinaryVariable>(name, numVariables_ - 1, numVariables_, getter, setter);
}

Int8Variable &fmu_base::register_int8(const std::string &name, int8_t *ptr, const std::function<void()> &onChange) {
    numVariables_++;
    return add_variable<Int8Variable>(name, numVariables_ - 1, numVariables_, ptr, onChange);
}

Int8Variable &fmu_base::register_int8(const std::string &name, const std::function<int8_t()> &getter, const std::optional<std::function<void(int8_t)>> &setter) {
    numVariables_++;
    return add_variable<Int8Variable>(name, numVariables_ - 1, numVariables_, getter, setter);
}

UInt8Variable &fmu_base::register_uint8(const std::string &name, uint8_t *ptr, const std::function<void()> &onChange) {
    numVariables_++;
    return add_variable<UInt8Variable>(name, numVariables_ - 1, numVariables_, ptr, onChange);
}

UInt8Variable &fmu_base::register_uint8(const std::string &name, const std::function<uint8_t()> &getter, const std::optional<std::function<void(uint8_t)>> &setter) {
    numVariables_++;
    return add_variable<UInt8Variable>(name, numVariables_ - 1, numVariables_, getter, setter);
}

Int16Variable &fmu_base::register_int16(const std::string &name, int16_t *ptr, const std::function<void()> &onChange) {
    numVariables_++;
    return add_variable<Int16Variable>(name, numVariables_ - 1, numVariables_, ptr, onChange);
}

Int16Variable &fmu_base::register_int16(const std::string &name, const std::function<int16_t()> &getter, const std::optional<std::function<void(int16_t)>> &setter) {
    numVariables_++;
    return add_variable<Int16Variable>(name, numVariables_ - 1, numVariables_, getter, setter);
}

UInt16Variable &fmu_base::register_uint16(const std::string &name, uint16_t *ptr, const std::function<void()> &onChange) {
    numVariables_++;
    return add_variable<UInt16Variable>(name, numVariables_ - 1, numVariables_, ptr, onChange);
}

UInt16Variable &fmu_base::register_uint16(const std::string &name, const std::function<uint16_t()> &getter, const std::optional<std::function<void(uint16_t)>> &setter) {
    numVariables_++;
    return add_variable<UInt16Variable>(name, numVariables_ - 1, numVariables_, getter, setter);
}

Int32Variable &fmu_base::register_int32(const std::string &name, int32_t *ptr, const std::function<void()> &onChange) {
    return register_integer(name, ptr, onChange);
}

Int32Variable &fmu_base::register_int32(const std::string &name, const std::function<int32_t()> &getter, const std::optional<std::function<void(int32_t)>> &setter) {
    return register_integer(name, getter, setter);
}

UInt32Variable &fmu_base::register_uint32(const std::string &name, uint32_t *ptr, const std::function<void()> &onChange) {
    numVariables_++;
    return add_variable<UInt32Variable>(name, numVariables_ - 1, numVariables_, ptr, onChange);
}

UInt32Variable &fmu_base::register_uint32(const std::string &name, const std::function<uint32_t()> &getter, const std::optional<std::function<void(uint32_t)>> &setter) {
    numVariables_++;
    return add_variable<UInt32Variable>(name, numVariables_ - 1, numVariables_, getter, setter);
}

Int64Variable &fmu_base::register_int64(const std::string &name, int64_t *ptr, const std::function<void()> &onChange) {
    numVariables_++;
    return add_variable<Int64Variable>(name, numVariables_ - 1, numVariables_, ptr, onChange);
}

Int64Variable &fmu_base::register_int64(const std::string &name, const std::function<int64_t()> &getter, const std::optional<std::function<void(int64_t)>> &setter) {
    numVariables_++;
    return add_variable<Int64Variable>(name, numVariables_ - 1, numVariables_, getter, setter);
}

UInt64Variable &fmu_base::register_uint64(const std::string &name, uint64_t *ptr, const std::function<void()> &onChange) {
    numVariables_++;
    return add_variable<UInt64Variable>(name, numVariables_ - 1, numVariables_, ptr, onChange);
}

UInt64Variable &fmu_base::register_uint64(const std::string &name, const std::function<uint64_t()> &getter, const std::optional<std::function<void(uint64_t)>> &setter) {
    numVariables_++;
    return add_variable<UInt64Variable>(name, numVariables_ - 1, numVariables_, getter, setter);
}

Float32Variable &fmu_base::register_float32(const std::string &name, float *ptr, const std::function<void()> &onChange) {
    numVariables_++;
    return add_variable<Float32Variable>(name, numVariables_ - 1, numVariables_, ptr, onChange);
}

Float32Variable &fmu_base::register_float32(const std::string &name, const std::function<float()> &getter, const std::optional<std::function<void(float)>> &setter) {
    numVariables_++;
    return add_variable<Float32Variable>(name, numVariables_ - 1, numVariables_, getter, setter);
}

Float64Variable &fmu_base::register_float64(const std::string &name, double *ptr, const std::function<void()> &onChange) {
    return register_real(name, ptr, onChange);
}

Float64Variable &fmu_base::register_float64(const std::string &name, const std::function<double()> &getter, const std::optional<std::function<void(double)>> &setter) {
    return register_real(name, getter, setter);
}

ClockVariable &fmu_base::register_clock(const std::string &name, bool *ptr, const std::function<void()> &onChange) {
    numVariables_++;
    return add_variable<ClockVariable>(name, numVariables_ - 1, numVariables_, ptr, onChange);
}

ClockVariable &fmu_base::register_clock(const std::string &name, const std::function<bool()> &getter, const std::optional<std::function<void(bool)>> &setter) {
    numVariables_++;
    return add_variable<ClockVariable>(name, numVariables_ - 1, numVariables_, getter, setter);
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

    for (const auto &v: variables_) {
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
    indices.reserve(variables_.size());
    for (const auto &v: variables_) {
        indices.emplace_back(v->value_reference());
    }

    return indices;
}


void *fmu_base::getFMUState() {
    auto *snap = new fmu_state_snapshot();
    snap->time = time_;
    snap->stop = stop_;
    snap->tolerance = tolerance_;
    snap->pending_events = has_pending_events();
    snap->discrete_states = last_discrete_states_;

    for (const auto &v: variables_) {
        if (v->type() == data_type::CLOCK) {
            auto *cv = static_cast<const ClockVariable *>(v.get());
            snap->clock_states.push_back({cv->value_reference(),
                                          cv->get(),
                                          cv->getIntervalDecimal(),
                                          cv->getShiftDecimal(),
                                          cv->getIntervalQualifier(),
                                          cv->next_tick_time()});
        }
    }

    if (state_ops_ && get_state_ptr_) {
        const void *in_place = get_state_ptr_(this);
        snap->model_state = state_ops_->create_from_state(in_place);
    }
    return snap;
}

void fmu_base::setFmuState(void *state) {
    if (!state) throw fatal_error("setFmuState called with null state");
    auto *snap = static_cast<fmu_state_snapshot *>(state);
    time_ = snap->time;
    stop_ = snap->stop;
    tolerance_ = snap->tolerance;
    set_has_pending_events(snap->pending_events);
    last_discrete_states_ = snap->discrete_states;

    for (const auto &cs: snap->clock_states) {
        auto it = vrToVariable_.find(cs.vr);
        if (it != vrToVariable_.end() && it->second->type() == data_type::CLOCK) {
            auto *cv = static_cast<ClockVariable *>(it->second);
            cv->force_set(cs.active);
            cv->restoreIntervalDecimal(cs.intervalDecimal);
            cv->restoreShiftDecimal(cs.shiftDecimal);
            cv->setIntervalQualifier(cs.intervalQualifier);
            cv->set_next_tick_time(cs.nextTickTime);
        }
    }

    if (state_ops_ && get_state_ptr_ && snap->model_state) {
        void *dst = get_state_ptr_(this);
        state_ops_->assign_into_state(dst, snap->model_state);
    }
}

void fmu_base::freeFmuState(void **state) {
    if (!state || !*state) throw fatal_error("freeFmuState called with null state");
    auto *snap = static_cast<fmu_state_snapshot *>(*state);
    if (snap->model_state && state_ops_) {
        state_ops_->destroy(snap->model_state);
        snap->model_state = nullptr;
    }
    delete snap;
    *state = nullptr;
}

void fmu_base::serializedFMUStateSize(void *state, size_t &size) {
    if (!state_ops_) throw fatal_error("serializedFMUStateSize not implemented");
    size = state_ops_->serialized_size();
}

void fmu_base::serializeFMUState(void *state, std::vector<uint8_t> &out) {
    if (!state_ops_) throw fatal_error("serializeFMUState not implemented");
    const void *ptr = nullptr;
    if (state) {
        auto *snap = static_cast<const fmu_state_snapshot *>(state);
        ptr = snap->model_state;
    } else {
        ptr = get_state_ptr_(this);
    }
    state_ops_->serialize(ptr, out);
}

void fmu_base::deserializeFMUState(const std::vector<uint8_t> &in, void **out) {
    if (!state_ops_) throw fatal_error("deserializeFMUState not implemented");
    void *model_state = nullptr;
    state_ops_->deserialize(in, &model_state);
    auto *snap = new fmu_state_snapshot();
    snap->time = time_;
    snap->stop = stop_;
    snap->tolerance = tolerance_;
    snap->pending_events = has_pending_events();
    snap->discrete_states = last_discrete_states_;
    snap->model_state = model_state;
    *out = snap;
}
