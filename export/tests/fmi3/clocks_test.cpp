#include "fmi3/fmi3Functions.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fmu4cpp/fmu_base.hpp>
#include <iostream>
#include <vector>

#include "ClocksModel.hpp"

namespace {

    void fmilogger(fmi3InstanceEnvironment, fmi3Status status, fmi3String /*category*/, fmi3String message) {
        std::cout << status << ": " << message << std::endl;
    }

}// namespace

std::string fmu4cpp::model_identifier() {
    return "ClocksModel";
}


TEST_CASE("fmi3_clocks_and_intervals") {
    ClocksModel model({});
    const auto guid = model.guid();

    // 1. Instantiation with eventModeUsed = false must fail because model has clocks
    auto c_fail = fmi3InstantiateCoSimulation(
            fmu4cpp::model_identifier().c_str(),
            guid.c_str(),
            "",
            false,
            true,
            false,// eventModeUsed = false
            false,
            nullptr,
            0,
            nullptr,
            fmilogger,
            nullptr);
    CHECK(c_fail == nullptr);

    // 2. Instantiation with eventModeUsed = true succeeds
    auto c = fmi3InstantiateCoSimulation(
            fmu4cpp::model_identifier().c_str(),
            guid.c_str(),
            "",
            false,
            true,
            true,// eventModeUsed = true
            false,
            nullptr,
            0,
            nullptr,
            fmilogger,
            nullptr);
    REQUIRE(c != nullptr);

    // 3. Enter and exit initialization mode -> transitions to EventMode
    REQUIRE(fmi3EnterInitializationMode(c, false, 0.0, 0.0, false, 0.0) == fmi3OK);
    REQUIRE(fmi3ExitInitializationMode(c) == fmi3OK);

    // 4. Test Shift Decimal on periodic clock (VR 3)
    fmi3ValueReference periodicVr = 3;
    fmi3Float64 shiftValue = 0.0;
    REQUIRE(fmi3GetShiftDecimal(c, &periodicVr, 1, &shiftValue) == fmi3OK);
    CHECK(shiftValue == 0.05);

    fmi3Float64 newShift = 0.08;
    REQUIRE(fmi3SetShiftDecimal(c, &periodicVr, 1, &newShift) == fmi3OK);
    REQUIRE(fmi3GetShiftDecimal(c, &periodicVr, 1, &shiftValue) == fmi3OK);
    CHECK(shiftValue == 0.08);

    // Negative tests for shift
    fmi3ValueReference nonClockVr = 4;
    CHECK(fmi3SetShiftDecimal(c, &nonClockVr, 1, &newShift) == fmi3Error);
    CHECK(fmi3SetShiftDecimal(c, nullptr, 1, &newShift) == fmi3Error);
    CHECK(fmi3SetShiftDecimal(c, &periodicVr, 1, nullptr) == fmi3Error);

    // Reset shift back to 0.05
    fmi3Float64 origShift = 0.05;
    REQUIRE(fmi3SetShiftDecimal(c, &periodicVr, 1, &origShift) == fmi3OK);

    // 5. Test Interval Decimal get/set and qualifiers on periodic clock (VR 3)
    fmi3Float64 intervalVal = 0.02;
    REQUIRE(fmi3SetIntervalDecimal(c, &periodicVr, 1, &intervalVal) == fmi3OK);

    fmi3Float64 queriedInterval = 0.0;
    fmi3IntervalQualifier qualifier;
    REQUIRE(fmi3GetIntervalDecimal(c, &periodicVr, 1, &queriedInterval, &qualifier) == fmi3OK);
    CHECK(queriedInterval == 0.02);
    CHECK(qualifier == fmi3IntervalChanged);

    // Second query should return IntervalUnchanged
    REQUIRE(fmi3GetIntervalDecimal(c, &periodicVr, 1, &queriedInterval, &qualifier) == fmi3OK);
    CHECK(queriedInterval == 0.02);
    CHECK(qualifier == fmi3IntervalUnchanged);

    // 6. Clocked variable access restriction
    // VR 4 is clocked by VR 1 (clockIn). In EventMode with clockIn inactive, accessing VR 4 should fail.
    fmi3ValueReference clockedVr = 4;
    fmi3Float64 varVal = 0.0;
    REQUIRE(fmi3GetFloat64(c, &clockedVr, 1, &varVal, 1) == fmi3Error);

    // Activate clockIn (VR 1)
    fmi3ValueReference clockInVr = 1;
    fmi3Clock clockActive = fmi3True;
    REQUIRE(fmi3SetClock(c, &clockInVr, 1, &clockActive) == fmi3OK);

    // Now accessing VR 4 must succeed
    REQUIRE(fmi3GetFloat64(c, &clockedVr, 1, &varVal, 1) == fmi3OK);
    CHECK(varVal == 42.0);

    fmi3Float64 newVarVal = 99.0;
    REQUIRE(fmi3SetFloat64(c, &clockedVr, 1, &newVarVal, 1) == fmi3OK);
    REQUIRE(fmi3GetFloat64(c, &clockedVr, 1, &varVal, 1) == fmi3OK);
    CHECK(varVal == 99.0);

    // 7. Output clock reading in Event Mode
    // Activating clockIn triggered on_clock_activated, which set clockOut_ = true
    fmi3ValueReference clockOutVr = 2;
    fmi3Clock clockOutVal = fmi3False;
    REQUIRE(fmi3GetClock(c, &clockOutVr, 1, &clockOutVal) == fmi3OK);
    CHECK(clockOutVal == fmi3True);

    // Second read of output clock in the same event iteration MUST still return true (idempotent read)
    REQUIRE(fmi3GetClock(c, &clockOutVr, 1, &clockOutVal) == fmi3OK);
    CHECK(clockOutVal == fmi3True);

    // 8. UpdateDiscreteStates deactivates active clocks (both input and output)
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
                    &nextEventTime) == fmi3OK);

    // Since clockIn was deactivated, accessing clockedVar again fails
    REQUIRE(fmi3GetFloat64(c, &clockedVr, 1, &varVal, 1) == fmi3Error);

    // Output clock is now deactivated by fmi3UpdateDiscreteStates
    REQUIRE(fmi3GetClock(c, &clockOutVr, 1, &clockOutVal) == fmi3OK);
    CHECK(clockOutVal == fmi3False);

    // 9. EnterStepMode, Terminate, and Free
    REQUIRE(fmi3EnterStepMode(c) == fmi3OK);
    REQUIRE(fmi3Terminate(c) == fmi3OK);
    fmi3FreeInstance(c);
}

