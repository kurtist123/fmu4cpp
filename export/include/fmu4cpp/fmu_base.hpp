
#ifndef FMU4CPP_FMU_BASE_HPP
#define FMU4CPP_FMU_BASE_HPP

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "fmu_base.hpp"
#include "fmu_variable.hpp"
#include "logger.hpp"
#include "model_info.hpp"
#include "status.hpp"


namespace fmu4cpp {

    namespace state {

        // detection idiom for member reset()
        template<typename T, typename = void>
        struct has_reset : std::false_type {};

        template<typename T>
        struct has_reset<T, std::void_t<decltype(std::declval<T &>().reset())>> : std::true_type {};

        // call_reset overloads selected by SFINAE
        template<typename T>
        std::enable_if_t<has_reset<T>::value, void> call_reset(void *dst) {
            static_cast<T *>(dst)->reset();
        }

        template<typename T>
        std::enable_if_t<!has_reset<T>::value, void> call_reset(void *dst) {
            *static_cast<T *>(dst) = T();
        }

        struct Ops {
            void *(*create_from_state)(const void *);
            void (*assign_into_state)(void *, const void *);
            void (*destroy)(void *);
            size_t (*serialized_size)();
            void (*serialize)(const void *, std::vector<uint8_t> &);
            void (*deserialize)(const std::vector<uint8_t> &, void **);
            void (*reset_inplace)(void *);// new: reset the in-place state to its initial/default values
        };

        template<typename State>
        const Ops *make_state_ops() {
            static_assert(std::is_trivially_copyable_v<State>, "State must be trivially copyable");
            static const Ops ops = {
                    // create_from_state
                    +[](const void *p) -> void * { return new State(*static_cast<const State *>(p)); },
                    // assign_into_state
                    +[](void *dst, const void *src) { *static_cast<State *>(dst) = *static_cast<const State *>(src); },
                    // destroy
                    +[](void *p) { delete static_cast<State *>(p); },
                    // serialized_size
                    +[]() -> size_t { return sizeof(State); },
                    // serialize
                    +[](const void *p, std::vector<uint8_t> &out) {
                        const auto *b = static_cast<const uint8_t *>(p);
                        out.assign(b, b + sizeof(State));
                    },
                    // deserialize
                    +[](const std::vector<uint8_t> &in, void **out) {
                        auto *s = new State();
                        std::memcpy(s, in.data(), sizeof(State));
                        *out = s;
                    },
                    // reset_inplace: dispatch to call_reset<State>
                    +[](void *dst) { call_reset<State>(dst); }};
            return &ops;
        }

    }// namespace state

    struct fmu_data {
        logger *fmiLogger{nullptr};
        std::string instanceName{};
        std::filesystem::path resourceLocation{};
        bool visible{false};
    };

    class fmu_base {

    public:
        explicit fmu_base(fmu_data data);

        fmu_base(const fmu_base &) = delete;
        fmu_base(const fmu_base &&) = delete;

        [[nodiscard]] std::string instanceName() const {
            return data_.instanceName;
        }

        [[nodiscard]] const std::filesystem::path &resourceLocation() const {
            return data_.resourceLocation;
        }

        [[nodiscard]] bool visible() const {
            return data_.visible;
        }

        [[nodiscard]] std::optional<IntVariable> get_int_variable(const std::string &name) const;
        [[nodiscard]] std::optional<RealVariable> get_real_variable(const std::string &name) const;
        [[nodiscard]] std::optional<BoolVariable> get_bool_variable(const std::string &name) const;
        [[nodiscard]] std::optional<StringVariable> get_string_variable(const std::string &name) const;
        [[nodiscard]] std::optional<BinaryVariable> get_binary_variable(const std::string &name) const;
        [[nodiscard]] std::optional<ClockVariable> get_clock_variable(const std::string &name) const;
        [[nodiscard]] std::optional<Float32Variable> get_float32_variable(const std::string &name) const;
        [[nodiscard]] std::optional<Int8Variable> get_int8_variable(const std::string &name) const;
        [[nodiscard]] std::optional<UInt8Variable> get_uint8_variable(const std::string &name) const;
        [[nodiscard]] std::optional<Int16Variable> get_int16_variable(const std::string &name) const;
        [[nodiscard]] std::optional<UInt16Variable> get_uint16_variable(const std::string &name) const;
        [[nodiscard]] std::optional<UInt32Variable> get_uint32_variable(const std::string &name) const;
        [[nodiscard]] std::optional<Int64Variable> get_int64_variable(const std::string &name) const;
        [[nodiscard]] std::optional<UInt64Variable> get_uint64_variable(const std::string &name) const;
        [[nodiscard]] const std::vector<ClockVariable> &get_clocks() const { return clocks_; }

