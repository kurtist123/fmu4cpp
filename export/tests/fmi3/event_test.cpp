#include "fmi3/fmi3Functions.h"

#include <catch2/catch_test_macros.hpp>
#include <fmu4cpp/fmu_base.hpp>
#include <iostream>
#include <vector>

namespace {

void testLogger(fmi3InstanceEnvironment, fmi3Status status, fmi3String /*category*/, fmi3String message) {
    std::cout << "[testLogger status=" << status << "] " << message << std::endl;
}

class EventModel : public fmu4cpp::fmu_base {
public:
    FMU4CPP_CTOR(EventModel) {
        register_clock("inClock", fmu4cpp::causality_t::INPUT);
        register_clock("outClock", fmu4cpp::causality_t::OUTPUT);

        register_integer("stepCount", &stepCount_).setCausality(fmu4cpp::causality_t::OUTPUT);
        register_integer("eventModeEnterCount", &eventModeEnterCount_).setCausality(fmu4cpp::causality_t::OUTPUT);
        register_integer("eventModeExitCount", &eventModeExitCount_).setCausality(fmu4cpp::causality_t::OUTPUT);
        register_integer("discreteUpdateCount", &discreteUpdateCount_).setCausality(fmu4cpp::causality_t::OUTPUT);
        register_integer("requestSecondIteration", &requestSecondIteration_).setCausality(fmu4cpp::causality_t::INPUT);
        register_integer("triggerTerminate", &triggerTerminate_).setCausality(fmu4cpp::causality_t::INPUT);

        EventModel::reset();
    }

    void on_enter_event_mode() noexcept override {
        eventModeEnterCount_++;
    }

    void on_exit_event_mode() noexcept override {
        eventModeExitCount_++;
    }

    void update_discrete_states(bool &discreteStatesNeedUpdate, bool &terminateSimulation) override {
        discreteUpdateCount_++;
        if (requestSecondIteration_ > 0) {
            discreteStatesNeedUpdate = true;
            requestSecondIteration_ = 0; // Next iteration will converge
        } else {
            discreteStatesNeedUpdate = false;
        }

        if (triggerTerminate_ > 0) {
            terminateSimulation = true;
        }
    }

    bool do_step(double dt) override {
        stepCount_++;
        if (stepCount_ == 2) {
            const auto outClock = get_clock_variable("outClock");
            notify_output_event(outClock->value_reference());
        }
        return true;
    }

    void reset() override {
        stepCount_ = 0;
        eventModeEnterCount_ = 0;
        eventModeExitCount_ = 0;
        discreteUpdateCount_ = 0;
        requestSecondIteration_ = 0;
        triggerTerminate_ = 0;
    }

private:
    int stepCount_{0};
    int eventModeEnterCount_{0};
    int eventModeExitCount_{0};
    int discreteUpdateCount_{0};
    int requestSecondIteration_{0};
    int triggerTerminate_{0};
};

} // namespace

fmu4cpp::model_info fmu4cpp::get_model_info() {
    fmu4cpp::model_info info;
    info.hasEventMode = true;
    return info;
}

std::string fmu4cpp::model_identifier() {
    return "event_test";
}

FMU4CPP_INSTANTIATE(EventModel);

TEST_CASE("FMI3 Event Mode Negotiation Rejection") {
    EventModel model({});
    const auto guid = model.guid();

    // Model has clocks and hasEventMode=true. Calling with eventModeUsed = false must fail!
    auto c = fmi3InstantiateCoSimulation(
            fmu4cpp::model_identifier().c_str(),
            guid.c_str(),
            "",
            false,
            true,
            false, // eventModeUsed = false
            false,
            nullptr, 0,
            nullptr, testLogger, nullptr);

    REQUIRE(c == nullptr);
}

