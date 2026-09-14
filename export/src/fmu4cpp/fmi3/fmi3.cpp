
#include "fmi3Functions.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include "fmu4cpp/fmu_base.hpp"
#include "fmu4cpp/fmu_except.hpp"
#include "fmu4cpp/logger.hpp"
#include "fmu4cpp/status.hpp"

namespace {


    fmi3Status toFmi3StatusFromCommon(fmiStatus status) {
        switch (status) {
            case fmiOK:
                return fmi3OK;
            case fmiWarning:
                return fmi3Warning;
            case fmiDiscard:
                return fmi3Discard;
            case fmiError:
                return fmi3Error;
            case fmiFatal:
                return fmi3Fatal;
            default:
                return fmi3Error;// or another appropriate fallback
        }
    }

    class fmi3Logger : public fmu4cpp::logger {

    public:
        fmi3Logger(fmi3InstanceEnvironment env, fmi3LogMessageCallback logCallback, std::string instanceName)
            : logger(std::move(instanceName)), env_(env), logCallback_(logCallback) {}

        void debugLog(fmiStatus s, const std::string &message) override {
            if (logCallback_) {
                const std::string msg = instanceName_ + ": " + message;
                logCallback_(env_, toFmi3StatusFromCommon(s), nullptr, msg.c_str());
            }
        }

    private:
        fmi3InstanceEnvironment env_;
        fmi3LogMessageCallback logCallback_;
    };

    // A struct that holds all the data for one model instance.
    struct Fmi3Component {

        enum class State {
            Instantiated = 1 << 0,
            InitializationMode = 1 << 1,
            StepMode = 1 << 2,
            EventMode = 1 << 3,
            Terminated = 1 << 4,
            Invalid = 1 << 5
        };

        Fmi3Component(std::unique_ptr<fmu4cpp::fmu_base> slave, std::unique_ptr<fmi3Logger> logger)
            : state(State::Instantiated),
              slave(std::move(slave)),
              logger(std::move(logger)) {}

        bool isStateAllowed(int allowedMask) const {
            return (static_cast<int>(state) & allowedMask) != 0;
        }

        State state;
        std::unique_ptr<fmu4cpp::fmu_base> slave;
        std::unique_ptr<fmi3Logger> logger;
        bool eventModeUsed{false};
        bool earlyReturnAllowed{false};
        bool discreteStatesNeedUpdate{false};
    };

    struct Fmi3FMUStateWrapper {
        void *slave_state{nullptr};
        Fmi3Component::State component_state{Fmi3Component::State::StepMode};
        bool discreteStatesNeedUpdate{false};
        bool eventModeUsed{false};
    };

    constexpr int StatesCanGet = static_cast<int>(Fmi3Component::State::Instantiated) |
                                 static_cast<int>(Fmi3Component::State::InitializationMode) |
                                 static_cast<int>(Fmi3Component::State::StepMode) |
                                 static_cast<int>(Fmi3Component::State::EventMode) |
                                 static_cast<int>(Fmi3Component::State::Terminated);

    constexpr int StatesCanSet = static_cast<int>(Fmi3Component::State::Instantiated) |
                                 static_cast<int>(Fmi3Component::State::InitializationMode) |
                                 static_cast<int>(Fmi3Component::State::StepMode) |
                                 static_cast<int>(Fmi3Component::State::EventMode);

    bool isClockActiveForVariable(const fmu4cpp::fmu_base &slave, const fmu4cpp::VariableBase &v, Fmi3Component::State state) {
        const auto &clocks = v.clocks();
        if (clocks.empty()) {
            return true;
        }
        if (state == Fmi3Component::State::InitializationMode || state == Fmi3Component::State::Instantiated) {
            return true;
        }
        if (state != Fmi3Component::State::EventMode) {
            return false;
        }
        for (auto clockVr: clocks) {
            auto *clockVar = slave.get_variable(clockVr);
            if (clockVar && clockVar->type() == fmu4cpp::data_type::CLOCK) {
                auto *cv = dynamic_cast<const fmu4cpp::ClockVariable *>(clockVar);
                if (cv && cv->get()) {
                    return true;
                }
            }
        }
        return false;
    }

}// namespace