        void enter_initialisation_mode(double start, std::optional<double> stop, std::optional<double> tolerance);
        virtual void exit_initialisation_mode();
        bool step(double currentTime, double dt);
        virtual void terminate();
        virtual void reset();

        virtual void on_enter_event_mode() noexcept {}
        virtual void on_exit_event_mode() noexcept {}
        virtual void update_discrete_states(bool &discreteStatesNeedUpdate, bool &terminateSimulation) {}

        void notify_output_event(value_ref clock_vr);
        [[nodiscard]] bool has_pending_output_events() const noexcept;
        void clear_triggered_clocks() noexcept;

        [[nodiscard]] bool get_clock(value_ref vr) const;
        void set_clock(value_ref vr, bool val);

        void get_integer(const unsigned int vr[], size_t nvr, int value[]) const;
        void get_real(const unsigned int vr[], size_t nvr, double value[]) const;

        void get_float32(const unsigned int vr[], size_t nvr, float value[]) const;
        void get_int8(const unsigned int vr[], size_t nvr, int8_t value[]) const;
        void get_uint8(const unsigned int vr[], size_t nvr, uint8_t value[]) const;
        void get_int16(const unsigned int vr[], size_t nvr, int16_t value[]) const;
        void get_uint16(const unsigned int vr[], size_t nvr, uint16_t value[]) const;
        void get_uint32(const unsigned int vr[], size_t nvr, uint32_t value[]) const;
        void get_int64(const unsigned int vr[], size_t nvr, int64_t value[]) const;
        void get_uint64(const unsigned int vr[], size_t nvr, uint64_t value[]) const;

        //fmi2
        void get_boolean(const unsigned int vr[], size_t nvr, int value[]) const;

        //fmi3
        void get_boolean(const unsigned int vr[], size_t nvr, bool value[]) const;

        void get_string(const unsigned int vr[], size_t nvr, const char *value[]);
        void get_binary(const unsigned int vr[], size_t nvr, size_t valueSizes[], const uint8_t *values[]);

        void set_integer(const unsigned int vr[], size_t nvr, const int value[]);
        void set_real(const unsigned int vr[], size_t nvr, const double value[]);

        void set_float32(const unsigned int vr[], size_t nvr, const float value[]);
        void set_int8(const unsigned int vr[], size_t nvr, const int8_t value[]);
        void set_uint8(const unsigned int vr[], size_t nvr, const uint8_t value[]);
        void set_int16(const unsigned int vr[], size_t nvr, const int16_t value[]);
        void set_uint16(const unsigned int vr[], size_t nvr, const uint16_t value[]);
        void set_uint32(const unsigned int vr[], size_t nvr, const uint32_t value[]);
        void set_int64(const unsigned int vr[], size_t nvr, const int64_t value[]);
        void set_uint64(const unsigned int vr[], size_t nvr, const uint64_t value[]);

        //fmi2
        void set_boolean(const unsigned int vr[], size_t nvr, const int value[]);
        //fmi3
        void set_boolean(const unsigned int vr[], size_t nvr, const bool value[]);

        void set_string(const unsigned int vr[], size_t nvr, const char *const value[]);
        void set_binary(const unsigned int vr[], size_t nvr, const size_t valueSizes[], const uint8_t *const value[]);

        [[nodiscard]] std::string guid() const;
        [[nodiscard]] std::string make_description() const;

        void debugLog(fmiStatus s, const std::string &message) const;

