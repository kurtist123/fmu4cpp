#include "fmi3Functions.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "fmu4cpp/fmu_base.hpp"
#include "fmu4cpp/fmu_except.hpp"
#include "fmu4cpp/logger.hpp"
#include "fmu4cpp/model_info.hpp"
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
                return fmi3Error;
        }
    }

    class fmi3Logger : public fmu4cpp::logger {

    public:
        fmi3Logger(fmi3InstanceEnvironment env, fmi3LogMessageCallback logCallback, std::string instanceName)
            : logger(std::move(instanceName)), env_(env), logCallback_(logCallback) {}

        void debugLog(fmiStatus s, const std::string &message) override {
            if (!logCallback_) return;
            const std::string msg = instanceName_ + ": " + message;
            logCallback_(env_, toFmi3StatusFromCommon(s), nullptr, msg.c_str());
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
            EventMode = 1 << 2,
            StepMode = 1 << 3,
            Terminated = 1 << 4,
            Invalid = 1 << 5
        };

        Fmi3Component(std::unique_ptr<fmu4cpp::fmu_base> slave, std::unique_ptr<fmi3Logger> logger, bool eventModeUsed)
            : state(State::Instantiated),
              eventModeUsed(eventModeUsed),
              slave(std::move(slave)),
              logger(std::move(logger)) {}

        State state;
        bool eventModeUsed{false};
        std::unique_ptr<fmu4cpp::fmu_base> slave;
        std::unique_ptr<fmi3Logger> logger;
    };

    std::string parseResourcePath(fmi3String resourcePath) {
        if (!resourcePath || resourcePath[0] == '\0') {
            return "";
        }
        std::string_view sv(resourcePath);
        if (sv.rfind("file://localhost/", 0) == 0) {
            sv.remove_prefix(16);
        } else if (sv.rfind("file:////", 0) == 0) {
            sv.remove_prefix(7);
        } else if (sv.rfind("file:///", 0) == 0) {
#ifdef _WIN32
            sv.remove_prefix(8);
#else
            sv.remove_prefix(7);
#endif
        } else if (sv.rfind("file://", 0) == 0) {
            sv.remove_prefix(7);
        } else if (sv.rfind("file:/", 0) == 0) {
#ifdef _WIN32
            sv.remove_prefix(6);
#else
            sv.remove_prefix(5);
#endif
        }
        return std::string(sv);
    }

#define FMU_TYPE(type) fmi3##type

#define NOT_IMPLEMENTED_GETTER(type)                                                           \
    fmi3Status fmi3Get##type(                                                                  \
            fmi3Instance c,                                                                    \
            const fmi3ValueReference vr[],                                                     \
            size_t nValueReferences,                                                           \
            FMU_TYPE(type) values[],                                                           \
            size_t nValues) {                                                                  \
        if (!c) return fmi3Error;                                                              \
        try {                                                                                  \
            const auto component = static_cast<Fmi3Component *>(c);                            \
            component->logger->log(fmiError, std::string("Unsupported function fmi3Get") + #type); \
            return fmi3Error;                                                                  \
        } catch (...) {                                                                        \
            return fmi3Error;                                                                  \
        }                                                                                      \
    }

#define NOT_IMPLEMENTED_SETTER(type)                                                           \
    fmi3Status fmi3Set##type(                                                                  \
            fmi3Instance c,                                                                    \
            const fmi3ValueReference vr[],                                                     \
            size_t nValueReferences,                                                           \
            const FMU_TYPE(type) values[],                                                     \
            size_t nValues) {                                                                  \
        if (!c) return fmi3Error;                                                              \
        try {                                                                                  \
            const auto component = static_cast<Fmi3Component *>(c);                            \
            component->logger->log(fmiError, std::string("Unsupported function fmi3Set") + #type); \
            return fmi3Error;                                                                  \
        } catch (...) {                                                                        \
            return fmi3Error;                                                                  \
        }                                                                                      \
    }

}// namespace