extern "C" {

const char *fmi3GetVersion(void) {
    return "3.0";
}

FMI3_Export void write_description(const char *location) {
    const auto instance = fmu4cpp::createInstance({});
    const auto xml = instance->make_description();
    std::ofstream of(location);
    of << xml;
    of.close();
}

fmi3Instance fmi3InstantiateModelExchange(fmi3String instanceName,
                                          fmi3String instantiationToken,
                                          fmi3String resourcePath,
                                          fmi3Boolean visible,
                                          fmi3Boolean loggingOn,
                                          fmi3InstanceEnvironment instanceEnvironment,
                                          fmi3LogMessageCallback logMessage) {

    fmi3Logger l(instanceEnvironment, logMessage, instanceName);
    l.log(fmiFatal, "[fmu4cpp] Unsupported mode: Model Exchange");
    return nullptr;
}

fmi3Instance fmi3InstantiateScheduledExecution(
        fmi3String instanceName,
        fmi3String instantiationToken,
        fmi3String resourcePath,
        fmi3Boolean visible,
        fmi3Boolean loggingOn,
        fmi3InstanceEnvironment instanceEnvironment,
        fmi3LogMessageCallback logMessage,
        fmi3ClockUpdateCallback clockUpdate,
        fmi3LockPreemptionCallback lockPreemption,
        fmi3UnlockPreemptionCallback unlockPreemption) {

    fmi3Logger l(instanceEnvironment, logMessage, instanceName);
    l.log(fmiFatal, "[fmu4cpp] Unsupported mode: Scheduled Execution");
    return nullptr;
}

fmi3Instance fmi3InstantiateCoSimulation(
        fmi3String instanceName,
        fmi3String instantiationToken,
        fmi3String resourcePath,
        fmi3Boolean visible,
        fmi3Boolean loggingOn,
        fmi3Boolean eventModeUsed,
        fmi3Boolean earlyReturnAllowed,
        const fmi3ValueReference requiredIntermediateVariables[],
        size_t nRequiredIntermediateVariables,
        fmi3InstanceEnvironment instanceEnvironment,
        fmi3LogMessageCallback logMessage,
        fmi3IntermediateUpdateCallback intermediateUpdate) {

    if (!instanceName || std::strlen(instanceName) == 0) {
        return nullptr;
    }
    if (!instantiationToken || std::strlen(instantiationToken) == 0) {
        return nullptr;
    }

    int magic = 1;
#ifdef _MSC_VER
    magic = 0;
#endif

    std::string resources(resourcePath ? resourcePath : "");

    if (resources.find("file:////") != std::string::npos) {
        resources.replace(0, 9 - magic, "");
    } else if (resources.find("file:///") != std::string::npos) {
        resources.replace(0, 8 - magic, "");
    } else if (resources.find("file://") != std::string::npos) {
        resources.replace(0, 7 - magic, "");
    } else if (resources.find("file:/") != std::string::npos) {
        resources.replace(0, 6 - magic, "");
    }

    auto logger = std::make_unique<fmi3Logger>(instanceEnvironment, logMessage, instanceName);
    logger->setDebugLogging(loggingOn);

    try {
        auto slave = fmu4cpp::createInstance(
                {
                        logger.get(),
                        instanceName,
                        resources,
                        visible,
                });
        const auto guid = slave->guid();
        if (guid != instantiationToken) {
            logger->log(fmiFatal, "[fmu4cpp] Error. Wrong guid!");
            return nullptr;
        }

        if (slave->has_clocks() && !eventModeUsed) {
            logger->log(fmiFatal, "[fmu4cpp] Model declares Clocks, but eventModeUsed is fmi3False.");
            return nullptr;
        }
        if (eventModeUsed && !slave->has_event_mode()) {
            logger->log(fmiFatal, "[fmu4cpp] eventModeUsed is fmi3True, but model does not support Event Mode.");
            return nullptr;
        }

        auto c = std::make_unique<Fmi3Component>(std::move(slave), std::move(logger));
        c->eventModeUsed = (eventModeUsed != fmi3False);
        c->earlyReturnAllowed = (earlyReturnAllowed != fmi3False);

        return c.release();
    } catch (const std::exception &e) {

        logger->log(fmiFatal, "[fmu4cpp] Unable to instantiate model! " + std::string(e.what()));

        return nullptr;
    }
}

fmi3Status fmi3EnterEventMode(fmi3Instance c) {
    if (!c) {
        return fmi3Error;
    }
    const auto component = static_cast<Fmi3Component *>(c);
    if (component->state == Fmi3Component::State::Invalid) {
        return fmi3Fatal;
    }
    if (!component->eventModeUsed) {
        component->logger->log(fmiError, "fmi3EnterEventMode: Event Mode was not enabled during instantiation.");
        return fmi3Error;
    }
    if (component->state != Fmi3Component::State::StepMode) {
        component->logger->log(fmiError, "fmi3EnterEventMode: Invalid state. Expected StepMode.");
        return fmi3Error;
    }
    try {
        component->slave->enter_event_mode();
        component->state = Fmi3Component::State::EventMode;
        component->discreteStatesNeedUpdate = true;
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        component->logger->log(fmiFatal, ex.what());
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        component->logger->log(fmiError, ex.what());
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3EnterInitializationMode(fmi3Instance c,
                                       fmi3Boolean toleranceDefined,
                                       fmi3Float64 tolerance,
                                       fmi3Float64 startTime,
                                       fmi3Boolean stopTimeDefined,
                                       fmi3Float64 stopTime) {
    if (!c) {
        return fmi3Error;
    }
    const auto component = static_cast<Fmi3Component *>(c);
    if (component->state == Fmi3Component::State::Invalid) {
        return fmi3Fatal;
    }
    if (component->state != Fmi3Component::State::Instantiated) {
        component->logger->log(fmiError, "fmi3EnterInitializationMode: Invalid state. Expected Instantiated.");
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
    if (toleranceDefined && tolerance < 0.0) {
        component->logger->log(fmiError, "fmi3EnterInitializationMode: tolerance must be non-negative.");
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
    if (stopTimeDefined && stopTime < startTime) {
        component->logger->log(fmiError, "fmi3EnterInitializationMode: stopTime must be >= startTime.");
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }

    std::optional<double> stop;
    std::optional<double> tol;

    if (stopTimeDefined) stop = stopTime;
    if (toleranceDefined) tol = tolerance;

    try {
        component->slave->enter_initialisation_mode(startTime, stop, tol);
        component->state = Fmi3Component::State::InitializationMode;
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        component->logger->log(fmiFatal, ex.what());
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        component->logger->log(fmiError, ex.what());
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3ExitInitializationMode(fmi3Instance c) {
    if (!c) {
        return fmi3Error;
    }
    const auto component = static_cast<Fmi3Component *>(c);
    if (component->state == Fmi3Component::State::Invalid) {
        return fmi3Fatal;
    }
    if (component->state != Fmi3Component::State::InitializationMode) {
        component->logger->log(fmiError, "fmi3ExitInitializationMode: Invalid state. Expected InitializationMode.");
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
    try {
        component->slave->exit_initialisation_mode();
        component->state = Fmi3Component::State::StepMode;
        if (component->eventModeUsed) {
            component->state = Fmi3Component::State::EventMode;
            component->discreteStatesNeedUpdate = true;
            component->slave->enter_event_mode();
        } else {
            component->state = Fmi3Component::State::StepMode;
        }
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        component->logger->log(fmiFatal, ex.what());
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        component->logger->log(fmiError, ex.what());
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3Terminate(fmi3Instance c) {
    if (!c) {
        return fmi3Error;
    }
    const auto component = static_cast<Fmi3Component *>(c);
    if (component->state == Fmi3Component::State::Invalid) {
        return fmi3Fatal;
    }
    if (component->state == Fmi3Component::State::Terminated) {
        return fmi3OK;
    }
    if (component->state != Fmi3Component::State::StepMode &&
        component->state != Fmi3Component::State::InitializationMode &&
        component->state != Fmi3Component::State::EventMode) {
        component->logger->log(fmiError, "fmi3Terminate called in illegal state.");
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
    try {
        component->slave->terminate();
        component->state = Fmi3Component::State::Terminated;
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        component->logger->log(fmiFatal, ex.what());
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        component->logger->log(fmiError, ex.what());
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3DoStep(fmi3Instance c,
                      fmi3Float64 currentCommunicationPoint,
                      fmi3Float64 communicationStepSize,
                      fmi3Boolean /*noSetFMUStatePriorToCurrentPoint*/,
                      fmi3Boolean *eventHandlingNeeded,
                      fmi3Boolean *terminateSimulation,
                      fmi3Boolean *earlyReturn,
                      fmi3Float64 *lastSuccessfulTime) {
    if (!c) {
        return fmi3Error;
    }
    const auto component = static_cast<Fmi3Component *>(c);
    if (component->state == Fmi3Component::State::Invalid) {
        return fmi3Fatal;
    }
    if (component->state != Fmi3Component::State::StepMode) {
        component->logger->log(fmiError, "fmi3DoStep: Invalid state. Expected StepMode.");
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
    if (!eventHandlingNeeded || !terminateSimulation || !earlyReturn || !lastSuccessfulTime) {
        component->logger->log(fmiError, "fmi3DoStep: Null pointer passed for output arguments.");
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
    if (communicationStepSize <= 0.0) {
        component->logger->log(fmiError, "fmi3DoStep: communicationStepSize must be positive.");
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
    if (currentCommunicationPoint < 0.0) {
        component->logger->log(fmiError, "fmi3DoStep: currentCommunicationPoint must be non-negative.");
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
    try {
        if (component->slave->step(currentCommunicationPoint, communicationStepSize)) {
            *earlyReturn = false;
            *terminateSimulation = false;
            *eventHandlingNeeded = false;
            *eventHandlingNeeded = component->eventModeUsed && component->slave->has_pending_events();
            *lastSuccessfulTime = currentCommunicationPoint + communicationStepSize;
            return fmi3OK;
        }

        component->logger->log(fmiWarning, "Step returned false!");
        return fmi3Discard;
    } catch (const fmu4cpp::fatal_error &ex) {
        component->logger->log(fmiFatal, ex.what());
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        component->logger->log(fmiError, ex.what());
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3CancelStep(fmi3Instance instance) {
    if (!instance) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(instance);
    component->logger->log(fmiError, "fmi3CancelStep is only valid during asynchronous step computation");
    return fmi3Error;
}

fmi3Status fmi3Reset(fmi3Instance c) {
    if (!c) {
        return fmi3Error;
    }
    const auto component = static_cast<Fmi3Component *>(c);
    if (component->state == Fmi3Component::State::Invalid) {
        return fmi3Fatal;
    }
    try {
        component->slave->reset();
        component->state = Fmi3Component::State::Instantiated;
        component->discreteStatesNeedUpdate = false;
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        component->logger->log(fmiFatal, ex.what());
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        component->logger->log(fmiError, ex.what());
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

#define IMPLEMENT_FMI3_TYPED_GETTER(Type, CppType)                                                       \
    fmi3Status fmi3Get##Type(                                                                            \
            fmi3Instance c,                                                                              \
            const fmi3ValueReference vr[],                                                               \
            size_t nvr,                                                                                  \
            fmi3##Type value[],                                                                          \
            size_t nValues) {                                                                            \
        if (!c) {                                                                                        \
            return fmi3Error;                                                                            \
        }                                                                                                \
        const auto component = static_cast<Fmi3Component *>(c);                                          \
        if (component->state == Fmi3Component::State::Invalid) {                                         \
            return fmi3Fatal;                                                                            \
        }                                                                                                \
        if (!component->isStateAllowed(StatesCanGet)) {                                                  \
            component->logger->log(fmiError, "fmi3Get" #Type " called in illegal state");                \
            component->state = Fmi3Component::State::Terminated;                                         \
            return fmi3Error;                                                                            \
        }                                                                                                \
        if (nvr > 0 && !vr) {                                                                            \
            component->logger->log(fmiError, "fmi3Get" #Type ": Null pointer passed for vr");            \
            component->state = Fmi3Component::State::Terminated;                                         \
            return fmi3Error;                                                                            \
        }                                                                                                \
        if (nValues > 0 && !value) {                                                                     \
            component->logger->log(fmiError, "fmi3Get" #Type ": Null pointer passed for values");        \
            component->state = Fmi3Component::State::Terminated;                                         \
            return fmi3Error;                                                                            \
        }                                                                                                \
        for (size_t i = 0; i < nvr; ++i) {                                                               \
            auto *var = component->slave->get_variable(vr[i]);                                           \
            if (var && !isClockActiveForVariable(*component->slave, *var, component->state)) {           \
                component->logger->log(fmiError, "Clocked variable with vr " + std::to_string(vr[i]) +   \
                                                         " accessed when its Clock is not active.");     \
                return fmi3Error;                                                                        \
            }                                                                                            \
        }                                                                                                \
        try {                                                                                            \
            component->slave->get_values<CppType>(vr, nvr, reinterpret_cast<CppType *>(value), nValues); \
            return fmi3OK;                                                                               \
        } catch (const fmu4cpp::fatal_error &ex) {                                                       \
            component->logger->log(fmiFatal, ex.what());                                                 \
            component->state = Fmi3Component::State::Invalid;                                            \
            return fmi3Fatal;                                                                            \
        } catch (const std::exception &ex) {                                                             \
            component->logger->log(fmiError, ex.what());                                                 \
            component->state = Fmi3Component::State::Terminated;                                         \
            return fmi3Error;                                                                            \
        }                                                                                                \
    }

#define IMPLEMENT_FMI3_TYPED_SETTER(Type, CppType)                                                             \
    fmi3Status fmi3Set##Type(                                                                                  \
            fmi3Instance c,                                                                                    \
            const fmi3ValueReference vr[],                                                                     \
            size_t nvr,                                                                                        \
            const fmi3##Type value[],                                                                          \
            size_t nValues) {                                                                                  \
        if (!c) {                                                                                              \
            return fmi3Error;                                                                                  \
        }                                                                                                      \
        const auto component = static_cast<Fmi3Component *>(c);                                                \
        if (component->state == Fmi3Component::State::Invalid) {                                               \
            return fmi3Fatal;                                                                                  \
        }                                                                                                      \
        if (!component->isStateAllowed(StatesCanSet)) {                                                        \
            component->logger->log(fmiError, "fmi3Set" #Type " called in illegal state");                      \
            component->state = Fmi3Component::State::Terminated;                                               \
            return fmi3Error;                                                                                  \
        }                                                                                                      \
        if (nvr > 0 && !vr) {                                                                                  \
            component->logger->log(fmiError, "fmi3Set" #Type ": Null pointer passed for vr");                  \
            component->state = Fmi3Component::State::Terminated;                                               \
            return fmi3Error;                                                                                  \
        }                                                                                                      \
        if (nValues > 0 && !value) {                                                                           \
            component->logger->log(fmiError, "fmi3Set" #Type ": Null pointer passed for values");              \
            component->state = Fmi3Component::State::Terminated;                                               \
            return fmi3Error;                                                                                  \
        }                                                                                                      \
        for (size_t i = 0; i < nvr; ++i) {                                                                     \
            auto *var = component->slave->get_variable(vr[i]);                                                 \
            if (var && !isClockActiveForVariable(*component->slave, *var, component->state)) {                 \
                component->logger->log(fmiError, "Clocked variable with vr " + std::to_string(vr[i]) +         \
                                                         " accessed when its Clock is not active.");           \
                return fmi3Error;                                                                              \
            }                                                                                                  \
        }                                                                                                      \
        try {                                                                                                  \
            component->slave->set_values<CppType>(vr, nvr, reinterpret_cast<const CppType *>(value), nValues); \
            return fmi3OK;                                                                                     \
        } catch (const fmu4cpp::fatal_error &ex) {                                                             \
            component->logger->log(fmiFatal, ex.what());                                                       \
            component->state = Fmi3Component::State::Invalid;                                                  \
            return fmi3Fatal;                                                                                  \
        } catch (const std::exception &ex) {                                                                   \
            component->logger->log(fmiError, ex.what());                                                       \
            component->state = Fmi3Component::State::Terminated;                                               \
            return fmi3Error;                                                                                  \
        }                                                                                                      \
    }

IMPLEMENT_FMI3_TYPED_GETTER(Int8, int8_t)
IMPLEMENT_FMI3_TYPED_SETTER(Int8, int8_t)

IMPLEMENT_FMI3_TYPED_GETTER(UInt8, uint8_t)
IMPLEMENT_FMI3_TYPED_SETTER(UInt8, uint8_t)

IMPLEMENT_FMI3_TYPED_GETTER(Int16, int16_t)
IMPLEMENT_FMI3_TYPED_SETTER(Int16, int16_t)

IMPLEMENT_FMI3_TYPED_GETTER(UInt16, uint16_t)
IMPLEMENT_FMI3_TYPED_SETTER(UInt16, uint16_t)

IMPLEMENT_FMI3_TYPED_GETTER(Int32, int32_t)
IMPLEMENT_FMI3_TYPED_SETTER(Int32, int32_t)

IMPLEMENT_FMI3_TYPED_GETTER(UInt32, uint32_t)
IMPLEMENT_FMI3_TYPED_SETTER(UInt32, uint32_t)

IMPLEMENT_FMI3_TYPED_GETTER(Int64, int64_t)
IMPLEMENT_FMI3_TYPED_SETTER(Int64, int64_t)

IMPLEMENT_FMI3_TYPED_GETTER(UInt64, uint64_t)
IMPLEMENT_FMI3_TYPED_SETTER(UInt64, uint64_t)

IMPLEMENT_FMI3_TYPED_GETTER(Float32, float)
IMPLEMENT_FMI3_TYPED_SETTER(Float32, float)

IMPLEMENT_FMI3_TYPED_GETTER(Float64, double)
IMPLEMENT_FMI3_TYPED_SETTER(Float64, double)

IMPLEMENT_FMI3_TYPED_GETTER(Boolean, bool)
IMPLEMENT_FMI3_TYPED_SETTER(Boolean, bool)

fmi3Status fmi3GetString(fmi3Instance c,
                         const fmi3ValueReference vr[],
                         size_t nvr,
                         fmi3String value[],
                         size_t nValues) {
    if (!c) {
        return fmi3Error;
    }
    const auto component = static_cast<Fmi3Component *>(c);
    if (component->state == Fmi3Component::State::Invalid) {
        return fmi3Fatal;
    }
    if (!component->isStateAllowed(StatesCanGet)) {
        component->logger->log(fmiError, "fmi3GetString called in illegal state");
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
    if (nvr > 0 && !vr) {
        component->logger->log(fmiError, "fmi3GetString: Null pointer passed for vr");
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
    if (nValues > 0 && !value) {
        component->logger->log(fmiError, "fmi3GetString: Null pointer passed for values");
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
    if (nValues != nvr) {
        component->logger->log(fmiError, "fmi3GetString: nValues must equal nvr");
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
    for (size_t i = 0; i < nvr; ++i) {
        auto *var = component->slave->get_variable(vr[i]);
        if (var && !isClockActiveForVariable(*component->slave, *var, component->state)) {
            component->logger->log(fmiError, "Clocked variable with vr " + std::to_string(vr[i]) +
                                                     " accessed when its Clock is not active.");
            return fmi3Error;
        }
    }
    try {
        component->slave->get_string(vr, nvr, value);
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        component->logger->log(fmiFatal, ex.what());
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        component->logger->log(fmiError, ex.what());
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3SetString(fmi3Instance c,
                         const fmi3ValueReference vr[],
                         size_t nvr,
                         const fmi3String value[],
                         size_t nValues) {
    if (!c) {
        return fmi3Error;
    }
    const auto component = static_cast<Fmi3Component *>(c);
    if (component->state == Fmi3Component::State::Invalid) {
        return fmi3Fatal;
    }
    if (!component->isStateAllowed(StatesCanSet)) {
        component->logger->log(fmiError, "fmi3SetString called in illegal state");
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
    if (nvr > 0 && !vr) {
        component->logger->log(fmiError, "fmi3SetString: Null pointer passed for vr");
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
    if (nValues > 0 && !value) {
        component->logger->log(fmiError, "fmi3SetString: Null pointer passed for values");
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
    if (nValues != nvr) {
        component->logger->log(fmiError, "fmi3SetString: nValues must equal nvr");
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
    for (size_t i = 0; i < nvr; ++i) {
        auto *var = component->slave->get_variable(vr[i]);
        if (var && !isClockActiveForVariable(*component->slave, *var, component->state)) {
            component->logger->log(fmiError, "Clocked variable with vr " + std::to_string(vr[i]) +
                                                     " accessed when its Clock is not active.");
            return fmi3Error;
        }
    }
    try {
        component->slave->set_string(vr, nvr, value);
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        component->logger->log(fmiFatal, ex.what());
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        component->logger->log(fmiError, ex.what());
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3GetBinary(fmi3Instance c,
                         const fmi3ValueReference vr[],
                         size_t nvr,
                         size_t valueSizes[],
                         fmi3Binary values[],
                         size_t nValues) {
    if (!c) {
        return fmi3Error;
    }
    const auto component = static_cast<Fmi3Component *>(c);
    if (component->state == Fmi3Component::State::Invalid) {
        return fmi3Fatal;
    }
    if (!component->isStateAllowed(StatesCanGet)) {
        component->logger->log(fmiError, "fmi3GetBinary called in illegal state");
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
    if (nvr > 0 && (!vr || !valueSizes || !values)) {
        component->logger->log(fmiError, "fmi3GetBinary: Null pointer passed for vr, valueSizes, or values");
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
    if (nValues != nvr) {
        component->logger->log(fmiError, "fmi3GetBinary: nValues must equal nvr");
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
    for (size_t i = 0; i < nvr; ++i) {
        auto *var = component->slave->get_variable(vr[i]);
        if (var && !isClockActiveForVariable(*component->slave, *var, component->state)) {
            component->logger->log(fmiError, "Clocked variable with vr " + std::to_string(vr[i]) +
                                                     " accessed when its Clock is not active.");
            return fmi3Error;
        }
    }
    try {
        component->slave->get_binary(vr, nvr, valueSizes, values);
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        component->logger->log(fmiFatal, ex.what());
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        component->logger->log(fmiError, ex.what());
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3SetBinary(fmi3Instance c,
                         const fmi3ValueReference vr[],
                         size_t nvr,
                         const size_t valueSizes[],
                         const fmi3Binary values[],
                         size_t nValues) {
    if (!c) {
        return fmi3Error;
    }
    const auto component = static_cast<Fmi3Component *>(c);
    if (component->state == Fmi3Component::State::Invalid) {
        return fmi3Fatal;
    }
    if (!component->isStateAllowed(StatesCanSet)) {
        component->logger->log(fmiError, "fmi3SetBinary called in illegal state");
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
    if (nvr > 0 && (!vr || !valueSizes || !values)) {
        component->logger->log(fmiError, "fmi3SetBinary: Null pointer passed for vr, valueSizes, or values");
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
    if (nValues != nvr) {
        component->logger->log(fmiError, "fmi3SetBinary: nValues must equal nvr");
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
    for (size_t i = 0; i < nvr; ++i) {
        auto *var = component->slave->get_variable(vr[i]);
        if (var && !isClockActiveForVariable(*component->slave, *var, component->state)) {
            component->logger->log(fmiError, "Clocked variable with vr " + std::to_string(vr[i]) +
                                                     " accessed when its Clock is not active.");
            return fmi3Error;
        }
    }
    try {
        component->slave->set_binary(vr, nvr, valueSizes, values);
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        component->logger->log(fmiFatal, ex.what());
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        component->logger->log(fmiError, ex.what());
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3SetDebugLogging(fmi3Instance c,
                               fmi3Boolean loggingOn,
                               size_t /*nCategories*/,
                               const fmi3String /*categories*/[]) {
    if (!c) {
        return fmi3Error;
    }
    const auto component = static_cast<Fmi3Component *>(c);
    if (component->state == Fmi3Component::State::Invalid) {
        return fmi3Fatal;
    }
    component->logger->setDebugLogging(loggingOn);
    return fmi3OK;
}

fmi3Status fmi3GetOutputDerivatives(fmi3Instance,
                                    const fmi3ValueReference[], size_t,
                                    const fmi3Int32[],
                                    fmi3Float64[],
                                    size_t) {
    return fmi3Error;
}


fmi3Status fmi3GetDirectionalDerivative(fmi3Instance instance,
                                        const fmi3ValueReference unknowns[],
                                        size_t nUnknowns,
                                        const fmi3ValueReference knowns[],
                                        size_t nKnowns,
                                        const fmi3Float64 seed[],
                                        size_t nSeed,
                                        fmi3Float64 sensitivity[],
                                        size_t nSensitivity) {

    return fmi3Error;
}


fmi3Status fmi3GetFMUState(fmi3Instance c, fmi3FMUState *state) {
    if (!c || !state) {
        return fmi3Error;
    }
    const auto component = static_cast<Fmi3Component *>(c);
    if (component->state == Fmi3Component::State::Invalid) {
        return fmi3Fatal;
    }

    try {

        const auto s = component->slave->getFMUState();
        auto *wrapper = new Fmi3FMUStateWrapper();
        wrapper->slave_state = s;
        wrapper->component_state = component->state;
        wrapper->discreteStatesNeedUpdate = component->discreteStatesNeedUpdate;
        wrapper->eventModeUsed = component->eventModeUsed;
        *state = wrapper;
        return fmi3OK;

    } catch (const fmu4cpp::fatal_error &ex) {
        component->logger->log(fmiFatal, ex.what());
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        component->logger->log(fmiError, ex.what());
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3SetFMUState(fmi3Instance c, fmi3FMUState state) {
    if (!c || !state) {
        return fmi3Error;
    }
    const auto component = static_cast<Fmi3Component *>(c);
    if (component->state == Fmi3Component::State::Invalid) {
        return fmi3Fatal;
    }

    try {

        auto *wrapper = static_cast<Fmi3FMUStateWrapper *>(state);
        component->slave->setFmuState(wrapper->slave_state);
        component->state = wrapper->component_state;
        component->discreteStatesNeedUpdate = wrapper->discreteStatesNeedUpdate;
        component->eventModeUsed = wrapper->eventModeUsed;
        return fmi3OK;

    } catch (const fmu4cpp::fatal_error &ex) {
        component->logger->log(fmiFatal, ex.what());
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        component->logger->log(fmiError, ex.what());
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}


fmi3Status fmi3FreeFMUState(fmi3Instance c, fmi3FMUState *state) {
    if (!c) {
        return fmi3Error;
    }
    if (state == nullptr || *state == nullptr) {
        return fmi3OK;
    }

    const auto component = static_cast<Fmi3Component *>(c);
    if (component->state == Fmi3Component::State::Invalid) {
        return fmi3Fatal;
    }

    try {

        auto *wrapper = static_cast<Fmi3FMUStateWrapper *>(*state);
        if (wrapper->slave_state) {
            component->slave->freeFmuState(&wrapper->slave_state);
        }
        delete wrapper;
        *state = nullptr;
        return fmi3OK;

    } catch (const fmu4cpp::fatal_error &ex) {
        component->logger->log(fmiFatal, ex.what());
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        component->logger->log(fmiError, ex.what());
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3SerializedFMUStateSize(fmi3Instance c, fmi3FMUState state, size_t *size) {
    if (!c || !state || !size) {
        return fmi3Error;
    }
    const auto component = static_cast<Fmi3Component *>(c);
    if (component->state == Fmi3Component::State::Invalid) {
        return fmi3Fatal;
    }

    try {

        auto *wrapper = static_cast<Fmi3FMUStateWrapper *>(state);
        component->slave->serializedFMUStateSize(wrapper->slave_state, *size);
        return fmi3OK;

    } catch (const fmu4cpp::fatal_error &ex) {
        component->logger->log(fmiFatal, ex.what());
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        component->logger->log(fmiError, ex.what());
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3SerializeFMUState(fmi3Instance c, fmi3FMUState state, fmi3Byte data[], size_t size) {
    if (!c || !state || !data || size == 0) {
        return fmi3Error;
    }
    const auto component = static_cast<Fmi3Component *>(c);
    if (component->state == Fmi3Component::State::Invalid) {
        return fmi3Fatal;
    }

    try {

        auto *wrapper = static_cast<Fmi3FMUStateWrapper *>(state);
        std::vector<uint8_t> serializedState(size);
        component->slave->serializeFMUState(wrapper->slave_state, serializedState);
        std::memcpy(data, serializedState.data(), size);
        return fmi3OK;

    } catch (const fmu4cpp::fatal_error &ex) {
        component->logger->log(fmiFatal, ex.what());
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        component->logger->log(fmiError, ex.what());
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3DeserializeFMUState(fmi3Instance c, const fmi3Byte data[], size_t size, fmi3FMUState *state) {
    if (!c || !data || size == 0 || !state) {
        return fmi3Error;
    }
    const auto component = static_cast<Fmi3Component *>(c);
    if (component->state == Fmi3Component::State::Invalid) {
        return fmi3Fatal;
    }

    try {

        const std::vector<uint8_t> serializedState(data, data + size);
        void *slave_state = nullptr;
        component->slave->deserializeFMUState(serializedState, &slave_state);
        auto *wrapper = new Fmi3FMUStateWrapper();
        wrapper->slave_state = slave_state;
        wrapper->component_state = component->state;
        wrapper->discreteStatesNeedUpdate = component->discreteStatesNeedUpdate;
        wrapper->eventModeUsed = component->eventModeUsed;
        *state = wrapper;
        return fmi3OK;

    } catch (const fmu4cpp::fatal_error &ex) {
        component->logger->log(fmiFatal, ex.what());
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        component->logger->log(fmiError, ex.what());
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3GetClock(fmi3Instance instance,
                        const fmi3ValueReference valueReferences[],
                        size_t nValueReferences,
                        fmi3Clock values[]) {
    if (!instance) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(instance);
    if (component->state == Fmi3Component::State::Invalid) return fmi3Fatal;
    if (component->state != Fmi3Component::State::EventMode) {
        component->logger->log(fmiError, "fmi3GetClock: Invalid state. Expected EventMode.");
        return fmi3Error;
    }
    if (nValueReferences > 0 && (!valueReferences || !values)) {
        component->logger->log(fmiError, "fmi3GetClock: Null pointer passed.");
        return fmi3Error;
    }
    try {
        component->slave->get_clock(valueReferences, nValueReferences, values);
        // Reset output clocks to inactive after query (one-shot)
        for (size_t i = 0; i < nValueReferences; ++i) {
            auto *var = component->slave->get_variable(valueReferences[i]);
            if (var && var->causality() == fmu4cpp::causality_t::OUTPUT) {
                auto *cv = const_cast<fmu4cpp::ClockVariable *>(dynamic_cast<const fmu4cpp::ClockVariable *>(var));
                if (cv && values[i]) {
                    cv->force_set(false);
                }
            }
        }
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        component->logger->log(fmiFatal, ex.what());
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        component->logger->log(fmiError, ex.what());
        return fmi3Error;
    }
}

fmi3Status fmi3SetClock(fmi3Instance instance,
                        const fmi3ValueReference valueReferences[],
                        size_t nValueReferences,
                        const fmi3Clock values[]) {
    if (!instance) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(instance);
    if (component->state == Fmi3Component::State::Invalid) return fmi3Fatal;
    if (component->state != Fmi3Component::State::EventMode) {
        component->logger->log(fmiError, "fmi3SetClock: Invalid state. Expected EventMode.");
        return fmi3Error;
    }
    if (nValueReferences > 0 && (!valueReferences || !values)) {
        component->logger->log(fmiError, "fmi3SetClock: Null pointer passed.");
        return fmi3Error;
    }
    try {
        component->slave->set_clock(valueReferences, nValueReferences, values);
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        component->logger->log(fmiFatal, ex.what());
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        component->logger->log(fmiError, ex.what());
        return fmi3Error;
    }
}

fmi3Status fmi3GetNumberOfVariableDependencies(fmi3Instance instance,
                                               fmi3ValueReference valueReference,
                                               size_t *nDependencies) {
    return fmi3Error;
}

fmi3Status fmi3GetVariableDependencies(fmi3Instance instance,
                                       fmi3ValueReference dependent,
                                       size_t elementIndicesOfDependent[],
                                       fmi3ValueReference independents[],
                                       size_t elementIndicesOfIndependents[],
                                       fmi3DependencyKind dependencyKinds[],
                                       size_t nDependencies) {
    return fmi3Error;
}

fmi3Status fmi3GetAdjointDerivative(fmi3Instance instance,
                                    const fmi3ValueReference unknowns[],
                                    size_t nUnknowns,
                                    const fmi3ValueReference knowns[],
                                    size_t nKnowns,
                                    const fmi3Float64 seed[],
                                    size_t nSeed,
                                    fmi3Float64 sensitivity[],
                                    size_t nSensitivity) {

    return fmi3Error;
}

fmi3Status fmi3EnterConfigurationMode(fmi3Instance instance) {
    if (!instance) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(instance);
    component->logger->log(fmiError, "fmi3EnterConfigurationMode is not supported");
    return fmi3Error;
}

fmi3Status fmi3ExitConfigurationMode(fmi3Instance instance) {
    return fmi3Error;
}

fmi3Status fmi3GetIntervalDecimal(fmi3Instance instance,
                                  const fmi3ValueReference valueReferences[],
                                  size_t nValueReferences,
                                  fmi3Float64 intervals[],
                                  fmi3IntervalQualifier qualifiers[]) {
    if (!instance) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(instance);
    if (component->state == Fmi3Component::State::Invalid) return fmi3Fatal;
    if (component->state != Fmi3Component::State::EventMode &&
        component->state != Fmi3Component::State::StepMode &&
        component->state != Fmi3Component::State::InitializationMode) {
        component->logger->log(fmiError, "fmi3GetIntervalDecimal: Invalid state.");
        return fmi3Error;
    }
    if (nValueReferences > 0 && (!valueReferences || !intervals || !qualifiers)) {
        component->logger->log(fmiError, "fmi3GetIntervalDecimal: Null pointer passed.");
        return fmi3Error;
    }
    try {
        std::vector<fmu4cpp::interval_qualifier_t> internalQualifiers(nValueReferences);
        component->slave->get_interval_decimal(valueReferences, nValueReferences, intervals, internalQualifiers.data());
        for (size_t i = 0; i < nValueReferences; ++i) {
            switch (internalQualifiers[i]) {
                case fmu4cpp::interval_qualifier_t::INTERVAL_NOT_YET_KNOWN:
                    qualifiers[i] = fmi3IntervalNotYetKnown;
                    break;
                case fmu4cpp::interval_qualifier_t::INTERVAL_UNCHANGED:
                    qualifiers[i] = fmi3IntervalUnchanged;
                    break;
                case fmu4cpp::interval_qualifier_t::INTERVAL_CHANGED:
                    qualifiers[i] = fmi3IntervalChanged;
                    break;
            }
        }
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        component->logger->log(fmiFatal, ex.what());
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        component->logger->log(fmiError, ex.what());
        return fmi3Error;
    }
}

fmi3Status fmi3GetIntervalFraction(fmi3Instance instance,
                                   const fmi3ValueReference valueReferences[],
                                   size_t nValueReferences,
                                   fmi3UInt64 counters[],
                                   fmi3UInt64 resolutions[],
                                   fmi3IntervalQualifier qualifiers[]) {

    return fmi3Error;
}

fmi3Status fmi3GetShiftDecimal(fmi3Instance instance,
                               const fmi3ValueReference valueReferences[],
                               size_t nValueReferences,
                               fmi3Float64 shifts[]) {
    if (!instance) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(instance);
    if (component->state == Fmi3Component::State::Invalid) return fmi3Fatal;
    if (nValueReferences > 0 && (!valueReferences || !shifts)) {
        component->logger->log(fmiError, "fmi3GetShiftDecimal: Null pointer passed.");
        return fmi3Error;
    }
    try {
        for (size_t i = 0; i < nValueReferences; ++i) {
            auto *v = component->slave->get_variable(valueReferences[i]);
            if (!v || v->type() != fmu4cpp::data_type::CLOCK) {
                component->logger->log(fmiError, "fmi3GetShiftDecimal: ValueReference is not a Clock.");
                return fmi3Error;
            }
            auto *cv = dynamic_cast<const fmu4cpp::ClockVariable *>(v);
            shifts[i] = cv->getShiftDecimal().value_or(0.0);
        }
        return fmi3OK;
    } catch (const std::exception &ex) {
        component->logger->log(fmiError, ex.what());
        return fmi3Error;
    }
}

fmi3Status fmi3GetShiftFraction(fmi3Instance instance,
                                const fmi3ValueReference valueReferences[],
                                size_t nValueReferences,
                                fmi3UInt64 counters[],
                                fmi3UInt64 resolutions[]) {

    return fmi3Error;
}

fmi3Status fmi3SetIntervalDecimal(fmi3Instance instance,
                                  const fmi3ValueReference valueReferences[],
                                  size_t nValueReferences,
                                  const fmi3Float64 intervals[]) {
    if (!instance) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(instance);
    if (component->state == Fmi3Component::State::Invalid) return fmi3Fatal;
    if (component->state != Fmi3Component::State::EventMode &&
        component->state != Fmi3Component::State::InitializationMode) {
        component->logger->log(fmiError, "fmi3SetIntervalDecimal: Invalid state.");
        return fmi3Error;
    }
    if (nValueReferences > 0 && (!valueReferences || !intervals)) {
        component->logger->log(fmiError, "fmi3SetIntervalDecimal: Null pointer passed.");
        return fmi3Error;
    }
    try {
        component->slave->set_interval_decimal(valueReferences, nValueReferences, intervals);
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        component->logger->log(fmiFatal, ex.what());
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        component->logger->log(fmiError, ex.what());
        return fmi3Error;
    }
}

fmi3Status fmi3SetIntervalFraction(fmi3Instance instance,
                                   const fmi3ValueReference valueReferences[],
                                   size_t nValueReferences,
                                   const fmi3UInt64 counters[],
                                   const fmi3UInt64 resolutions[]) {

    return fmi3Error;
}

fmi3Status fmi3SetShiftDecimal(fmi3Instance instance,
                               const fmi3ValueReference valueReferences[],
                               size_t nValueReferences,
                               const fmi3Float64 shifts[]) {

    return fmi3Error;
}

fmi3Status fmi3SetShiftFraction(fmi3Instance instance,
                                const fmi3ValueReference valueReferences[],
                                size_t nValueReferences,
                                const fmi3UInt64 counters[],
                                const fmi3UInt64 resolutions[]) {

    return fmi3Error;
}

fmi3Status fmi3EvaluateDiscreteStates(fmi3Instance instance) {

    return fmi3Error;
}

fmi3Status fmi3UpdateDiscreteStates(fmi3Instance instance,
                                    fmi3Boolean *discreteStatesNeedUpdate,
                                    fmi3Boolean *terminateSimulation,
                                    fmi3Boolean *nominalsOfContinuousStatesChanged,
                                    fmi3Boolean *valuesOfContinuousStatesChanged,
                                    fmi3Boolean *nextEventTimeDefined,
                                    fmi3Float64 *nextEventTime) {
    if (!instance) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(instance);
    if (component->state == Fmi3Component::State::Invalid) return fmi3Fatal;
    if (component->state != Fmi3Component::State::EventMode) {
        component->logger->log(fmiError, "fmi3UpdateDiscreteStates: Invalid state. Expected EventMode.");
        return fmi3Error;
    }
    if (!discreteStatesNeedUpdate || !terminateSimulation ||
        !nominalsOfContinuousStatesChanged || !valuesOfContinuousStatesChanged ||
        !nextEventTimeDefined || !nextEventTime) {
        component->logger->log(fmiError, "fmi3UpdateDiscreteStates: Null pointer passed for output arguments.");
        return fmi3Error;
    }
    try {
        fmu4cpp::discrete_states_info info{};
        component->slave->update_discrete_states(info);
        component->slave->set_discrete_states_info(info);
        if (!info.discreteStatesNeedUpdate) {
            component->slave->set_has_pending_events(false);
        }

        *discreteStatesNeedUpdate = info.discreteStatesNeedUpdate ? fmi3True : fmi3False;
        *terminateSimulation = info.terminateSimulation ? fmi3True : fmi3False;
        *nominalsOfContinuousStatesChanged = fmi3False;
        *valuesOfContinuousStatesChanged = fmi3False;
        *nextEventTimeDefined = info.nextEventTimeDefined ? fmi3True : fmi3False;
        *nextEventTime = info.nextEventTime;

        component->discreteStatesNeedUpdate = info.discreteStatesNeedUpdate;

        // Active clocks must be deactivated internally during this call (FMI 3.0 Section 2.4.6)
        for (const auto vr: component->slave->get_value_refs()) {
            auto *v = component->slave->get_variable(vr);
            if (v && v->type() == fmu4cpp::data_type::CLOCK) {
                auto *cv = const_cast<fmu4cpp::ClockVariable *>(dynamic_cast<const fmu4cpp::ClockVariable *>(v));
                if (cv && cv->causality() == fmu4cpp::causality_t::INPUT && cv->get()) {
                    cv->force_set(false);
                    component->slave->on_clock_deactivated(vr);
                }
            }
        }

        if (info.terminateSimulation) {
            component->state = Fmi3Component::State::Terminated;
        }

        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        component->logger->log(fmiFatal, ex.what());
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        component->logger->log(fmiError, ex.what());
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3EnterContinuousTimeMode(fmi3Instance instance) {
    if (!instance) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(instance);
    component->logger->log(fmiError, "fmi3EnterContinuousTimeMode is not supported");
    return fmi3Error;
}

fmi3Status fmi3CompletedIntegratorStep(fmi3Instance instance,
                                       fmi3Boolean noSetFMUStatePriorToCurrentPoint,
                                       fmi3Boolean *enterEventMode,
                                       fmi3Boolean *terminateSimulation) {

    return fmi3Error;
}

fmi3Status fmi3SetTime(fmi3Instance instance, fmi3Float64 time) {
    return fmi3Error;
}

fmi3Status fmi3SetContinuousStates(fmi3Instance instance,
                                   const fmi3Float64 continuousStates[],
                                   size_t nContinuousStates) {
    return fmi3Error;
}

fmi3Status fmi3GetContinuousStateDerivatives(fmi3Instance instance,
                                             fmi3Float64 derivatives[],
                                             size_t nContinuousStates) {
    return fmi3Error;
}

fmi3Status fmi3GetEventIndicators(fmi3Instance instance,
                                  fmi3Float64 eventIndicators[],
                                  size_t nEventIndicators) {
    return fmi3Error;
}

fmi3Status fmi3GetContinuousStates(fmi3Instance instance,
                                   fmi3Float64 continuousStates[],
                                   size_t nContinuousStates) {
    return fmi3Error;
}

fmi3Status fmi3GetNominalsOfContinuousStates(fmi3Instance instance,
                                             fmi3Float64 nominals[],
                                             size_t nContinuousStates) {
    return fmi3Error;
}
fmi3Status fmi3GetNumberOfEventIndicators(fmi3Instance instance,
                                          size_t *nEventIndicators) {
    return fmi3Error;
}

fmi3Status fmi3GetNumberOfContinuousStates(fmi3Instance instance,
                                           size_t *nContinuousStates) {
    return fmi3Error;
}

fmi3Status fmi3EnterStepMode(fmi3Instance instance) {
    if (!instance) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(instance);
    if (component->state == Fmi3Component::State::Invalid) return fmi3Fatal;
    if (component->state != Fmi3Component::State::EventMode) {
        component->logger->log(fmiError, "fmi3EnterStepMode: Invalid state. Expected EventMode.");
        return fmi3Error;
    }
    if (component->discreteStatesNeedUpdate) {
        component->logger->log(fmiError, "fmi3EnterStepMode: discreteStatesNeedUpdate is true. Must update discrete states until false.");
        return fmi3Error;
    }
    try {
        component->slave->enter_step_mode();
        component->state = Fmi3Component::State::StepMode;
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        component->logger->log(fmiFatal, ex.what());
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        component->logger->log(fmiError, ex.what());
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3ActivateModelPartition(fmi3Instance instance,
                                      fmi3ValueReference clockReference,
                                      fmi3Float64 activationTime) {

    return fmi3Error;
}

void fmi3FreeInstance(fmi3Instance c) {
    if (c) {
        const auto component = static_cast<Fmi3Component *>(c);
        component->state = Fmi3Component::State::Invalid;
        delete component;
        c = nullptr;
    }
}
}