        virtual void *getFMUState();
        virtual void setFmuState(void *state);
        virtual void freeFmuState(void **state);

        virtual void serializedFMUStateSize(void *state, size_t &size);
        virtual void serializeFMUState(void *state, std::vector<uint8_t> &out);
        virtual void deserializeFMUState(const std::vector<uint8_t> &in, void **out);

        [[nodiscard]] std::vector<unsigned int> get_value_refs() const;
        [[nodiscard]] std::vector<const VariableBase *> collect_variables(
                const std::function<bool(const VariableBase &)> &predicate = [](const VariableBase &) { return true; }) const;

        virtual ~fmu_base() = default;

    protected:
        IntVariable &register_integer(const std::string &name, int *ptr, const std::function<void()> &onChange = {});
        IntVariable &register_integer(const std::string &name,
                                      const std::function<int()> &getter,
                                      const std::optional<std::function<void(int)>> &setter = std::nullopt);

        RealVariable &register_real(const std::string &name, double *ptr, const std::function<void()> &onChange = {});
        RealVariable &register_real(const std::string &name,
                                    const std::function<double()> &getter,
                                    const std::optional<std::function<void(double)>> &setter = std::nullopt);

        BoolVariable &register_boolean(const std::string &name, bool *ptr, const std::function<void()> &onChange = {});
        BoolVariable &register_boolean(const std::string &name,
                                       const std::function<bool()> &getter,
                                       const std::optional<std::function<void(bool)>> &setter);

        StringVariable &register_string(const std::string &name, std::string *ptr, const std::function<void()> &onChange = {});
        StringVariable &register_string(const std::string &name,
                                        const std::function<std::string()> &getter,
                                        const std::optional<std::function<void(std::string)>> &setter = std::nullopt);

        BinaryVariable &register_binary(const std::string &name, BinaryType *ptr, const std::function<void()> &onChange = {});
        BinaryVariable &register_binary(const std::string &name,
                                        const std::function<std::vector<uint8_t>()> &getter,
                                        const std::optional<std::function<void(std::vector<uint8_t>)>> &setter = std::nullopt);

        Float32Variable &register_float32(const std::string &name, float *ptr, const std::function<void()> &onChange = {});
        Float32Variable &register_float32(const std::string &name,
                                          const std::function<float()> &getter,
                                          const std::optional<std::function<void(float)>> &setter = std::nullopt);

        Int8Variable &register_int8(const std::string &name, int8_t *ptr, const std::function<void()> &onChange = {});
        Int8Variable &register_int8(const std::string &name,
                                    const std::function<int8_t()> &getter,
                                    const std::optional<std::function<void(int8_t)>> &setter = std::nullopt);

        UInt8Variable &register_uint8(const std::string &name, uint8_t *ptr, const std::function<void()> &onChange = {});
        UInt8Variable &register_uint8(const std::string &name,
                                      const std::function<uint8_t()> &getter,
                                      const std::optional<std::function<void(uint8_t)>> &setter = std::nullopt);

        Int16Variable &register_int16(const std::string &name, int16_t *ptr, const std::function<void()> &onChange = {});
        Int16Variable &register_int16(const std::string &name,
                                      const std::function<int16_t()> &getter,
                                      const std::optional<std::function<void(int16_t)>> &setter = std::nullopt);

        UInt16Variable &register_uint16(const std::string &name, uint16_t *ptr, const std::function<void()> &onChange = {});
        UInt16Variable &register_uint16(const std::string &name,
                                        const std::function<uint16_t()> &getter,
                                        const std::optional<std::function<void(uint16_t)>> &setter = std::nullopt);

        UInt32Variable &register_uint32(const std::string &name, uint32_t *ptr, const std::function<void()> &onChange = {});
        UInt32Variable &register_uint32(const std::string &name,
                                        const std::function<uint32_t()> &getter,
                                        const std::optional<std::function<void(uint32_t)>> &setter = std::nullopt);