TEST_CASE("FMI3 Event Mode Lifecycle and State Transitions") {
    EventModel model({});
    const auto guid = model.guid();

    const auto inClockVar = model.get_clock_variable("inClock");
    const auto outClockVar = model.get_clock_variable("outClock");
    const auto enterCountVar = model.get_int_variable("eventModeEnterCount");
    const auto exitCountVar = model.get_int_variable("eventModeExitCount");
    REQUIRE(inClockVar.has_value());
    REQUIRE(outClockVar.has_value());

    const fmi3ValueReference inClockVr = inClockVar->value_reference();
    const fmi3ValueReference outClockVr = outClockVar->value_reference();
    const fmi3ValueReference enterCountVr = enterCountVar->value_reference();
    const fmi3ValueReference exitCountVr = exitCountVar->value_reference();

    auto c = fmi3InstantiateCoSimulation(
            fmu4cpp::model_identifier().c_str(),
            guid.c_str(),
            "",
            false,
            true,
            true, // eventModeUsed = true
            false,
            nullptr, 0,
            nullptr, testLogger, nullptr);
    REQUIRE(c != nullptr);

    // In Instantiated state, fmi3EnterEventMode or fmi3EnterStepMode must fail
    CHECK(fmi3EnterEventMode(c) == fmi3Error);
    CHECK(fmi3EnterStepMode(c) == fmi3Error);

    // Enter Initialization Mode
    REQUIRE(fmi3EnterInitializationMode(c, false, 0, 0, false, 0) == fmi3OK);

    // In Initialization Mode, fmi3GetClock and fmi3SetClock are allowed
    fmi3Clock clockVal = fmi3ClockActive;
    CHECK(fmi3SetClock(c, &inClockVr, 1, &clockVal) == fmi3OK);
    fmi3Clock readClock = fmi3ClockInactive;
    CHECK(fmi3GetClock(c, &inClockVr, 1, &readClock) == fmi3OK);
    CHECK(readClock == fmi3ClockActive);

    // Reset clock back to inactive before exiting initialization
    clockVal = fmi3ClockInactive;
    CHECK(fmi3SetClock(c, &inClockVr, 1, &clockVal) == fmi3OK);

    // Exit Initialization Mode -> With eventModeUsed=true, transitions to EventMode
    REQUIRE(fmi3ExitInitializationMode(c) == fmi3OK);

    // Verify on_enter_event_mode() was invoked
    fmi3Int32 enterCount = 0;
    CHECK(fmi3GetInt32(c, &enterCountVr, 1, &enterCount, 1) == fmi3OK);
    CHECK(enterCount == 1);

    // While in EventMode:
    // fmi3DoStep is forbidden
    bool eventHandlingNeeded = false;
    bool terminateSimulation = false;
    bool earlyReturn = false;
    double lastSuccessfulTime = 0.0;
    CHECK(fmi3DoStep(c, 0.0, 0.1, true, &eventHandlingNeeded, &terminateSimulation, &earlyReturn, &lastSuccessfulTime) == fmi3Error);

    // fmi3EnterEventMode is forbidden (already in EventMode, only allowed from StepMode)
    CHECK(fmi3EnterEventMode(c) == fmi3Error);

    // fmi3EnterStepMode transitions to StepMode
    REQUIRE(fmi3EnterStepMode(c) == fmi3OK);

    // Verify on_exit_event_mode() was invoked
    fmi3Int32 exitCount = 0;
    CHECK(fmi3GetInt32(c, &exitCountVr, 1, &exitCount, 1) == fmi3OK);
    CHECK(exitCount == 1);

    // While in StepMode:
    // fmi3GetClock and fmi3SetClock are forbidden
    CHECK(fmi3GetClock(c, &inClockVr, 1, &readClock) == fmi3Error);
    CHECK(fmi3SetClock(c, &inClockVr, 1, &clockVal) == fmi3Error);

    // fmi3EnterStepMode is forbidden (only allowed from EventMode)
    CHECK(fmi3EnterStepMode(c) == fmi3Error);

    // Step 1: No output event
    REQUIRE(fmi3DoStep(c, 0.0, 0.1, true, &eventHandlingNeeded, &terminateSimulation, &earlyReturn, &lastSuccessfulTime) == fmi3OK);
    CHECK(eventHandlingNeeded == fmi3False);
    CHECK(earlyReturn == fmi3False);
    CHECK(lastSuccessfulTime == 0.1);

    // Step 2: Model triggers output event!
    REQUIRE(fmi3DoStep(c, 0.1, 0.1, true, &eventHandlingNeeded, &terminateSimulation, &earlyReturn, &lastSuccessfulTime) == fmi3OK);
    CHECK(eventHandlingNeeded == fmi3True);
    CHECK(earlyReturn == fmi3False);
    CHECK(lastSuccessfulTime == 0.2);

    // Co-simulation master enters EventMode in response to eventHandlingNeeded
    REQUIRE(fmi3EnterEventMode(c) == fmi3OK);

    // Check enter count incremented
    CHECK(fmi3GetInt32(c, &enterCountVr, 1, &enterCount, 1) == fmi3OK);
    CHECK(enterCount == 2);

    // Inspect triggered output clock
    fmi3Clock outClockVal = fmi3ClockInactive;
    CHECK(fmi3GetClock(c, &outClockVr, 1, &outClockVal) == fmi3OK);
    CHECK(outClockVal == fmi3ClockActive);

    // Clock inspection idempotency: reading again must NOT deactivate or clear the clock
    outClockVal = fmi3ClockInactive;
    CHECK(fmi3GetClock(c, &outClockVr, 1, &outClockVal) == fmi3OK);
    CHECK(outClockVal == fmi3ClockActive);

    // Update discrete states: commits event and clears triggered clocks
    fmi3Boolean discreteStatesNeedUpdate = fmi3False;
    fmi3Boolean termSim = fmi3False;
    fmi3Boolean nomChanged = fmi3False;
    fmi3Boolean valChanged = fmi3False;
    fmi3Boolean nextTimeDef = fmi3False;
    fmi3Float64 nextTime = 0.0;

    REQUIRE(fmi3UpdateDiscreteStates(c, &discreteStatesNeedUpdate, &termSim, &nomChanged, &valChanged, &nextTimeDef, &nextTime) == fmi3OK);
    CHECK(discreteStatesNeedUpdate == fmi3False);
    CHECK(termSim == fmi3False);

    // After fmi3UpdateDiscreteStates, triggered clocks are cleared
    CHECK(fmi3GetClock(c, &outClockVr, 1, &outClockVal) == fmi3OK);
    CHECK(outClockVal == fmi3ClockInactive);

    // Transition back to StepMode
    REQUIRE(fmi3EnterStepMode(c) == fmi3OK);
    CHECK(fmi3GetInt32(c, &exitCountVr, 1, &exitCount, 1) == fmi3OK);
    CHECK(exitCount == 2);

    REQUIRE(fmi3Terminate(c) == fmi3OK);
    fmi3FreeInstance(c);
}