extern "C" {

const char *fmi3GetVersion(void) {
    return "3.0";
}

FMI3_Export bool write_description(const char *location) {
    if (!location) return false;
    try {
        const auto instance = fmu4cpp::createInstance({});
        if (!instance) return false;
        const auto xml = instance->make_description();
        std::ofstream of(location);
        if (!of.is_open()) return false;
        of << xml;
        if (!of.good()) return false;
        of.close();
        return true;
    } catch (const std::exception &ex) {
        std::cerr << "[fmu4cpp fmi3] Error generating description: " << ex.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "[fmu4cpp fmi3] Unknown error generating description" << std::endl;
        return false;
    }
}

fmi3Instance fmi3InstantiateModelExchange(fmi3String instanceName,
                                          fmi3String instantiationToken,
                                          fmi3String resourcePath,
                                          fmi3Boolean visible,
                                          fmi3Boolean loggingOn,
                                          fmi3InstanceEnvironment instanceEnvironment,
                                          fmi3LogMessageCallback logMessage) {
    try {
        fmi3Logger l(instanceEnvironment, logMessage, instanceName ? instanceName : "");
        l.log(fmiFatal, "[fmu4cpp] Unsupported mode: Model Exchange");
    } catch (...) {}
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
    try {
        fmi3Logger l(instanceEnvironment, logMessage, instanceName ? instanceName : "");
        l.log(fmiFatal, "[fmu4cpp] Unsupported mode: Scheduled Execution");
    } catch (...) {}
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

    try {
        const std::string name = instanceName ? instanceName : "";
        auto logger = std::make_unique<fmi3Logger>(instanceEnvironment, logMessage, name);
        logger->setDebugLogging(loggingOn);

        const std::string resources = parseResourcePath(resourcePath);

        auto slave = fmu4cpp::createInstance(
                {
                        logger.get(),
                        name,
                        resources,
                        visible,
                });
        if (!slave) {
            logger->log(fmiFatal, "[fmu4cpp] Unable to instantiate model! createInstance returned nullptr");
            return nullptr;
        }

        const auto guid = slave->guid();
        if (instantiationToken == nullptr || guid != instantiationToken) {
            logger->log(fmiFatal, "[fmu4cpp] Error. Wrong guid!");
            return nullptr;
        }

        const auto info = fmu4cpp::get_model_info();
        const bool modelHasClocks = !slave->get_clocks().empty();
        const bool requiresEventMode = modelHasClocks || info.hasEventMode;

        if (requiresEventMode && !eventModeUsed) {
            logger->log(fmiFatal, "[fmu4cpp] Model requires EventMode (has clocks or hasEventMode=true), but eventModeUsed was false.");
            return nullptr;
        }

        auto c = std::make_unique<Fmi3Component>(std::move(slave), std::move(logger), eventModeUsed == fmi3True);
        return c.release();
    } catch (const std::exception &e) {
        try {
            fmi3Logger l(instanceEnvironment, logMessage, instanceName ? instanceName : "");
            l.log(fmiFatal, "[fmu4cpp] Unable to instantiate model! " + std::string(e.what()));
        } catch (...) {}
        return nullptr;
    } catch (...) {
        try {
            fmi3Logger l(instanceEnvironment, logMessage, instanceName ? instanceName : "");
            l.log(fmiFatal, "[fmu4cpp] Unable to instantiate model! Unknown exception.");
        } catch (...) {}
        return nullptr;
    }
}

fmi3Status fmi3EnterEventMode(fmi3Instance instance) {
    if (!instance) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(instance);
    try {
        if (component->state != Fmi3Component::State::StepMode) {
            component->logger->log(fmiError, "Invalid state for fmi3EnterEventMode. Expected StepMode.");
            return fmi3Error;
        }
        component->state = Fmi3Component::State::EventMode;
        component->slave->on_enter_event_mode();
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        try { component->logger->log(fmiFatal, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        try { component->logger->log(fmiError, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    } catch (...) {
        try { component->logger->log(fmiError, "Unknown exception in fmi3EnterEventMode."); } catch (...) {}
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
    if (!c) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(c);
    try {
        if (component->state != Fmi3Component::State::Instantiated) {
            throw std::logic_error("Invalid state. Expected Instantiated.");
        }
        std::optional<double> stop;
        std::optional<double> tol;
        if (stopTimeDefined) stop = stopTime;
        if (toleranceDefined) tol = tolerance;

        component->slave->enter_initialisation_mode(startTime, stop, tol);
        component->state = Fmi3Component::State::InitializationMode;
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        try { component->logger->log(fmiFatal, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        try { component->logger->log(fmiError, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    } catch (...) {
        try { component->logger->log(fmiError, "Unknown exception in fmi3EnterInitializationMode."); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3ExitInitializationMode(fmi3Instance c) {
    if (!c) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(c);
    try {
        if (component->state != Fmi3Component::State::InitializationMode) {
            throw std::logic_error("Invalid state. Expected InitializationMode.");
        }

        component->slave->exit_initialisation_mode();
        if (component->eventModeUsed) {
            component->state = Fmi3Component::State::EventMode;
            component->slave->on_enter_event_mode();
        } else {
            component->state = Fmi3Component::State::StepMode;
        }
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        try { component->logger->log(fmiFatal, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        try { component->logger->log(fmiError, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    } catch (...) {
        try { component->logger->log(fmiError, "Unknown exception in fmi3ExitInitializationMode."); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3Terminate(fmi3Instance c) {
    if (!c) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(c);
    try {
        component->slave->terminate();
        component->state = Fmi3Component::State::Terminated;
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        try { component->logger->log(fmiFatal, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        try { component->logger->log(fmiError, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    } catch (...) {
        try { component->logger->log(fmiError, "Unknown exception in fmi3Terminate."); } catch (...) {}
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
    if (!c) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(c);
    try {
        if (component->state != Fmi3Component::State::StepMode) {
            component->logger->log(fmiError, "Invalid state for fmi3DoStep. Expected StepMode.");
            return fmi3Error;
        }
        if (!eventHandlingNeeded || !terminateSimulation || !earlyReturn || !lastSuccessfulTime) {
            component->logger->log(fmiError, "Null pointer passed to fmi3DoStep.");
            return fmi3Error;
        }

        if (component->slave->step(currentCommunicationPoint, communicationStepSize)) {
            *earlyReturn = fmi3False;
            *terminateSimulation = fmi3False;
            *lastSuccessfulTime = currentCommunicationPoint + communicationStepSize;
            if (component->slave->has_pending_output_events()) {
                *eventHandlingNeeded = fmi3True;
            } else {
                *eventHandlingNeeded = fmi3False;
            }
            return fmi3OK;
        }

        component->logger->log(fmiWarning, "Step returned false!");
        return fmi3Discard;
    } catch (const fmu4cpp::fatal_error &ex) {
        try { component->logger->log(fmiFatal, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        try { component->logger->log(fmiError, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    } catch (...) {
        try { component->logger->log(fmiError, "Unknown exception in fmi3DoStep."); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3CancelStep(fmi3Instance instance) {
    if (!instance) return fmi3Error;
    try {
        const auto component = static_cast<Fmi3Component *>(instance);
        component->logger->log(fmiError, "fmi3CancelStep is not supported.");
        return fmi3Error;
    } catch (...) {
        return fmi3Error;
    }
}

fmi3Status fmi3Reset(fmi3Instance c) {
    if (!c) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(c);
    try {
        component->slave->reset();
        component->slave->clear_triggered_clocks();
        component->state = Fmi3Component::State::Instantiated;
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        try { component->logger->log(fmiFatal, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        try { component->logger->log(fmiError, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    } catch (...) {
        try { component->logger->log(fmiError, "Unknown exception in fmi3Reset."); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3GetInt32(
        fmi3Instance c,
        const fmi3ValueReference vr[],
        size_t nvr,
        fmi3Int32 value[],
        size_t nValues) {
    if (!c) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(c);
    try {
        if (nvr > 0 && (!vr || !value)) {
            component->logger->log(fmiError, "Null pointer passed to fmi3GetInt32.");
            return fmi3Error;
        }
        component->slave->get_integer(vr, nvr, value);
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        try { component->logger->log(fmiFatal, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        try { component->logger->log(fmiError, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    } catch (...) {
        try { component->logger->log(fmiError, "Unknown exception in fmi3GetInt32."); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3GetFloat64(
        fmi3Instance c,
        const fmi3ValueReference vr[],
        size_t nvr,
        fmi3Float64 value[],
        size_t nValues) {
    if (!c) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(c);
    try {
        if (nvr > 0 && (!vr || !value)) {
            component->logger->log(fmiError, "Null pointer passed to fmi3GetFloat64.");
            return fmi3Error;
        }
        component->slave->get_real(vr, nvr, value);
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        try { component->logger->log(fmiFatal, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        try { component->logger->log(fmiError, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    } catch (...) {
        try { component->logger->log(fmiError, "Unknown exception in fmi3GetFloat64."); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3GetBoolean(
        fmi3Instance c,
        const fmi3ValueReference vr[],
        size_t nvr,
        fmi3Boolean value[],
        size_t nValues) {
    if (!c) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(c);
    try {
        if (nvr > 0 && (!vr || !value)) {
            component->logger->log(fmiError, "Null pointer passed to fmi3GetBoolean.");
            return fmi3Error;
        }
        component->slave->get_boolean(vr, nvr, value);
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        try { component->logger->log(fmiFatal, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        try { component->logger->log(fmiError, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    } catch (...) {
        try { component->logger->log(fmiError, "Unknown exception in fmi3GetBoolean."); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3GetString(
        fmi3Instance c,
        const fmi3ValueReference vr[],
        size_t nvr,
        fmi3String value[],
        size_t nValues) {
    if (!c) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(c);
    try {
        if (nvr > 0 && (!vr || !value)) {
            component->logger->log(fmiError, "Null pointer passed to fmi3GetString.");
            return fmi3Error;
        }
        component->slave->get_string(vr, nvr, value);
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        try { component->logger->log(fmiFatal, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        try { component->logger->log(fmiError, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    } catch (...) {
        try { component->logger->log(fmiError, "Unknown exception in fmi3GetString."); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3SetInt32(
        fmi3Instance c,
        const fmi3ValueReference vr[],
        size_t nvr,
        const fmi3Int32 value[],
        size_t nValues) {
    if (!c) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(c);
    try {
        if (nvr > 0 && (!vr || !value)) {
            component->logger->log(fmiError, "Null pointer passed to fmi3SetInt32.");
            return fmi3Error;
        }
        component->slave->set_integer(vr, nvr, value);
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        try { component->logger->log(fmiFatal, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        try { component->logger->log(fmiError, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    } catch (...) {
        try { component->logger->log(fmiError, "Unknown exception in fmi3SetInt32."); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3SetFloat64(
        fmi3Instance c,
        const fmi3ValueReference vr[],
        size_t nvr,
        const fmi3Float64 value[],
        size_t nValues) {
    if (!c) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(c);
    try {
        if (nvr > 0 && (!vr || !value)) {
            component->logger->log(fmiError, "Null pointer passed to fmi3SetFloat64.");
            return fmi3Error;
        }
        component->slave->set_real(vr, nvr, value);
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        try { component->logger->log(fmiFatal, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        try { component->logger->log(fmiError, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    } catch (...) {
        try { component->logger->log(fmiError, "Unknown exception in fmi3SetFloat64."); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3SetBoolean(
        fmi3Instance c,
        const fmi3ValueReference vr[],
        size_t nvr,
        const fmi3Boolean value[],
        size_t nValues) {
    if (!c) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(c);
    try {
        if (nvr > 0 && (!vr || !value)) {
            component->logger->log(fmiError, "Null pointer passed to fmi3SetBoolean.");
            return fmi3Error;
        }
        component->slave->set_boolean(vr, nvr, value);
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        try { component->logger->log(fmiFatal, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        try { component->logger->log(fmiError, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    } catch (...) {
        try { component->logger->log(fmiError, "Unknown exception in fmi3SetBoolean."); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3SetString(
        fmi3Instance c,
        const fmi3ValueReference vr[],
        size_t nvr,
        const fmi3String value[],
        size_t nValues) {
    if (!c) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(c);
    try {
        if (nvr > 0 && (!vr || !value)) {
            component->logger->log(fmiError, "Null pointer passed to fmi3SetString.");
            return fmi3Error;
        }
        component->slave->set_string(vr, nvr, value);
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        try { component->logger->log(fmiFatal, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        try { component->logger->log(fmiError, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    } catch (...) {
        try { component->logger->log(fmiError, "Unknown exception in fmi3SetString."); } catch (...) {}
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
    if (!c) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(c);
    try {
        if (nvr > 0 && (!vr || !valueSizes || !values)) {
            component->logger->log(fmiError, "Null pointer passed to fmi3GetBinary.");
            return fmi3Error;
        }
        component->slave->get_binary(vr, nvr, valueSizes, values);
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        try { component->logger->log(fmiFatal, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        try { component->logger->log(fmiError, ex.what()); } catch (...) {}
        return fmi3Error;
    } catch (...) {
        try { component->logger->log(fmiError, "Unknown exception in fmi3GetBinary."); } catch (...) {}
        return fmi3Error;
    }
}

fmi3Status fmi3SetBinary(fmi3Instance c,
                         const fmi3ValueReference vr[],
                         size_t nvr,
                         const size_t valueSizes[],
                         const fmi3Binary values[],
                         size_t nValues) {
    if (!c) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(c);
    try {
        if (nvr > 0 && (!vr || !valueSizes || !values)) {
            component->logger->log(fmiError, "Null pointer passed to fmi3SetBinary.");
            return fmi3Error;
        }
        for (size_t i = 0; i < nvr; ++i) {
            if (valueSizes[i] > 0 && values[i] == nullptr) {
                component->logger->log(fmiError, "Null data pointer for non-zero binary value in fmi3SetBinary.");
                return fmi3Error;
            }
        }
        component->slave->set_binary(vr, nvr, valueSizes, values);
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        try { component->logger->log(fmiFatal, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        try { component->logger->log(fmiError, ex.what()); } catch (...) {}
        return fmi3Error;
    } catch (...) {
        try { component->logger->log(fmiError, "Unknown exception in fmi3SetBinary."); } catch (...) {}
        return fmi3Error;
    }
}

NOT_IMPLEMENTED_SETTER(UInt8);
NOT_IMPLEMENTED_SETTER(UInt16);
NOT_IMPLEMENTED_SETTER(UInt32);
NOT_IMPLEMENTED_SETTER(UInt64);

NOT_IMPLEMENTED_GETTER(UInt8);
NOT_IMPLEMENTED_GETTER(UInt16);
NOT_IMPLEMENTED_GETTER(UInt32);
NOT_IMPLEMENTED_GETTER(UInt64);

NOT_IMPLEMENTED_SETTER(Int8);
NOT_IMPLEMENTED_SETTER(Int16);
NOT_IMPLEMENTED_SETTER(Int64);

NOT_IMPLEMENTED_GETTER(Int8);
NOT_IMPLEMENTED_GETTER(Int16);
NOT_IMPLEMENTED_GETTER(Int64);

NOT_IMPLEMENTED_SETTER(Float32);
NOT_IMPLEMENTED_GETTER(Float32);

fmi3Status fmi3SetDebugLogging(fmi3Instance c,
                               fmi3Boolean loggingOn,
                               size_t /*nCategories*/,
                               const fmi3String /*categories*/[]) {
    if (!c) return fmi3Error;
    try {
        const auto component = static_cast<Fmi3Component *>(c);
        component->logger->setDebugLogging(loggingOn);
        return fmi3OK;
    } catch (...) {
        return fmi3Error;
    }
}

fmi3Status fmi3GetOutputDerivatives(fmi3Instance instance,
                                    const fmi3ValueReference[], size_t,
                                    const fmi3Int32[],
                                    fmi3Float64[],
                                    size_t) {
    if (!instance) return fmi3Error;
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
    if (!instance) return fmi3Error;
    return fmi3Error;
}

fmi3Status fmi3GetFMUState(fmi3Instance c, fmi3FMUState *state) {
    if (!c) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(c);
    try {
        if (!state) {
            component->logger->log(fmiError, "Null pointer passed to fmi3GetFMUState.");
            return fmi3Error;
        }
        const auto s = component->slave->getFMUState();
        *state = s;
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        try { component->logger->log(fmiFatal, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        try { component->logger->log(fmiError, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    } catch (...) {
        try { component->logger->log(fmiError, "Unknown exception in fmi3GetFMUState."); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3SetFMUState(fmi3Instance c, fmi3FMUState state) {
    if (!c) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(c);
    try {
        component->slave->setFmuState(state);
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        try { component->logger->log(fmiFatal, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        try { component->logger->log(fmiError, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    } catch (...) {
        try { component->logger->log(fmiError, "Unknown exception in fmi3SetFMUState."); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3FreeFMUState(fmi3Instance c, fmi3FMUState *state) {
    if (!c) return fmi3Error;
    if (state == nullptr || *state == nullptr) {
        return fmi3OK;
    }
    const auto component = static_cast<Fmi3Component *>(c);
    try {
        component->slave->freeFmuState(state);
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        try { component->logger->log(fmiFatal, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        try { component->logger->log(fmiError, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    } catch (...) {
        try { component->logger->log(fmiError, "Unknown exception in fmi3FreeFMUState."); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3SerializedFMUStateSize(fmi3Instance c, fmi3FMUState state, size_t *size) {
    if (!c) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(c);
    try {
        if (!size) {
            component->logger->log(fmiError, "Null pointer passed to fmi3SerializedFMUStateSize.");
            return fmi3Error;
        }
        component->slave->serializedFMUStateSize(state, *size);
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        try { component->logger->log(fmiFatal, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        try { component->logger->log(fmiError, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    } catch (...) {
        try { component->logger->log(fmiError, "Unknown exception in fmi3SerializedFMUStateSize."); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3SerializeFMUState(fmi3Instance c, fmi3FMUState state, fmi3Byte data[], size_t size) {
    if (!c) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(c);
    try {
        if (size > 0 && !data) {
            component->logger->log(fmiError, "Null pointer passed to fmi3SerializeFMUState.");
            return fmi3Error;
        }
        std::vector<uint8_t> serializedState(size);
        component->slave->serializeFMUState(state, serializedState);
        std::memcpy(data, serializedState.data(), size);
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        try { component->logger->log(fmiFatal, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        try { component->logger->log(fmiError, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    } catch (...) {
        try { component->logger->log(fmiError, "Unknown exception in fmi3SerializeFMUState."); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3DeserializeFMUState(fmi3Instance c, const fmi3Byte data[], size_t size, fmi3FMUState *state) {
    if (!c) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(c);
    try {
        if ((size > 0 && !data) || !state) {
            component->logger->log(fmiError, "Null pointer passed to fmi3DeserializeFMUState.");
            return fmi3Error;
        }
        std::vector<uint8_t> serializedState(data, data + size);
        component->slave->deserializeFMUState(serializedState, state);
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        try { component->logger->log(fmiFatal, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        try { component->logger->log(fmiError, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    } catch (...) {
        try { component->logger->log(fmiError, "Unknown exception in fmi3DeserializeFMUState."); } catch (...) {}
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
    try {
        if (component->state != Fmi3Component::State::EventMode &&
            component->state != Fmi3Component::State::InitializationMode) {
            component->logger->log(fmiError, "fmi3GetClock is only allowed in EventMode and InitializationMode.");
            return fmi3Error;
        }
        if (nValueReferences > 0 && (!valueReferences || !values)) {
            component->logger->log(fmiError, "Null pointer passed to fmi3GetClock.");
            return fmi3Error;
        }
        for (size_t i = 0; i < nValueReferences; ++i) {
            values[i] = component->slave->get_clock(valueReferences[i]);
        }
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        try { component->logger->log(fmiFatal, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        try { component->logger->log(fmiError, ex.what()); } catch (...) {}
        return fmi3Error;
    } catch (...) {
        try { component->logger->log(fmiError, "Unknown exception in fmi3GetClock."); } catch (...) {}
        return fmi3Error;
    }
}

fmi3Status fmi3SetClock(fmi3Instance instance,
                        const fmi3ValueReference valueReferences[],
                        size_t nValueReferences,
                        const fmi3Clock values[]) {
    if (!instance) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(instance);
    try {
        if (component->state != Fmi3Component::State::EventMode &&
            component->state != Fmi3Component::State::InitializationMode) {
            component->logger->log(fmiError, "fmi3SetClock is only allowed in EventMode and InitializationMode.");
            return fmi3Error;
        }
        if (nValueReferences > 0 && (!valueReferences || !values)) {
            component->logger->log(fmiError, "Null pointer passed to fmi3SetClock.");
            return fmi3Error;
        }
        for (size_t i = 0; i < nValueReferences; ++i) {
            component->slave->set_clock(valueReferences[i], values[i]);
        }
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        try { component->logger->log(fmiFatal, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        try { component->logger->log(fmiError, ex.what()); } catch (...) {}
        return fmi3Error;
    } catch (...) {
        try { component->logger->log(fmiError, "Unknown exception in fmi3SetClock."); } catch (...) {}
        return fmi3Error;
    }
}

fmi3Status fmi3GetNumberOfVariableDependencies(fmi3Instance instance,
                                               fmi3ValueReference valueReference,
                                               size_t *nDependencies) {
    if (!instance) return fmi3Error;
    return fmi3Error;
}

fmi3Status fmi3GetVariableDependencies(fmi3Instance instance,
                                       fmi3ValueReference dependent,
                                       size_t elementIndicesOfDependent[],
                                       fmi3ValueReference independents[],
                                       size_t elementIndicesOfIndependents[],
                                       fmi3DependencyKind dependencyKinds[],
                                       size_t nDependencies) {
    if (!instance) return fmi3Error;
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
    if (!instance) return fmi3Error;
    return fmi3Error;
}

fmi3Status fmi3EnterConfigurationMode(fmi3Instance instance) {
    if (!instance) return fmi3Error;
    try {
        const auto component = static_cast<Fmi3Component *>(instance);
        component->logger->log(fmiError, "fmi3EnterConfigurationMode is not supported.");
        return fmi3Error;
    } catch (...) {
        return fmi3Error;
    }
}

fmi3Status fmi3ExitConfigurationMode(fmi3Instance instance) {
    if (!instance) return fmi3Error;
    return fmi3Error;
}

fmi3Status fmi3GetIntervalDecimal(fmi3Instance instance,
                                  const fmi3ValueReference valueReferences[],
                                  size_t nValueReferences,
                                  fmi3Float64 intervals[],
                                  fmi3IntervalQualifier qualifiers[]) {
    if (!instance) return fmi3Error;
    return fmi3Error;
}

fmi3Status fmi3GetIntervalFraction(fmi3Instance instance,
                                   const fmi3ValueReference valueReferences[],
                                   size_t nValueReferences,
                                   fmi3UInt64 counters[],
                                   fmi3UInt64 resolutions[],
                                   fmi3IntervalQualifier qualifiers[]) {
    if (!instance) return fmi3Error;
    return fmi3Error;
}

fmi3Status fmi3GetShiftDecimal(fmi3Instance instance,
                               const fmi3ValueReference valueReferences[],
                               size_t nValueReferences,
                               fmi3Float64 shifts[]) {
    if (!instance) return fmi3Error;
    return fmi3Error;
}

fmi3Status fmi3GetShiftFraction(fmi3Instance instance,
                                const fmi3ValueReference valueReferences[],
                                size_t nValueReferences,
                                fmi3UInt64 counters[],
                                fmi3UInt64 resolutions[]) {
    if (!instance) return fmi3Error;
    return fmi3Error;
}

fmi3Status fmi3SetIntervalDecimal(fmi3Instance instance,
                                  const fmi3ValueReference valueReferences[],
                                  size_t nValueReferences,
                                  const fmi3Float64 intervals[]) {
    if (!instance) return fmi3Error;
    return fmi3Error;
}

fmi3Status fmi3SetIntervalFraction(fmi3Instance instance,
                                   const fmi3ValueReference valueReferences[],
                                   size_t nValueReferences,
                                   const fmi3UInt64 counters[],
                                   const fmi3UInt64 resolutions[]) {
    if (!instance) return fmi3Error;
    return fmi3Error;
}

fmi3Status fmi3SetShiftDecimal(fmi3Instance instance,
                               const fmi3ValueReference valueReferences[],
                               size_t nValueReferences,
                               const fmi3Float64 shifts[]) {
    if (!instance) return fmi3Error;
    return fmi3Error;
}

fmi3Status fmi3SetShiftFraction(fmi3Instance instance,
                                const fmi3ValueReference valueReferences[],
                                size_t nValueReferences,
                                const fmi3UInt64 counters[],
                                const fmi3UInt64 resolutions[]) {
    if (!instance) return fmi3Error;
    return fmi3Error;
}

fmi3Status fmi3EvaluateDiscreteStates(fmi3Instance instance) {
    if (!instance) return fmi3Error;
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
    try {
        if (component->state != Fmi3Component::State::EventMode) {
            component->logger->log(fmiError, "fmi3UpdateDiscreteStates is only allowed in EventMode.");
            return fmi3Error;
        }
        if (!discreteStatesNeedUpdate || !terminateSimulation ||
            !nominalsOfContinuousStatesChanged || !valuesOfContinuousStatesChanged ||
            !nextEventTimeDefined || !nextEventTime) {
            component->logger->log(fmiError, "Null pointer passed to fmi3UpdateDiscreteStates.");
            return fmi3Error;
        }
        bool needUpdate = false;
        bool termSim = false;
        component->slave->update_discrete_states(needUpdate, termSim);
        component->slave->clear_triggered_clocks();

        *discreteStatesNeedUpdate = needUpdate ? fmi3True : fmi3False;
        *terminateSimulation = termSim ? fmi3True : fmi3False;
        *nominalsOfContinuousStatesChanged = fmi3False;
        *valuesOfContinuousStatesChanged = fmi3False;
        *nextEventTimeDefined = fmi3False;
        *nextEventTime = 0.0;
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        try { component->logger->log(fmiFatal, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        try { component->logger->log(fmiError, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    } catch (...) {
        try { component->logger->log(fmiError, "Unknown exception in fmi3UpdateDiscreteStates."); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3EnterContinuousTimeMode(fmi3Instance instance) {
    if (!instance) return fmi3Error;
    try {
        const auto component = static_cast<Fmi3Component *>(instance);
        component->logger->log(fmiError, "fmi3EnterContinuousTimeMode is not supported.");
        return fmi3Error;
    } catch (...) {
        return fmi3Error;
    }
}

fmi3Status fmi3CompletedIntegratorStep(fmi3Instance instance,
                                       fmi3Boolean noSetFMUStatePriorToCurrentPoint,
                                       fmi3Boolean *enterEventMode,
                                       fmi3Boolean *terminateSimulation) {
    if (!instance) return fmi3Error;
    return fmi3Error;
}

fmi3Status fmi3SetTime(fmi3Instance instance, fmi3Float64 time) {
    if (!instance) return fmi3Error;
    return fmi3Error;
}

fmi3Status fmi3SetContinuousStates(fmi3Instance instance,
                                   const fmi3Float64 continuousStates[],
                                   size_t nContinuousStates) {
    if (!instance) return fmi3Error;
    return fmi3Error;
}

fmi3Status fmi3GetContinuousStateDerivatives(fmi3Instance instance,
                                             fmi3Float64 derivatives[],
                                             size_t nContinuousStates) {
    if (!instance) return fmi3Error;
    return fmi3Error;
}

fmi3Status fmi3GetEventIndicators(fmi3Instance instance,
                                  fmi3Float64 eventIndicators[],
                                  size_t nEventIndicators) {
    if (!instance) return fmi3Error;
    return fmi3Error;
}

fmi3Status fmi3GetContinuousStates(fmi3Instance instance,
                                   fmi3Float64 continuousStates[],
                                   size_t nContinuousStates) {
    if (!instance) return fmi3Error;
    return fmi3Error;
}

fmi3Status fmi3GetNominalsOfContinuousStates(fmi3Instance instance,
                                             fmi3Float64 nominals[],
                                             size_t nContinuousStates) {
    if (!instance) return fmi3Error;
    return fmi3Error;
}

fmi3Status fmi3GetNumberOfEventIndicators(fmi3Instance instance,
                                          size_t *nEventIndicators) {
    if (!instance) return fmi3Error;
    return fmi3Error;
}

fmi3Status fmi3GetNumberOfContinuousStates(fmi3Instance instance,
                                           size_t *nContinuousStates) {
    if (!instance) return fmi3Error;
    return fmi3Error;
}

fmi3Status fmi3EnterStepMode(fmi3Instance instance) {
    if (!instance) return fmi3Error;
    const auto component = static_cast<Fmi3Component *>(instance);
    try {
        if (component->state != Fmi3Component::State::EventMode) {
            component->logger->log(fmiError, "Invalid state for fmi3EnterStepMode. Expected EventMode.");
            return fmi3Error;
        }
        component->slave->on_exit_event_mode();
        component->state = Fmi3Component::State::StepMode;
        return fmi3OK;
    } catch (const fmu4cpp::fatal_error &ex) {
        try { component->logger->log(fmiFatal, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Invalid;
        return fmi3Fatal;
    } catch (const std::exception &ex) {
        try { component->logger->log(fmiError, ex.what()); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    } catch (...) {
        try { component->logger->log(fmiError, "Unknown exception in fmi3EnterStepMode."); } catch (...) {}
        component->state = Fmi3Component::State::Terminated;
        return fmi3Error;
    }
}

fmi3Status fmi3ActivateModelPartition(fmi3Instance instance,
                                      fmi3ValueReference clockReference,
                                      fmi3Float64 activationTime) {
    if (!instance) return fmi3Error;
    return fmi3Error;
}

void fmi3FreeInstance(fmi3Instance c) {
    if (c) {
        try {
            const auto component = static_cast<Fmi3Component *>(c);
            component->state = Fmi3Component::State::Invalid;
            delete component;
        } catch (...) {}
    }
}

}