        Int64Variable &register_int64(const std::string &name, int64_t *ptr, const std::function<void()> &onChange = {});
        Int64Variable &register_int64(const std::string &name,
                                      const std::function<int64_t()> &getter,
                                      const std::optional<std::function<void(int64_t)>> &setter = std::nullopt);

        UInt64Variable &register_uint64(const std::string &name, uint64_t *ptr, const std::function<void()> &onChange = {});
        UInt64Variable &register_uint64(const std::string &name,
                                        const std::function<uint64_t()> &getter,
                                        const std::optional<std::function<void(uint64_t)>> &setter = std::nullopt);

        ClockVariable &register_clock(std::string name, value_ref vr, causality_t causality);
        ClockVariable &register_clock(std::string name, causality_t causality);

        virtual void enter_initialisation_mode();
        virtual bool do_step(double dt) = 0;

        [[nodiscard]] double currentTime() const {
            return time_;
        }

        [[nodiscard]] std::optional<double> tolerance() const {
            return tolerance_;
        }

        template<typename Model, typename State>
        void register_state(State Model::*stateMember) {
            static_assert(std::is_trivially_copyable_v<State>, "State must be trivially copyable");
            get_state_ptr_ = [stateMember](void *self) -> void * {
                return &(static_cast<Model *>(self)->*stateMember);
            };
            state_ops_ = state::make_state_ops<State>();
        }

    private:
        fmu_data data_;

        double time_{0};
        size_t numVariables_{0};

        std::optional<double> stop_;
        std::optional<double> tolerance_;

        std::vector<IntVariable> integers_;
        std::unordered_map<unsigned int, size_t> vrToIntegerIndices_;

        std::vector<RealVariable> reals_;
        std::unordered_map<unsigned int, size_t> vrToRealIndices_;

        std::vector<BoolVariable> booleans_;
        std::unordered_map<unsigned int, size_t> vrToBooleanIndices_;

        std::vector<StringVariable> strings_;
        std::vector<std::string> stringBuffer_;
        std::unordered_map<unsigned int, size_t> vrToStringIndices_;

        std::vector<BinaryVariable> binary_;
        std::vector<std::vector<uint8_t>> binaryBuffer_;
        std::unordered_map<unsigned int, size_t> vrToBinaryIndices_;

        std::vector<Float32Variable> float32s_;
        std::unordered_map<unsigned int, size_t> vrToFloat32Indices_;

        std::vector<Int8Variable> int8s_;
        std::unordered_map<unsigned int, size_t> vrToInt8Indices_;

        std::vector<UInt8Variable> uint8s_;
        std::unordered_map<unsigned int, size_t> vrToUInt8Indices_;

        std::vector<Int16Variable> int16s_;
        std::unordered_map<unsigned int, size_t> vrToInt16Indices_;

        std::vector<UInt16Variable> uint16s_;
        std::unordered_map<unsigned int, size_t> vrToUInt16Indices_;

        std::vector<UInt32Variable> uint32s_;
        std::unordered_map<unsigned int, size_t> vrToUInt32Indices_;

        std::vector<Int64Variable> int64s_;
        std::unordered_map<unsigned int, size_t> vrToInt64Indices_;

        std::vector<UInt64Variable> uint64s_;
        std::unordered_map<unsigned int, size_t> vrToUInt64Indices_;

        std::vector<ClockVariable> clocks_;
        std::unordered_map<unsigned int, size_t> vrToClockIndices_;

        std::function<void *(void *)> get_state_ptr_{nullptr};
        const state::Ops *state_ops_{nullptr};
    };


#define FMU4CPP_INSTANTIATE(MODELCLASS)                                                         \
    std::unique_ptr<fmu4cpp::fmu_base> fmu4cpp::createInstance(const fmu4cpp::fmu_data &data) { \
        return std::make_unique<MODELCLASS>(data);                                              \
    }

#define FMU4CPP_CTOR(MODELCLASS) \
    explicit MODELCLASS(fmu4cpp::fmu_data data) : fmu_base(std::move(data))

    model_info get_model_info();

    std::unique_ptr<fmu_base> createInstance(const fmu_data &data);

}// namespace fmu4cpp

#endif//FMU4CPP_FMU_BASE_HPP