TEST_CASE("FMI3 Super-Dense Time Handling via fmi3UpdateDiscreteStates") {
    EventModel model({});
    const auto guid = model.guid();

    const auto reqSecondVar = model.get_int_variable("requestSecondIteration");
    const auto updateCountVar = model.get_int_variable("discreteUpdateCount");
    const fmi3ValueReference reqSecondVr = reqSecondVar->value_reference();
    const fmi3ValueReference updateCountVr = updateCountVar->value_reference();

    auto c = fmi3InstantiateCoSimulation(
            fmu4cpp::model_identifier().c_str(),
            guid.c_str(),
            "",
            false,
            true,
            true,
            false,
            nullptr, 0,
            nullptr, testLogger, nullptr);
    REQUIRE(c != nullptr);

    REQUIRE(fmi3EnterInitializationMode(c, false, 0, 0, false, 0) == fmi3OK);
    REQUIRE(fmi3ExitInitializationMode(c) == fmi3OK);

    // Now in EventMode. Set requestSecondIteration = 1
    fmi3Int32 req = 1;
    REQUIRE(fmi3SetInt32(c, &reqSecondVr, 1, &req, 1) == fmi3OK);

    fmi3Boolean discreteStatesNeedUpdate = fmi3False;
    fmi3Boolean termSim = fmi3False;
    fmi3Boolean nomChanged = fmi3False;
    fmi3Boolean valChanged = fmi3False;
    fmi3Boolean nextTimeDef = fmi3False;
    fmi3Float64 nextTime = 0.0;

    // Super-dense time iteration 1
    REQUIRE(fmi3UpdateDiscreteStates(c, &discreteStatesNeedUpdate, &termSim, &nomChanged, &valChanged, &nextTimeDef, &nextTime) == fmi3OK);
    CHECK(discreteStatesNeedUpdate == fmi3True);

    fmi3Int32 updateCount = 0;
    CHECK(fmi3GetInt32(c, &updateCountVr, 1, &updateCount, 1) == fmi3OK);
    CHECK(updateCount == 1);

    // Super-dense time iteration 2 (converged)
    REQUIRE(fmi3UpdateDiscreteStates(c, &discreteStatesNeedUpdate, &termSim, &nomChanged, &valChanged, &nextTimeDef, &nextTime) == fmi3OK);
    CHECK(discreteStatesNeedUpdate == fmi3False);

    CHECK(fmi3GetInt32(c, &updateCountVr, 1, &updateCount, 1) == fmi3OK);
    CHECK(updateCount == 2);

    REQUIRE(fmi3EnterStepMode(c) == fmi3OK);
    REQUIRE(fmi3Terminate(c) == fmi3OK);
    fmi3FreeInstance(c);
}

