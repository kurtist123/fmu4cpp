#include "fmi3/fmi3Functions.h"

#include <catch2/catch_test_macros.hpp>
#include <fmu4cpp/fmu_base.hpp>
#include <iostream>

namespace {

void fmilogger(fmi3InstanceEnvironment, fmi3Status status, fmi3String /*category*/, fmi3String message) {
    std::cout << status << ": " << message << std::endl;
}

class EventModel : public fmu4cpp::fmu_base {
public:
    FMU4CPP_CTOR(EventModel) {
        register_real("v", &v_).setCausality(fmu4cpp::causality_t::OUTPUT);
        EventModel::reset();
    }

    void reset() override {
        fmu_base::reset();
        v_ = 0.0;
        event_mode_entered_ = false;
        step_mode_entered_ = false;
        pending_events_ = false;
        update_calls_ = 0;
    }

    bool do_step(double dt) override {
        v_ += dt;
        if (v_ >= 1.0) {
            pending_events_ = true;
        }
        return true;
    }

    void enter_event_mode() override {
        event_mode_entered_ = true;
    }

    void enter_step_mode() override {
        step_mode_entered_ = true;
    }

    void update_discrete_states(fmu4cpp::discrete_states_info &info) override {
        update_calls_++;
        info.discreteStatesNeedUpdate = false;
        info.terminateSimulation = false;
        info.nominalsOfContinuousStatesChanged = false;
        info.valuesOfContinuousStatesChanged = false;
        info.nextEventTimeDefined = false;
        info.nextEventTime = 0.0;
        pending_events_ = false;
    }

    bool has_pending_events() const override {
        return pending_events_;
    }

    double v_{0.0};
    bool event_mode_entered_{false};
    bool step_mode_entered_{false};
    bool pending_events_{false};
    int update_calls_{0};
};

} // namespace

fmu4cpp::model_info fmu4cpp::get_model_info() {
    fmu4cpp::model_info info;
    info.modelName = "EventModel";
    info.hasEventMode = true;
    return info;
}

std::string fmu4cpp::model_identifier() {
    return "EventModel";
}

FMU4CPP_INSTANTIATE(EventModel);

TEST_CASE("fmi3_event_mode_lifecycle") {
    EventModel model({});
    const auto guid = model.guid();

    // 1. Instantiation with eventModeUsed = fmi3True
    auto c = fmi3InstantiateCoSimulation(
        fmu4cpp::model_identifier().c_str(),
        guid.c_str(),
        "",
        false,
        true,
        true, // eventModeUsed
        false,
        nullptr,
        0,
        nullptr,
        fmilogger,
        nullptr
    );
    REQUIRE(c != nullptr);

    // 2. InitializationMode
    REQUIRE(fmi3EnterInitializationMode(c, false, 0.0, 0.0, false, 0.0) == fmi3OK);

    // 3. ExitInitializationMode -> transitions directly to EventMode because eventModeUsed is true
    REQUIRE(fmi3ExitInitializationMode(c) == fmi3OK);

    // 4. Entering StepMode before UpdateDiscreteStates must fail (discreteStatesNeedUpdate is true)
    REQUIRE(fmi3EnterStepMode(c) == fmi3Error);

    // 5. UpdateDiscreteStates
    fmi3Boolean discreteStatesNeedUpdate = fmi3True;
    fmi3Boolean terminateSimulation = fmi3False;
    fmi3Boolean nominalsChanged = fmi3False;
    fmi3Boolean valuesChanged = fmi3False;
    fmi3Boolean nextEventTimeDefined = fmi3False;
    fmi3Float64 nextEventTime = 0.0;

    REQUIRE(fmi3UpdateDiscreteStates(
        c,
        &discreteStatesNeedUpdate,
        &terminateSimulation,
        &nominalsChanged,
        &valuesChanged,
        &nextEventTimeDefined,
        &nextEventTime
    ) == fmi3OK);

    CHECK(discreteStatesNeedUpdate == fmi3False);
    CHECK(terminateSimulation == fmi3False);

    // 6. EnterStepMode now succeeds
    REQUIRE(fmi3EnterStepMode(c) == fmi3OK);

    // 7. DoStep
    fmi3Boolean eventHandlingNeeded = fmi3False;
    fmi3Boolean earlyReturn = fmi3False;
    fmi3Float64 lastSuccessfulTime = 0.0;

    // Step 0.5s -> v becomes 0.5, no pending events
    REQUIRE(fmi3DoStep(c, 0.0, 0.5, fmi3False, &eventHandlingNeeded, &terminateSimulation, &earlyReturn, &lastSuccessfulTime) == fmi3OK);
    CHECK(eventHandlingNeeded == fmi3False);

    // Step another 0.6s -> v becomes 1.1 >= 1.0, pending events triggered
    REQUIRE(fmi3DoStep(c, 0.5, 0.6, fmi3False, &eventHandlingNeeded, &terminateSimulation, &earlyReturn, &lastSuccessfulTime) == fmi3OK);
    CHECK(eventHandlingNeeded == fmi3True);

    // 8. Transition back to EventMode
    REQUIRE(fmi3EnterEventMode(c) == fmi3OK);

    // 9. UpdateDiscreteStates clears pending events
    REQUIRE(fmi3UpdateDiscreteStates(
        c,
        &discreteStatesNeedUpdate,
        &terminateSimulation,
        &nominalsChanged,
        &valuesChanged,
        &nextEventTimeDefined,
        &nextEventTime
    ) == fmi3OK);
    CHECK(discreteStatesNeedUpdate == fmi3False);

    // 10. EnterStepMode again
    REQUIRE(fmi3EnterStepMode(c) == fmi3OK);

    // 11. Can terminate from StepMode or EventMode
    REQUIRE(fmi3EnterEventMode(c) == fmi3OK);
    REQUIRE(fmi3Terminate(c) == fmi3OK);

    // 12. Reset
    REQUIRE(fmi3Reset(c) == fmi3OK);

    fmi3FreeInstance(c);
}
