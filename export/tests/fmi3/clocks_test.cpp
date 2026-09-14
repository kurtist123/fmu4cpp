#include "fmi3/fmi3Functions.h"

#include <catch2/catch_test_macros.hpp>
#include <fmu4cpp/fmu_base.hpp>
#include <iostream>

namespace {

void fmilogger(fmi3InstanceEnvironment, fmi3Status status, fmi3String /*category*/, fmi3String message) {
    std::cout << status << ": " << message << std::endl;
}

class ClocksModel : public fmu4cpp::fmu_base {
public:
    FMU4CPP_CTOR(ClocksModel) {
        // VR 0: time (from fmu_base)
        // VR 1: clockIn (Input clock)
        register_clock("clockIn", &clockIn_).setCausality(fmu4cpp::causality_t::INPUT);
        // VR 2: clockOut (Output clock)
        register_clock("clockOut", &clockOut_).setCausality(fmu4cpp::causality_t::OUTPUT);
        // VR 3: clockPeriodic (Periodic clock with shift and interval)
        register_clock("clockPeriodic", &clockPeriodic_)
            .setIntervalVariability(fmu4cpp::interval_variability_t::TUNABLE)
            .setShiftDecimal(0.05);
        // VR 4: clockedVar (associated with clockIn, VR 1)
        register_real("clockedVar", &clockedVar_).setCausality(fmu4cpp::causality_t::INPUT).setClocks({1});

        ClocksModel::reset();
    }

    bool do_step(double dt) override {
        return true;
    }

    void reset() override {
        fmu_base::reset();
        clockIn_ = false;
        clockOut_ = false;
        clockPeriodic_ = false;
        clockedVar_ = 42.0;
        clock_activated_count_ = 0;
        clock_deactivated_count_ = 0;
    }

    void on_clock_activated(unsigned int vr) override {
        if (vr == 1) {
            clock_activated_count_++;
            // When clockIn activates, trigger clockOut as an event response
            clockOut_ = true;
        }
    }

    void on_clock_deactivated(unsigned int vr) override {
        if (vr == 1) {
            clock_deactivated_count_++;
        }
    }

    void update_discrete_states(fmu4cpp::discrete_states_info &info) override {
        info.discreteStatesNeedUpdate = false;
        info.terminateSimulation = false;
        info.nominalsOfContinuousStatesChanged = false;
        info.valuesOfContinuousStatesChanged = false;
        info.nextEventTimeDefined = false;
        info.nextEventTime = 0.0;
    }

    bool clockIn_{false};
    bool clockOut_{false};
    bool clockPeriodic_{false};
    double clockedVar_{42.0};
    int clock_activated_count_{0};
    int clock_deactivated_count_{0};
};

} // namespace

fmu4cpp::model_info fmu4cpp::get_model_info() {
    fmu4cpp::model_info info;
    info.modelName = "ClocksModel";
    return info;
}

std::string fmu4cpp::model_identifier() {
    return "ClocksModel";
}

FMU4CPP_INSTANTIATE(ClocksModel);

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
        false, // eventModeUsed = false
        false,
        nullptr,
        0,
        nullptr,
        fmilogger,
        nullptr
    );
    CHECK(c_fail == nullptr);

    // 2. Instantiation with eventModeUsed = true succeeds
    auto c = fmi3InstantiateCoSimulation(
        fmu4cpp::model_identifier().c_str(),
        guid.c_str(),
        "",
        false,
        true,
        true, // eventModeUsed = true
        false,
        nullptr,
        0,
        nullptr,
        fmilogger,
        nullptr
    );
    REQUIRE(c != nullptr);

    // 3. Enter and exit initialization mode -> transitions to EventMode
    REQUIRE(fmi3EnterInitializationMode(c, false, 0.0, 0.0, false, 0.0) == fmi3OK);
    REQUIRE(fmi3ExitInitializationMode(c) == fmi3OK);

    // 4. Test Shift Decimal on periodic clock (VR 3)
    fmi3ValueReference periodicVr = 3;
    fmi3Float64 shiftValue = 0.0;
    REQUIRE(fmi3GetShiftDecimal(c, &periodicVr, 1, &shiftValue) == fmi3OK);
    CHECK(shiftValue == 0.05);

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

    // 7. Output clock one-shot reading
    // Activating clockIn triggered on_clock_activated, which set clockOut_ = true
    fmi3ValueReference clockOutVr = 2;
    fmi3Clock clockOutVal = fmi3False;
    REQUIRE(fmi3GetClock(c, &clockOutVr, 1, &clockOutVal) == fmi3OK);
    CHECK(clockOutVal == fmi3True);

    // Second read of output clock returns false because it is one-shot deactivated
    REQUIRE(fmi3GetClock(c, &clockOutVr, 1, &clockOutVal) == fmi3OK);
    CHECK(clockOutVal == fmi3False);

    // 8. UpdateDiscreteStates deactivates input clocks
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

    // Since clockIn was deactivated, accessing clockedVar again fails
    REQUIRE(fmi3GetFloat64(c, &clockedVr, 1, &varVal, 1) == fmi3Error);

    // 9. EnterStepMode, Terminate, and Free
    REQUIRE(fmi3EnterStepMode(c) == fmi3OK);
    REQUIRE(fmi3Terminate(c) == fmi3OK);
    fmi3FreeInstance(c);
}