TEST_CASE("fmi3_clocks_fmu_state_rollback") {
    ClocksModel model({});
    const auto guid = model.guid();

    auto c = fmi3InstantiateCoSimulation(
            fmu4cpp::model_identifier().c_str(),
            guid.c_str(),
            "",
            false,
            true,
            true,// eventModeUsed = true
            false,
            nullptr,
            0,
            nullptr,
            fmilogger,
            nullptr);
    REQUIRE(c != nullptr);

    REQUIRE(fmi3EnterInitializationMode(c, false, 0.0, 0.0, false, 0.0) == fmi3OK);
    REQUIRE(fmi3ExitInitializationMode(c) == fmi3OK);

    // Set periodic clock interval
    fmi3ValueReference periodicVr = 3;
    fmi3Float64 intervalVal = 0.05;
    REQUIRE(fmi3SetIntervalDecimal(c, &periodicVr, 1, &intervalVal) == fmi3OK);

    // Activate clockIn (VR 1), triggering clockOut (VR 2) = true
    fmi3ValueReference clockInVr = 1;
    fmi3Clock clockActive = fmi3True;
    REQUIRE(fmi3SetClock(c, &clockInVr, 1, &clockActive) == fmi3OK);

    // Set clocked variable (VR 4)
    fmi3ValueReference clockedVr = 4;
    fmi3Float64 varVal = 123.45;
    REQUIRE(fmi3SetFloat64(c, &clockedVr, 1, &varVal, 1) == fmi3OK);

    // Snapshot state while in EventMode with active clockIn, active clockOut, modified interval, and set clockedVar
    fmi3FMUState state1 = nullptr;
    REQUIRE(fmi3GetFMUState(c, &state1) == fmi3OK);
    REQUIRE(state1 != nullptr);

    // Read clockOut (multiple calls in same event iteration stay active)
    fmi3ValueReference clockOutVr = 2;
    fmi3Clock clockOutVal = fmi3False;
    REQUIRE(fmi3GetClock(c, &clockOutVr, 1, &clockOutVal) == fmi3OK);
    CHECK(clockOutVal == fmi3True);
    REQUIRE(fmi3GetClock(c, &clockOutVr, 1, &clockOutVal) == fmi3OK);
    CHECK(clockOutVal == fmi3True);

    // Read and clear interval qualifier
    fmi3Float64 queriedInterval = 0.0;
    fmi3IntervalQualifier qualifier;
    REQUIRE(fmi3GetIntervalDecimal(c, &periodicVr, 1, &queriedInterval, &qualifier) == fmi3OK);
    CHECK(qualifier == fmi3IntervalChanged);
    REQUIRE(fmi3GetIntervalDecimal(c, &periodicVr, 1, &queriedInterval, &qualifier) == fmi3OK);
    CHECK(qualifier == fmi3IntervalUnchanged);

    // Update discrete states deactivates active clocks (both input and output)
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
                    &nextEventTime) == fmi3OK);

    // clockedVar access fails because clockIn is inactive
    REQUIRE(fmi3GetFloat64(c, &clockedVr, 1, &varVal, 1) == fmi3Error);

    // clockOut is also deactivated by fmi3UpdateDiscreteStates
    REQUIRE(fmi3GetClock(c, &clockOutVr, 1, &clockOutVal) == fmi3OK);
    CHECK(clockOutVal == fmi3False);

    // Enter step mode and advance step
    REQUIRE(fmi3EnterStepMode(c) == fmi3OK);
    fmi3Boolean eventHandlingNeeded = fmi3False;
    fmi3Boolean earlyReturn = fmi3False;
    fmi3Float64 lastSuccessfulTime = 0.0;
    REQUIRE(fmi3DoStep(c, 0.0, 0.1, fmi3False, &eventHandlingNeeded, &terminateSimulation, &earlyReturn, &lastSuccessfulTime) == fmi3OK);

    // Now test rollback via fmi3SetFMUState
    REQUIRE(fmi3SetFMUState(c, state1) == fmi3OK);

    // Verify component restored to EventMode: fmi3GetClock is allowed and clockOut is active again!
    REQUIRE(fmi3GetClock(c, &clockOutVr, 1, &clockOutVal) == fmi3OK);
    CHECK(clockOutVal == fmi3True);
    REQUIRE(fmi3GetClock(c, &clockOutVr, 1, &clockOutVal) == fmi3OK);
    CHECK(clockOutVal == fmi3True);

    // Verify clockIn restored to active: clockedVar access succeeds and has the saved value
    REQUIRE(fmi3GetFloat64(c, &clockedVr, 1, &varVal, 1) == fmi3OK);
    CHECK(varVal == 123.45);

    // Verify interval qualifier restored to fmi3IntervalChanged
    REQUIRE(fmi3GetIntervalDecimal(c, &periodicVr, 1, &queriedInterval, &qualifier) == fmi3OK);
    CHECK(queriedInterval == 0.05);
    CHECK(qualifier == fmi3IntervalChanged);

    // Test serialization / deserialization roundtrip
    size_t stateSize = 0;
    REQUIRE(fmi3SerializedFMUStateSize(c, state1, &stateSize) == fmi3OK);
    CHECK(stateSize > 0);
    std::vector<fmi3Byte> buffer(stateSize);
    REQUIRE(fmi3SerializeFMUState(c, state1, buffer.data(), stateSize) == fmi3OK);

    fmi3FMUState deserializedState = nullptr;
    REQUIRE(fmi3DeserializeFMUState(c, buffer.data(), stateSize, &deserializedState) == fmi3OK);
    REQUIRE(deserializedState != nullptr);

    // Roll back to deserialized state
    REQUIRE(fmi3SetFMUState(c, deserializedState) == fmi3OK);
    REQUIRE(fmi3GetFloat64(c, &clockedVr, 1, &varVal, 1) == fmi3OK);
    CHECK(varVal == 123.45);

    // Test fmi3Reset restores initial clock states
    REQUIRE(fmi3Reset(c) == fmi3OK);

    REQUIRE(fmi3FreeFMUState(c, &deserializedState) == fmi3OK);
    REQUIRE(fmi3FreeFMUState(c, &state1) == fmi3OK);
    CHECK(state1 == nullptr);

    fmi3FreeInstance(c);
}

