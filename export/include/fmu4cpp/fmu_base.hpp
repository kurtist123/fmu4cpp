
#ifndef FMU4CPP_FMU_BASE_HPP
#define FMU4CPP_FMU_BASE_HPP

#include "fmu4cpp/fmu_variable.hpp"
#include "fmu4cpp/logger.hpp"
#include "fmu4cpp/model_info.hpp"

#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fmu4cpp {

    namespace state {

        template<typename State>
        void call_reset(void *dst) {
            static_cast<State *>(dst)->~State();
            new (dst) State();
        }

        struct Ops {
            void *(*create_from_state)(const void *);
            void (*assign_into_state)(void *, const void *);
            void (*destroy)(void *);
            size_t (*serialized_size)();
            void (*serialize)(const void *, std::vector<uint8_t> &);
            void (*deserialize)(const std::vector<uint8_t> &, void **);
            void (*reset_inplace)(void *);// reset the in-place state to its initial/default values
        };

        template<typename State>
        const Ops *make_state_ops() {
            static const Ops ops{
                    // create_from_state
                    +[](const void *src) -> void * {
                        return new State(*static_cast<const State *>(src));
                    },
                    // assign_into_state
                    +[](void *dst, const void *src) {
                        *static_cast<State *>(dst) = *static_cast<const State *>(src);
                    },
                    // destroy
                    +[](void *p) {
                        delete static_cast<State *>(p);
                    },
                    // serialized_size
                    +[]() -> size_t {
                        return sizeof(State);
                    },
                    // serialize
                    +[](const void *state, std::vector<uint8_t> &out) {
                        out.resize(sizeof(State));
                        std::memcpy(out.data(), state, sizeof(State));
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

    struct clock_snapshot {
        unsigned int vr{0};
        bool active{false};
        std::optional<double> intervalDecimal{std::nullopt};
        std::optional<double> shiftDecimal{std::nullopt};
        interval_qualifier_t intervalQualifier{interval_qualifier_t::INTERVAL_UNCHANGED};
    };

    struct discrete_states_info {
        bool discreteStatesNeedUpdate{false};
        bool terminateSimulation{false};
        bool nominalsOfContinuousStatesChanged{false};
        bool valuesOfContinuousStatesChanged{false};
        bool nextEventTimeDefined{false};
        double nextEventTime{0.0};
    };

    struct fmu_state_snapshot {
        double time{0.0};
        std::optional<double> stop{std::nullopt};
        std::optional<double> tolerance{std::nullopt};
        void *model_state{nullptr};

        std::vector<clock_snapshot> clock_states{};
        bool pending_events{false};
        discrete_states_info discrete_states{};
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

        [[nodiscard]] const std::vector<std::unique_ptr<VariableBase>> &variables() const {
            return variables_;
        }

        [[nodiscard]] const VariableBase *get_variable(const std::string &name) const;
        [[nodiscard]] const VariableBase *get_variable(unsigned int vr) const;

        template<typename VarType>
        [[nodiscard]] std::optional<VarType> get_variable(const std::string &name) const {
            auto it = nameToVariable_.find(name);
            if (it != nameToVariable_.end()) {
                if (auto p = dynamic_cast<const VarType *>(it->second)) {
                    return *p;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<IntVariable> get_int_variable(const std::string &name) const;
        [[nodiscard]] std::optional<RealVariable> get_real_variable(const std::string &name) const;
        [[nodiscard]] std::optional<BoolVariable> get_bool_variable(const std::string &name) const;
        [[nodiscard]] std::optional<StringVariable> get_string_variable(const std::string &name) const;
        [[nodiscard]] std::optional<BinaryVariable> get_binary_variable(const std::string &name) const;
        [[nodiscard]] std::optional<ClockVariable> get_clock_variable(const std::string &name) const;

        [[nodiscard]] bool has_clocks() const;
        [[nodiscard]] bool has_event_mode() const;

        void enter_initialisation_mode(double start, std::optional<double> stop, std::optional<double> tolerance);
        virtual void exit_initialisation_mode();
        bool step(double currentTime, double dt);
        virtual void terminate();
        virtual void reset();

        virtual void enter_event_mode() {}
        virtual void update_discrete_states(discrete_states_info &info) {}
        virtual void enter_step_mode() {}
        [[nodiscard]] virtual bool has_pending_events() const { return pending_events_; }
        void set_has_pending_events(bool pending) { pending_events_ = pending; }
        [[nodiscard]] const discrete_states_info &get_discrete_states_info() const { return last_discrete_states_; }
        void set_discrete_states_info(const discrete_states_info &info) { last_discrete_states_ = info; }
        virtual void on_clock_activated(unsigned int vr) {}
        virtual void on_clock_deactivated(unsigned int vr) {}
        virtual void on_interval_changed(unsigned int vr, double interval) {}

        void get_clock(const unsigned int vr[], size_t nvr, bool value[]) const;
        void set_clock(const unsigned int vr[], size_t nvr, const bool value[]);
        void get_interval_decimal(const unsigned int vr[], size_t nvr, double intervals[], interval_qualifier_t qualifiers[]) const;
        void set_interval_decimal(const unsigned int vr[], size_t nvr, const double intervals[]);

        void get_integer(const unsigned int vr[], size_t nvr, int value[]) const;
        void get_real(const unsigned int vr[], size_t nvr, double value[]) const;

        //fmi2
        void get_boolean(const unsigned int vr[], size_t nvr, int value[]) const;

        //fmi3
        void get_boolean(const unsigned int vr[], size_t nvr, bool value[]) const;

        void get_string(const unsigned int vr[], size_t nvr, const char *value[]);
        void get_binary(const unsigned int vr[], size_t nvr, size_t valueSizes[], const uint8_t *values[]);

        void set_integer(const unsigned int vr[], size_t nvr, const int value[]);
        void set_real(const unsigned int vr[], size_t nvr, const double value[]);

        //fmi2
        void set_boolean(const unsigned int vr[], size_t nvr, const int value[]);
        //fmi3
        void set_boolean(const unsigned int vr[], size_t nvr, const bool value[]);

        void set_string(const unsigned int vr[], size_t nvr, const char *const value[]);
        void set_binary(const unsigned int vr[], size_t nvr, const size_t valueSizes[], const uint8_t *const value[]);

        template<typename T>
        void get_values(const unsigned int vr[], size_t nvr, T value[], size_t nValues) const {
            size_t totalExpected = 0;
            for (size_t i = 0; i < nvr; ++i) {
                auto it = vrToVariable_.find(vr[i]);
                if (it == vrToVariable_.end()) {
                    throw std::out_of_range("Invalid valueReference: " + std::to_string(vr[i]));
                }
                totalExpected += it->second->flattened_size();
            }
            if (totalExpected != nValues) {
                throw std::invalid_argument("nValues (" + std::to_string(nValues) +
                                            ") does not match expected element count (" +
                                            std::to_string(totalExpected) + ")");
            }

            size_t offset = 0;
            for (size_t i = 0; i < nvr; ++i) {
                auto it = vrToVariable_.find(vr[i]);
                auto *var = dynamic_cast<const TypedVariableBase<T> *>(it->second);
                if (!var) {
                    throw std::invalid_argument("Type mismatch for valueReference " + std::to_string(vr[i]));
                }
                const size_t cnt = var->flattened_size();
                var->read_values(value + offset, cnt);
                offset += cnt;
            }
        }

        template<typename T>
        void set_values(const unsigned int vr[], size_t nvr, const T value[], size_t nValues) {
            size_t totalExpected = 0;
            for (size_t i = 0; i < nvr; ++i) {
                auto it = vrToVariable_.find(vr[i]);
                if (it == vrToVariable_.end()) {
                    throw std::out_of_range("Invalid valueReference: " + std::to_string(vr[i]));
                }
                totalExpected += it->second->flattened_size();
            }
            if (totalExpected != nValues) {
                throw std::invalid_argument("nValues (" + std::to_string(nValues) +
                                            ") does not match expected element count (" +
                                            std::to_string(totalExpected) + ")");
            }

            size_t offset = 0;
            for (size_t i = 0; i < nvr; ++i) {
                auto it = vrToVariable_.find(vr[i]);
                auto *var = dynamic_cast<TypedVariableBase<T> *>(it->second);
                if (!var) {
                    throw std::invalid_argument("Type mismatch for valueReference " + std::to_string(vr[i]));
                }
                const size_t cnt = var->flattened_size();
                var->write_values(value + offset, cnt);
                offset += cnt;
            }
        }

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

        template<typename VarClass, typename... Args>
        VarClass &add_variable(Args &&...args) {
            auto var = std::make_unique<VarClass>(std::forward<Args>(args)...);
            auto *ptr = var.get();
            vrToVariable_.emplace(ptr->value_reference(), ptr);
            nameToVariable_.emplace(ptr->name(), ptr);
            variables_.emplace_back(std::move(var));
            return *ptr;
        }

        // Sized integer types
        Int8Variable &register_int8(const std::string &name,
                                    int8_t *ptr,
                                    const std::function<void()> &onChange = nullptr);
        Int8Variable &register_int8(const std::string &name,
                                    const std::function<int8_t()> &getter,
                                    const std::optional<std::function<void(int8_t)>> &setter = std::nullopt);

        UInt8Variable &register_uint8(const std::string &name,
                                      uint8_t *ptr,
                                      const std::function<void()> &onChange = nullptr);
        UInt8Variable &register_uint8(const std::string &name,
                                      const std::function<uint8_t()> &getter,
                                      const std::optional<std::function<void(uint8_t)>> &setter = std::nullopt);

        Int16Variable &register_int16(const std::string &name,
                                      int16_t *ptr,
                                      const std::function<void()> &onChange = nullptr);
        Int16Variable &register_int16(const std::string &name,
                                      const std::function<int16_t()> &getter,
                                      const std::optional<std::function<void(int16_t)>> &setter = std::nullopt);

        UInt16Variable &register_uint16(const std::string &name,
                                        uint16_t *ptr,
                                        const std::function<void()> &onChange = nullptr);
        UInt16Variable &register_uint16(const std::string &name,
                                        const std::function<uint16_t()> &getter,
                                        const std::optional<std::function<void(uint16_t)>> &setter = std::nullopt);

        Int32Variable &register_int32(const std::string &name,
                                      int32_t *ptr,
                                      const std::function<void()> &onChange = nullptr);
        Int32Variable &register_int32(const std::string &name,
                                      const std::function<int32_t()> &getter,
                                      const std::optional<std::function<void(int32_t)>> &setter = std::nullopt);

        UInt32Variable &register_uint32(const std::string &name,
                                        uint32_t *ptr,
                                        const std::function<void()> &onChange = nullptr);
        UInt32Variable &register_uint32(const std::string &name,
                                        const std::function<uint32_t()> &getter,
                                        const std::optional<std::function<void(uint32_t)>> &setter = std::nullopt);

        Int64Variable &register_int64(const std::string &name,
                                      int64_t *ptr,
                                      const std::function<void()> &onChange = nullptr);
        Int64Variable &register_int64(const std::string &name,
                                      const std::function<int64_t()> &getter,
                                      const std::optional<std::function<void(int64_t)>> &setter = std::nullopt);

        UInt64Variable &register_uint64(const std::string &name,
                                        uint64_t *ptr,
                                        const std::function<void()> &onChange = nullptr);
        UInt64Variable &register_uint64(const std::string &name,
                                        const std::function<uint64_t()> &getter,
                                        const std::optional<std::function<void(uint64_t)>> &setter = std::nullopt);

        // Floating-point types
        Float32Variable &register_float32(const std::string &name,
                                          float *ptr,
                                          const std::function<void()> &onChange = nullptr);
        Float32Variable &register_float32(const std::string &name,
                                          const std::function<float()> &getter,
                                          const std::optional<std::function<void(float)>> &setter = std::nullopt);

        Float64Variable &register_float64(const std::string &name,
                                          double *ptr,
                                          const std::function<void()> &onChange = nullptr);
        Float64Variable &register_float64(const std::string &name,
                                          const std::function<double()> &getter,
                                          const std::optional<std::function<void(double)>> &setter = std::nullopt);

        ClockVariable &register_clock(const std::string &name,
                                      bool *ptr,
                                      const std::function<void()> &onChange = nullptr);
        ClockVariable &register_clock(const std::string &name,
                                      const std::function<bool()> &getter,
                                      const std::optional<std::function<void(bool)>> &setter = std::nullopt);

        virtual void enter_initialisation_mode();
        virtual bool do_step(double dt) = 0;

        [[nodiscard]] double currentTime() const {
            return time_;
        }

        [[nodiscard]] std::optional<double> tolerance() const {
            return tolerance_;
        }

        [[nodiscard]] std::optional<double> stopTime() const {
            return stop_;
        }

        void set_current_time(double t) {
            time_ = t;
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

        bool pending_events_{false};
        discrete_states_info last_discrete_states_{};

        std::vector<std::unique_ptr<VariableBase>> variables_;
        std::unordered_map<unsigned int, VariableBase *> vrToVariable_;
        std::unordered_map<std::string, VariableBase *> nameToVariable_;

        std::vector<std::string> stringBuffer_;
        std::vector<std::vector<uint8_t>> binaryBuffer_;

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