TEST_CASE("FMI3 Discrete State Simulation Termination Signal") {
    EventModel model({});
    const auto guid = model.guid();

    const auto termVar = model.get_int_variable("triggerTerminate");
    const fmi3ValueReference termVr = termVar->value_reference();

    auto c = fmi3InstantiateCoSimulation(
            fmu4cpp::model_identifier().c_str(),
            guid.c_str(),
            "",
            false,
            true,
            true,
            false,
            nullptr, 0,
            nullptr, testLogger, nullptr);
    REQUIRE(c != nullptr);

    REQUIRE(fmi3EnterInitializationMode(c, false, 0, 0, false, 0) == fmi3OK);
    REQUIRE(fmi3ExitInitializationMode(c) == fmi3OK);

    fmi3Int32 trig = 1;
    REQUIRE(fmi3SetInt32(c, &termVr, 1, &trig, 1) == fmi3OK);

    fmi3Boolean discreteStatesNeedUpdate = fmi3False;
    fmi3Boolean termSim = fmi3False;
    fmi3Boolean nomChanged = fmi3False;
    fmi3Boolean valChanged = fmi3False;
    fmi3Boolean nextTimeDef = fmi3False;
    fmi3Float64 nextTime = 0.0;

    REQUIRE(fmi3UpdateDiscreteStates(c, &discreteStatesNeedUpdate, &termSim, &nomChanged, &valChanged, &nextTimeDef, &nextTime) == fmi3OK);
    CHECK(termSim == fmi3True);

    REQUIRE(fmi3Terminate(c) == fmi3OK);
    fmi3FreeInstance(c);
}

TEST_CASE("FMI3 Resource Path and Exception Containment") {
    EventModel model({});
    const auto guid = model.guid();

    // 1. Nullable resourcePath
    auto cNullPath = fmi3InstantiateCoSimulation(
            fmu4cpp::model_identifier().c_str(),
            guid.c_str(),
            nullptr, // NULL resourcePath
            false,
            true,
            true,
            false,
            nullptr, 0,
            nullptr, testLogger, nullptr);
    REQUIRE(cNullPath != nullptr);
    fmi3FreeInstance(cNullPath);

    // 2. Absolute filesystem path (should not strip anything)
    auto cAbsPath = fmi3InstantiateCoSimulation(
            fmu4cpp::model_identifier().c_str(),
            guid.c_str(),
            "/usr/local/resources",
            false,
            true,
            true,
            false,
            nullptr, 0,
            nullptr, testLogger, nullptr);
    REQUIRE(cAbsPath != nullptr);
    fmi3FreeInstance(cAbsPath);

    // 3. URI path (file://)
    auto cUriPath = fmi3InstantiateCoSimulation(
            fmu4cpp::model_identifier().c_str(),
            guid.c_str(),
            "file:///usr/local/resources",
            false,
            true,
            true,
            false,
            nullptr, 0,
            nullptr, testLogger, nullptr);
    REQUIRE(cUriPath != nullptr);

    // 4. Exception Containment: pass null pointers to C ABI
    CHECK(fmi3GetClock(nullptr, nullptr, 0, nullptr) == fmi3Error);
    CHECK(fmi3SetClock(nullptr, nullptr, 0, nullptr) == fmi3Error);
    CHECK(fmi3UpdateDiscreteStates(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) == fmi3Error);
    CHECK(fmi3EnterEventMode(nullptr) == fmi3Error);
    CHECK(fmi3EnterStepMode(nullptr) == fmi3Error);
    CHECK(fmi3DoStep(nullptr, 0, 0, true, nullptr, nullptr, nullptr, nullptr) == fmi3Error);
    CHECK(fmi3GetBinary(nullptr, nullptr, 0, nullptr, nullptr, 0) == fmi3Error);
    CHECK(fmi3SetBinary(nullptr, nullptr, 0, nullptr, nullptr, 0) == fmi3Error);

    // Passing invalid VR to fmi3GetClock / fmi3SetClock in initialization mode
    REQUIRE(fmi3EnterInitializationMode(cUriPath, false, 0, 0, false, 0) == fmi3OK);
    fmi3ValueReference badVr = 999999;
    fmi3Clock clk = fmi3ClockActive;
    CHECK(fmi3GetClock(cUriPath, &badVr, 1, &clk) == fmi3Error);
    CHECK(fmi3SetClock(cUriPath, &badVr, 1, &clk) == fmi3Error);

    fmi3FreeInstance(cUriPath);
}
