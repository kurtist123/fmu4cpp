
#ifndef FMU4CPP_FMU_VARIABLE_HPP
#define FMU4CPP_FMU_VARIABLE_HPP

#include "variable_access.hpp"

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace fmu4cpp {

    using BinaryType = std::vector<uint8_t>;

    enum class causality_t {
        PARAMETER,
        CALCULATED_PARAMETER,
        INPUT,
        OUTPUT,
        LOCAL,
        INDEPENDENT
    };

    enum class variability_t {
        CONSTANT,
        FIXED,
        TUNABLE,
        DISCRETE,
        CONTINUOUS
    };

    enum class initial_t {
        EXACT,
        APPROX,
        CALCULATED,
    };

    enum class interval_variability_t {
        CONSTANT,
        FIXED,
        TUNABLE,
        CHANGING,
        COUNTDOWN,
        TRIGGERED
    };

    enum class interval_qualifier_t {
        INTERVAL_NOT_YET_KNOWN,
        INTERVAL_UNCHANGED,
        INTERVAL_CHANGED
    };

    enum class data_type {
        INT8,
        UINT8,
        INT16,
        UINT16,
        INT32,
        UINT32,
        INT64,
        UINT64,
        FLOAT32,
        FLOAT64,
        BOOLEAN,
        STRING,
        BINARY,
        CLOCK
    };

    std::string to_string(const causality_t &c);
    std::string to_string(const variability_t &v);
    std::string to_string(const initial_t &i);
    std::string to_string(const interval_variability_t &iv);
    std::string to_string(const data_type &dt);

    template<typename T>
    struct type_to_data_type;

    template<>
    struct type_to_data_type<int8_t> {
        static constexpr data_type value = data_type::INT8;
    };
    template<>
    struct type_to_data_type<uint8_t> {
        static constexpr data_type value = data_type::UINT8;
    };
    template<>
    struct type_to_data_type<int16_t> {
        static constexpr data_type value = data_type::INT16;
    };
    template<>
    struct type_to_data_type<uint16_t> {
        static constexpr data_type value = data_type::UINT16;
    };
    template<>
    struct type_to_data_type<int32_t> {
        static constexpr data_type value = data_type::INT32;
    };
    template<>
    struct type_to_data_type<uint32_t> {
        static constexpr data_type value = data_type::UINT32;
    };
    template<>
    struct type_to_data_type<int64_t> {
        static constexpr data_type value = data_type::INT64;
    };
    template<>
    struct type_to_data_type<uint64_t> {
        static constexpr data_type value = data_type::UINT64;
    };
    template<>
    struct type_to_data_type<float> {
        static constexpr data_type value = data_type::FLOAT32;
    };
    template<>
    struct type_to_data_type<double> {
        static constexpr data_type value = data_type::FLOAT64;
    };
    template<>
    struct type_to_data_type<bool> {
        static constexpr data_type value = data_type::BOOLEAN;
    };
    template<>
    struct type_to_data_type<std::string> {
        static constexpr data_type value = data_type::STRING;
    };
    template<>
    struct type_to_data_type<BinaryType> {
        static constexpr data_type value = data_type::BINARY;
    };

    template<typename T>
    std::string format_numeric(T val) {
        if constexpr (std::is_floating_point_v<T>) {
            std::ostringstream oss;
            oss.precision(std::numeric_limits<T>::max_digits10);
            oss << val;
            return oss.str();
        } else {
            return std::to_string(+val);
        }
    }

    class VariableBase {

    protected:
        causality_t causality_ = causality_t::LOCAL;
        std::optional<variability_t> variability_;
        std::optional<initial_t> initial_;
        std::vector<std::string> annotations_;
        std::vector<std::string> dependencies_;
        std::string description_;
        std::optional<std::string> declaredType_;
        std::vector<size_t> dimensions_;
        std::vector<unsigned int> clocks_;

    public:
        VariableBase(std::string name, unsigned int vr, size_t index)
            : name_(std::move(name)), vr_(vr), index_(index) {}

        [[nodiscard]] const std::string &name() const {
            return name_;
        }

        [[nodiscard]] unsigned int value_reference() const {
            return vr_;
        }

        [[nodiscard]] size_t index() const {
            return index_;
        }

        [[nodiscard]] causality_t causality() const {
            return causality_;
        }

        [[nodiscard]] std::optional<variability_t> variability() const {
            return variability_;
        }

        [[nodiscard]] std::optional<initial_t> initial() const {
            return initial_;
        }

        [[nodiscard]] std::string getDescription() const {
            return description_;
        }

        [[nodiscard]] std::vector<std::string> getDependencies() const {
            if (causality_ != causality_t::OUTPUT) {
                throw std::logic_error("Can only declare dependencies for outputs!");
            }
            return dependencies_;
        }

        [[nodiscard]] std::vector<std::string> getAnnotations() const {
            return annotations_;
        }

        virtual ~VariableBase() = default;

        [[nodiscard]] std::optional<std::string> getDeclaredType() const {
            return declaredType_;
        }

        [[nodiscard]] const std::vector<size_t> &dimensions() const {
            return dimensions_;
        }

        [[nodiscard]] bool is_array() const {
            return !dimensions_.empty();
        }

        [[nodiscard]] size_t flattened_size() const {
            if (dimensions_.empty()) return 1;
            size_t s = 1;
            for (size_t d: dimensions_) s *= d;
            return s;
        }

        [[nodiscard]] const std::vector<unsigned int> &clocks() const {
            return clocks_;
        }

        [[nodiscard]] virtual data_type type() const = 0;
        [[nodiscard]] virtual std::string type_name() const = 0;

        [[nodiscard]] virtual std::optional<std::string> get_start_as_string() const { return std::nullopt; }
        [[nodiscard]] virtual std::optional<std::string> get_min_as_string() const { return std::nullopt; }
        [[nodiscard]] virtual std::optional<std::string> get_max_as_string() const { return std::nullopt; }
        [[nodiscard]] virtual std::optional<std::string> getUnit() const { return std::nullopt; }
        [[nodiscard]] virtual std::optional<std::string> getDisplayUnit() const { return std::nullopt; }
        [[nodiscard]] virtual std::optional<std::string> getQuantity() const { return std::nullopt; }

    private:
        std::string name_;
        unsigned int vr_;
        size_t index_;
    };

    template<typename T>
    class TypedVariableBase : public VariableBase {

    public:
        TypedVariableBase(
                std::string name,
                unsigned int vr, size_t index, T *ptr, const std::function<void()> &onChange)
            : VariableBase(std::move(name), vr, index), access_(std::make_unique<PtrAccess<T>>(ptr, onChange)) {}

        TypedVariableBase(
                std::string name,
                unsigned int vr, size_t index,
                std::function<T()> getter,
                std::optional<std::function<void(T)>> setter)
            : VariableBase(std::move(name), vr, index),
              access_(std::make_unique<LambdaAccess<T>>(std::move(getter), std::move(setter))) {}

        [[nodiscard]] T get() const {

            return access_->get();
        }

        void set(T value) {
            if (causality_ == causality_t::LOCAL || (causality_ == causality_t::OUTPUT && initial_ != initial_t::EXACT) || causality_ == causality_t::INDEPENDENT) {
                throw std::logic_error("Cannot set value for variable with causality: " + to_string(causality_));
            }

            access_->set(value);
        }

        void force_set(T value) {
            access_->set(value);
        }

        void read_values(T *dest, size_t count) const {
            access_->read(dest, count);
        }

        void write_values(const T *src, size_t count) {
            if (causality_ == causality_t::LOCAL || (causality_ == causality_t::OUTPUT && initial_ != initial_t::EXACT) || causality_ == causality_t::INDEPENDENT) {
                throw std::logic_error("Cannot set value for variable with causality: " + to_string(causality_));
            }
            access_->write(src, count);
        }

    private:
        std::shared_ptr<VariableAccess<T>> access_;
    };

    template<class T, class V>
    class Variable : public TypedVariableBase<T> {

    public:
        using TypedVariableBase<T>::TypedVariableBase;

        V &setDescription(const std::string &description) {
            this->description_ = description;
            return *static_cast<V *>(this);
        }

        V &setCausality(causality_t causality) {
            this->causality_ = causality;
            return *static_cast<V *>(this);
        }

        V &setVariability(variability_t variability) {
            this->variability_ = variability;
            return *static_cast<V *>(this);
        }

        V &setInitial(initial_t initial) {
            this->initial_ = initial;
            return *static_cast<V *>(this);
        }

        V &setDependencies(const std::vector<std::string> &dependencies) {
            for (const auto &i: dependencies) {
                this->dependencies_.emplace_back(i);
            }
            return *static_cast<V *>(this);
        }

        V &setAnnotations(const std::vector<std::string> &annotations) {
            for (const auto &i: annotations) {
                this->annotations_.emplace_back(i);
            }
            return *static_cast<V *>(this);
        }

        V &addAnnotation(const std::string &annotation) {
            this->annotations_.emplace_back(annotation);
            return *static_cast<V *>(this);
        }

        V &setDeclaredType(const std::string &declaredType) {
            this->declaredType_ = declaredType;
            return *static_cast<V *>(this);
        }

        V &setDimensions(std::vector<size_t> dimensions) {
            this->dimensions_ = std::move(dimensions);
            return *static_cast<V *>(this);
        }

        V &setClocks(std::vector<unsigned int> clocks) {
            this->clocks_ = std::move(clocks);
            return *static_cast<V *>(this);
        }
    };

    template<class T, class V>
    class NumericVariable : public Variable<T, V> {

    public:
        using Variable<T, V>::Variable;

        [[nodiscard]] std::optional<T> getMin() const {
            return min_;
        }

        [[nodiscard]] std::optional<T> getMax() const {
            return max_;
        }

        [[nodiscard]] std::optional<std::string> getUnit() const override {
            return unit_;
        }

        [[nodiscard]] std::optional<std::string> getDisplayUnit() const override {
            return displayUnit_;
        }

        [[nodiscard]] std::optional<std::string> getQuantity() const override {
            return quantity_;
        }

        [[nodiscard]] std::optional<std::string> get_start_as_string() const override {
            return format_numeric(this->get());
        }

        [[nodiscard]] std::optional<std::string> get_min_as_string() const override {
            if (min_) return format_numeric(*min_);
            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::string> get_max_as_string() const override {
            if (max_) return format_numeric(*max_);
            return std::nullopt;
        }

        V &setMin(const std::optional<T> &min) {
            min_ = min;
            return *static_cast<V *>(this);
        }

        V &setMax(const std::optional<T> &max) {
            max_ = max;
            return *static_cast<V *>(this);
        }

        V &setUnit(const std::optional<std::string> &unit) {
            unit_ = unit;
            return *static_cast<V *>(this);
        }

        V &setDisplayUnit(const std::optional<std::string> &displayUnit) {
            displayUnit_ = displayUnit;
            return *static_cast<V *>(this);
        }

        V &setQuantity(const std::optional<std::string> &quantity) {
            quantity_ = quantity;
            return *static_cast<V *>(this);
        }

    private:
        std::optional<T> min_;
        std::optional<T> max_;
        std::optional<std::string> unit_;
        std::optional<std::string> displayUnit_;
        std::optional<std::string> quantity_;
    };

    class Int8Variable final : public NumericVariable<int8_t, Int8Variable> {
    public:
        using NumericVariable::NumericVariable;
        [[nodiscard]] data_type type() const override { return data_type::INT8; }
        [[nodiscard]] std::string type_name() const override { return "Int8"; }
    };

    class UInt8Variable final : public NumericVariable<uint8_t, UInt8Variable> {
    public:
        using NumericVariable::NumericVariable;
        [[nodiscard]] data_type type() const override { return data_type::UINT8; }
        [[nodiscard]] std::string type_name() const override { return "UInt8"; }
    };

    class Int16Variable final : public NumericVariable<int16_t, Int16Variable> {
    public:
        using NumericVariable::NumericVariable;
        [[nodiscard]] data_type type() const override { return data_type::INT16; }
        [[nodiscard]] std::string type_name() const override { return "Int16"; }
    };

    class UInt16Variable final : public NumericVariable<uint16_t, UInt16Variable> {
    public:
        using NumericVariable::NumericVariable;
        [[nodiscard]] data_type type() const override { return data_type::UINT16; }
        [[nodiscard]] std::string type_name() const override { return "UInt16"; }
    };

    class Int32Variable : public NumericVariable<int32_t, Int32Variable> {
    public:
        using NumericVariable::NumericVariable;
        [[nodiscard]] data_type type() const override { return data_type::INT32; }
        [[nodiscard]] std::string type_name() const override { return "Int32"; }
    };

    class UInt32Variable final : public NumericVariable<uint32_t, UInt32Variable> {
    public:
        using NumericVariable::NumericVariable;
        [[nodiscard]] data_type type() const override { return data_type::UINT32; }
        [[nodiscard]] std::string type_name() const override { return "UInt32"; }
    };

    class Int64Variable final : public NumericVariable<int64_t, Int64Variable> {
    public:
        using NumericVariable::NumericVariable;
        [[nodiscard]] data_type type() const override { return data_type::INT64; }
        [[nodiscard]] std::string type_name() const override { return "Int64"; }
    };

    class UInt64Variable final : public NumericVariable<uint64_t, UInt64Variable> {
    public:
        using NumericVariable::NumericVariable;
        [[nodiscard]] data_type type() const override { return data_type::UINT64; }
        [[nodiscard]] std::string type_name() const override { return "UInt64"; }
    };

    class Float32Variable final : public NumericVariable<float, Float32Variable> {
    public:
        Float32Variable(
                std::string name,
                unsigned int vr, size_t index, float *ptr, const std::function<void()> &onChange)
            : NumericVariable(std::move(name), vr, index, ptr, onChange) {
            variability_ = variability_t::CONTINUOUS;
        }

        Float32Variable(
                std::string name,
                unsigned int vr, size_t index,
                std::function<float()> getter,
                std::optional<std::function<void(float)>> setter)
            : NumericVariable(std::move(name), vr, index, std::move(getter), std::move(setter)) {
            variability_ = variability_t::CONTINUOUS;
        }

        [[nodiscard]] data_type type() const override { return data_type::FLOAT32; }
        [[nodiscard]] std::string type_name() const override { return "Float32"; }
    };

    class Float64Variable : public NumericVariable<double, Float64Variable> {
    public:
        Float64Variable(
                std::string name,
                unsigned int vr, size_t index, double *ptr, const std::function<void()> &onChange)
            : NumericVariable(std::move(name), vr, index, ptr, onChange) {
            variability_ = variability_t::CONTINUOUS;
        }

        Float64Variable(
                std::string name,
                unsigned int vr, size_t index,
                std::function<double()> getter,
                std::optional<std::function<void(double)>> setter)
            : NumericVariable(std::move(name), vr, index, std::move(getter), std::move(setter)) {
            variability_ = variability_t::CONTINUOUS;
        }

        [[nodiscard]] data_type type() const override { return data_type::FLOAT64; }
        [[nodiscard]] std::string type_name() const override { return "Float64"; }
    };

    // Aliases for FMI 2 / legacy API backwards compatibility
    using IntVariable = Int32Variable;
    using RealVariable = Float64Variable;

    class BoolVariable final : public Variable<bool, BoolVariable> {
    public:
        using Variable::Variable;
        [[nodiscard]] data_type type() const override { return data_type::BOOLEAN; }
        [[nodiscard]] std::string type_name() const override { return "Boolean"; }
        [[nodiscard]] std::optional<std::string> get_start_as_string() const override {
            return this->get() ? "true" : "false";
        }
    };

    class StringVariable final : public Variable<std::string, StringVariable> {
    public:
        using Variable::Variable;
        [[nodiscard]] data_type type() const override { return data_type::STRING; }
        [[nodiscard]] std::string type_name() const override { return "String"; }
        [[nodiscard]] std::optional<std::string> get_start_as_string() const override {
            return this->get();
        }
    };

    class BinaryVariable final : public Variable<BinaryType, BinaryVariable> {
    public:
        using Variable::Variable;

        [[nodiscard]] data_type type() const override { return data_type::BINARY; }
        [[nodiscard]] std::string type_name() const override { return "Binary"; }

        [[nodiscard]] std::optional<std::string> get_start_as_string() const override {
            const auto &bytes = this->get();
            static const char hex[] = "0123456789ABCDEF";
            std::string out;
            out.reserve(bytes.size() * 2);
            for (uint8_t c: bytes) {
                out.push_back(hex[c >> 4]);
                out.push_back(hex[c & 0x0F]);
            }
            return out;
        }

        [[nodiscard]] std::optional<std::string> getMimeType() const { return mimeType_; }
        BinaryVariable &setMimeType(const std::optional<std::string> &mimeType) {
            mimeType_ = mimeType;
            return *this;
        }

    private:
        std::optional<std::string> mimeType_;
    };

    class ClockVariable final : public Variable<bool, ClockVariable> {
    public:
        using Variable::Variable;

        [[nodiscard]] data_type type() const override { return data_type::CLOCK; }
        [[nodiscard]] std::string type_name() const override { return "Clock"; }

        [[nodiscard]] std::optional<interval_variability_t> getIntervalVariability() const { return intervalVariability_; }
        [[nodiscard]] std::optional<double> getIntervalDecimal() const { return intervalDecimal_; }
        [[nodiscard]] std::optional<double> getShiftDecimal() const { return shiftDecimal_; }
        [[nodiscard]] bool supportsFraction() const { return supportsFraction_; }
        [[nodiscard]] std::optional<uint64_t> getResolution() const { return resolution_; }
        [[nodiscard]] std::optional<uint64_t> getIntervalCounter() const { return intervalCounter_; }
        [[nodiscard]] std::optional<uint64_t> getShiftCounter() const { return shiftCounter_; }
        [[nodiscard]] std::optional<uint32_t> getPriority() const { return priority_; }
        [[nodiscard]] bool canBeDeactivated() const { return canBeDeactivated_; }
        [[nodiscard]] interval_qualifier_t getIntervalQualifier() const { return intervalQualifier_; }

        [[nodiscard]] bool is_time_based() const {
            return intervalVariability_.has_value() &&
                   *intervalVariability_ != interval_variability_t::TRIGGERED;
        }

        [[nodiscard]] std::optional<double> next_tick_time() const { return nextTickTime_; }
        void set_next_tick_time(const std::optional<double> &nt) { nextTickTime_ = nt; }

        void compute_initial_tick_time(double startTime) {
            if (!is_time_based()) {
                nextTickTime_ = std::nullopt;
                return;
            }
            if (shiftDecimal_.has_value() && *shiftDecimal_ > 0.0) {
                nextTickTime_ = startTime + *shiftDecimal_;
            } else if (intervalDecimal_.has_value() && *intervalDecimal_ > 0.0) {
                nextTickTime_ = startTime + *intervalDecimal_;
            } else {
                nextTickTime_ = std::nullopt;
            }
        }

        void advance_tick() {
            if (nextTickTime_.has_value() && intervalDecimal_.has_value() && *intervalDecimal_ > 0.0) {
                *nextTickTime_ += *intervalDecimal_;
            }
        }

        ClockVariable &setIntervalVariability(interval_variability_t iv) {
            intervalVariability_ = iv;
            return *this;
        }
        ClockVariable &setIntervalDecimal(double d) {
            if (!initialIntervalDecimal_.has_value()) {
                initialIntervalDecimal_ = d;
            }
            intervalDecimal_ = d;
            intervalQualifier_ = interval_qualifier_t::INTERVAL_CHANGED;
            return *this;
        }
        ClockVariable &setIntervalQualifier(interval_qualifier_t q) {
            intervalQualifier_ = q;
            return *this;
        }
        void resetIntervalQualifier() {
            intervalQualifier_ = interval_qualifier_t::INTERVAL_UNCHANGED;
        }
        ClockVariable &setShiftDecimal(double s) {
            if (!initialShiftDecimal_.has_value()) {
                initialShiftDecimal_ = s;
            }
            shiftDecimal_ = s;
            return *this;
        }
        void restoreIntervalDecimal(const std::optional<double> &d) {
            intervalDecimal_ = d;
        }
        void restoreShiftDecimal(const std::optional<double> &s) {
            shiftDecimal_ = s;
        }
        void reset() {
            force_set(false);
            resetIntervalQualifier();
            intervalDecimal_ = initialIntervalDecimal_;
            shiftDecimal_ = initialShiftDecimal_;
            nextTickTime_ = std::nullopt;
        }
        ClockVariable &setSupportsFraction(bool sf) {
            supportsFraction_ = sf;
            return *this;
        }
        ClockVariable &setResolution(uint64_t r) {
            resolution_ = r;
            return *this;
        }
        ClockVariable &setIntervalCounter(uint64_t c) {
            intervalCounter_ = c;
            return *this;
        }
        ClockVariable &setShiftCounter(uint64_t s) {
            shiftCounter_ = s;
            return *this;
        }
        ClockVariable &setPriority(uint32_t p) {
            priority_ = p;
            return *this;
        }
        ClockVariable &setCanBeDeactivated(bool cbd) {
            canBeDeactivated_ = cbd;
            return *this;
        }

    private:
        std::optional<interval_variability_t> intervalVariability_;
        std::optional<double> intervalDecimal_;
        std::optional<double> shiftDecimal_;
        std::optional<double> initialIntervalDecimal_;
        std::optional<double> initialShiftDecimal_;
        std::optional<double> nextTickTime_;
        bool supportsFraction_{false};
        std::optional<uint64_t> resolution_;
        std::optional<uint64_t> intervalCounter_;
        std::optional<uint64_t> shiftCounter_;
        std::optional<uint32_t> priority_;
        bool canBeDeactivated_{false};
        interval_qualifier_t intervalQualifier_{interval_qualifier_t::INTERVAL_UNCHANGED};
    };

    bool requires_start(const VariableBase &v);

}// namespace fmu4cpp

#endif//FMU4CPP_FMU_VARIABLE_HPP