TEST_CASE("fmi3_periodic_clock_autonomous_scheduling") {
    ClocksModel model({});
    const auto guid = model.guid();

    auto c = fmi3InstantiateCoSimulation(
            fmu4cpp::model_identifier().c_str(),
            guid.c_str(),
            "",
            false,
            true,
            true,// eventModeUsed = true
            false,
            nullptr,
            0,
            nullptr,
            fmilogger,
            nullptr);
    REQUIRE(c != nullptr);

    REQUIRE(fmi3EnterInitializationMode(c, false, 0.0, 0.0, false, 0.0) == fmi3OK);
    REQUIRE(fmi3ExitInitializationMode(c) == fmi3OK);

    // VR 3 is periodic clock with shift = 0.05. Set interval = 0.05.
    fmi3ValueReference periodicVr = 3;
    fmi3Float64 intervalVal = 0.05;
    REQUIRE(fmi3SetIntervalDecimal(c, &periodicVr, 1, &intervalVal) == fmi3OK);

    // Initial event iteration: fmi3UpdateDiscreteStates reports nextEventTime = 0.05
    fmi3Boolean discreteStatesNeedUpdate = fmi3False;
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
                    &nextEventTime) == fmi3OK);
    CHECK(discreteStatesNeedUpdate == fmi3False);
    CHECK(nextEventTimeDefined == fmi3True);
    CHECK(nextEventTime == Catch::Approx(0.05));

    // Enter step mode at t = 0.0
    REQUIRE(fmi3EnterStepMode(c) == fmi3OK);

    // Step 1: t = 0.0 to 0.02 (dt = 0.02). Next tick is at 0.05, so no tick.
    fmi3Boolean eventHandlingNeeded = fmi3False;
    terminateSimulation = fmi3False;
    fmi3Boolean earlyReturn = fmi3False;
    fmi3Float64 lastSuccessfulTime = 0.0;
    REQUIRE(fmi3DoStep(c, 0.0, 0.02, fmi3False, &eventHandlingNeeded, &terminateSimulation, &earlyReturn, &lastSuccessfulTime) == fmi3OK);
    CHECK(eventHandlingNeeded == fmi3False);
    CHECK(lastSuccessfulTime == 0.02);

    // Step 2: t = 0.02 to 0.05 (dt = 0.03). Reaches next tick (0.05), so clock fires and triggers pending event!
    REQUIRE(fmi3DoStep(c, 0.02, 0.03, fmi3False, &eventHandlingNeeded, &terminateSimulation, &earlyReturn, &lastSuccessfulTime) == fmi3OK);
    CHECK(eventHandlingNeeded == fmi3True);
    CHECK(lastSuccessfulTime == 0.05);

    // Enter Event Mode to process the clock tick
    REQUIRE(fmi3EnterEventMode(c) == fmi3OK);

    // Periodic clock is active
    fmi3Clock periodicActive = fmi3False;
    REQUIRE(fmi3GetClock(c, &periodicVr, 1, &periodicActive) == fmi3OK);
    CHECK(periodicActive == fmi3True);

    // Snapshot state while clock is active
    fmi3FMUState snap = nullptr;
    REQUIRE(fmi3GetFMUState(c, &snap) == fmi3OK);
    REQUIRE(snap != nullptr);

    // Calling fmi3UpdateDiscreteStates deactivates the clock and reports the next event time (0.10)
    discreteStatesNeedUpdate = fmi3False;
    nominalsChanged = fmi3False;
    valuesChanged = fmi3False;
    nextEventTimeDefined = fmi3False;
    nextEventTime = 0.0;
    REQUIRE(fmi3UpdateDiscreteStates(
                    c,
                    &discreteStatesNeedUpdate,
                    &terminateSimulation,
                    &nominalsChanged,
                    &valuesChanged,
                    &nextEventTimeDefined,
                    &nextEventTime) == fmi3OK);

    REQUIRE(fmi3GetClock(c, &periodicVr, 1, &periodicActive) == fmi3OK);
    CHECK(periodicActive == fmi3False);
    CHECK(nextEventTimeDefined == fmi3True);
    CHECK(nextEventTime == Catch::Approx(0.10));

    // Rollback to snap: clock should be active again
    REQUIRE(fmi3SetFMUState(c, snap) == fmi3OK);
    REQUIRE(fmi3GetClock(c, &periodicVr, 1, &periodicActive) == fmi3OK);
    CHECK(periodicActive == fmi3True);

    // Deactivate via updateDiscreteStates again
    REQUIRE(fmi3UpdateDiscreteStates(
                    c,
                    &discreteStatesNeedUpdate,
                    &terminateSimulation,
                    &nominalsChanged,
                    &valuesChanged,
                    &nextEventTimeDefined,
                    &nextEventTime) == fmi3OK);
    CHECK(nextEventTimeDefined == fmi3True);
    CHECK(nextEventTime == Catch::Approx(0.10));

    // Return to Step Mode and step to 0.10
    REQUIRE(fmi3EnterStepMode(c) == fmi3OK);
    REQUIRE(fmi3DoStep(c, 0.05, 0.05, fmi3False, &eventHandlingNeeded, &terminateSimulation, &earlyReturn, &lastSuccessfulTime) == fmi3OK);
    CHECK(eventHandlingNeeded == fmi3True);
    CHECK(lastSuccessfulTime == 0.10);

    REQUIRE(fmi3FreeFMUState(c, &snap) == fmi3OK);
    REQUIRE(fmi3Terminate(c) == fmi3OK);
    fmi3FreeInstance(c);
}
