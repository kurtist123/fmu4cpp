#include <catch2/catch_test_macros.hpp>

#include <fmu4cpp/fmu_base.hpp>
#include <fmu4cpp/fmu_except.hpp>
#include "fmi3/fmi3Functions.h"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class FaultModel : public fmu4cpp::fmu_base {
public:
    FMU4CPP_CTOR(FaultModel) {
        register_real("val", &val_).setCausality(fmu4cpp::causality_t::INPUT);
        register_integer("fault_type", &fault_type_).setCausality(fmu4cpp::causality_t::INPUT);
        register_integer("ival", &ival_).setCausality(fmu4cpp::causality_t::INPUT);
        register_boolean("bval", &bval_).setCausality(fmu4cpp::causality_t::INPUT);
        register_string("sval", &sval_).setCausality(fmu4cpp::causality_t::INPUT);
        register_binary("binval", &binval_).setCausality(fmu4cpp::causality_t::INPUT);

        FaultModel::reset();
    }

    bool do_step(double dt) override {
        if (fault_type_ == 1) {
            throw std::runtime_error("simulated runtime exception in do_step");
        } else if (fault_type_ == 2) {
            throw fmu4cpp::fatal_error("simulated fatal exception in do_step");
        }
        val_ += dt;
        return true;
    }

    void reset() override {
        fmu_base::reset();
        val_ = 0.0;
        fault_type_ = 0;
        ival_ = 42;
        bval_ = true;
        sval_ = "hello";
        binval_ = {0x01, 0x02, 0x03};
    }

private:
    double val_{0.0};
    int fault_type_{0};
    int ival_{42};
    bool bval_{true};
    std::string sval_{"hello"};
    std::vector<uint8_t> binval_{0x01, 0x02, 0x03};
};

void nullLogger(fmi3InstanceEnvironment, fmi3Status, fmi3String, fmi3String) {
}

fmi3Instance createTestInstance(bool useLogger = true) {
    FaultModel m({});
    const auto guid = m.guid();
    return fmi3InstantiateCoSimulation(
            "fault_inst",
            guid.c_str(),
            "",
            false,
            true,
            false,
            false,
            nullptr,
            0,
            nullptr,
            useLogger ? nullLogger : nullptr,
            nullptr
    );
}

void enterStepMode(fmi3Instance c) {
    REQUIRE(fmi3EnterInitializationMode(c, false, 0.0, 0.0, false, 0.0) == fmi3OK);
    REQUIRE(fmi3ExitInitializationMode(c) == fmi3OK);
}

} // namespace

fmu4cpp::model_info fmu4cpp::get_model_info() {
    fmu4cpp::model_info info;
    info.modelName = "abi_negative_test";
    info.canGetAndSetFMUstate = true;
    return info;
}

std::string fmu4cpp::model_identifier() {
    return "abi_negative_test";
}

FMU4CPP_INSTANTIATE(FaultModel);

TEST_CASE("fmi3_null_instance_checks") {
    fmi3Float64 rval{0.0};
    fmi3Int32 ival{0};
    fmi3Boolean bval{false};
    fmi3String sval{nullptr};
    size_t binSize{0};
    const fmi3Byte *binVal{nullptr};
    fmi3ValueReference vr = 0;

    fmi3Boolean eh{false};
    fmi3Boolean term{false};
    fmi3Boolean er{false};
    fmi3Float64 last{0.0};

    fmi3FMUState state{nullptr};
    size_t stateSize{0};

    CHECK(fmi3EnterInitializationMode(nullptr, false, 0.0, 0.0, false, 0.0) == fmi3Error);
    CHECK(fmi3ExitInitializationMode(nullptr) == fmi3Error);
    CHECK(fmi3EnterStepMode(nullptr) == fmi3Error);
    CHECK(fmi3DoStep(nullptr, 0.0, 0.1, true, &eh, &term, &er, &last) == fmi3Error);
    CHECK(fmi3Reset(nullptr) == fmi3Error);
    CHECK(fmi3Terminate(nullptr) == fmi3Error);

    // Freeing null instance should be a safe no-op
    fmi3FreeInstance(nullptr);

    // Getters with null instance
    CHECK(fmi3GetFloat64(nullptr, &vr, 1, &rval, 1) == fmi3Error);
    CHECK(fmi3GetInt32(nullptr, &vr, 1, &ival, 1) == fmi3Error);
    CHECK(fmi3GetBoolean(nullptr, &vr, 1, &bval, 1) == fmi3Error);
    CHECK(fmi3GetString(nullptr, &vr, 1, &sval, 1) == fmi3Error);
    CHECK(fmi3GetBinary(nullptr, &vr, 1, &binSize, &binVal, 1) == fmi3Error);

    // Setters with null instance
    CHECK(fmi3SetFloat64(nullptr, &vr, 1, &rval, 1) == fmi3Error);
    CHECK(fmi3SetInt32(nullptr, &vr, 1, &ival, 1) == fmi3Error);
    CHECK(fmi3SetBoolean(nullptr, &vr, 1, &bval, 1) == fmi3Error);
    CHECK(fmi3SetString(nullptr, &vr, 1, &sval, 1) == fmi3Error);
    CHECK(fmi3SetBinary(nullptr, &vr, 1, &binSize, &binVal, 1) == fmi3Error);

    // State functions with null instance
    CHECK(fmi3GetFMUState(nullptr, &state) == fmi3Error);
    CHECK(fmi3SetFMUState(nullptr, state) == fmi3Error);
    CHECK(fmi3FreeFMUState(nullptr, &state) == fmi3Error);
    CHECK(fmi3SerializedFMUStateSize(nullptr, state, &stateSize) == fmi3Error);
    CHECK(fmi3SerializeFMUState(nullptr, state, nullptr, 0) == fmi3Error);
    CHECK(fmi3DeserializeFMUState(nullptr, nullptr, 0, &state) == fmi3Error);
}

TEST_CASE("fmi3_null_callback_instantiation") {
    // Instantiating with logMessage == nullptr must not crash even when errors are logged
    auto *c = createTestInstance(false);
    REQUIRE(c != nullptr);

    fmi3Boolean eh{false};
    fmi3Boolean term{false};
    fmi3Boolean er{false};
    fmi3Float64 last{0.0};

    // Calling fmi3DoStep in Instantiated state triggers an error log
    fmi3Status status = fmi3DoStep(c, 0.0, 0.1, true, &eh, &term, &er, &last);
    CHECK(status == fmi3Error);

    fmi3FreeInstance(c);
}

TEST_CASE("fmi3_lifecycle_violations") {
    auto *c = createTestInstance();
    REQUIRE(c != nullptr);

    fmi3Boolean eh{false};
    fmi3Boolean term{false};
    fmi3Boolean er{false};
    fmi3Float64 last{0.0};

    // 1. In Instantiated state, calling fmi3ExitInitializationMode is illegal
    CHECK(fmi3ExitInitializationMode(c) == fmi3Error);

    // 2. In Instantiated state, calling fmi3DoStep is illegal
    CHECK(fmi3DoStep(c, 0.0, 0.1, true, &eh, &term, &er, &last) == fmi3Error);

    // Reset back to Instantiated
    CHECK(fmi3Reset(c) == fmi3OK);

    // 3. Enter initialization mode
    CHECK(fmi3EnterInitializationMode(c, false, 0.0, 0.0, false, 0.0) == fmi3OK);

    // 4. Calling fmi3EnterInitializationMode again while already in InitializationMode is illegal
    CHECK(fmi3EnterInitializationMode(c, false, 0.0, 0.0, false, 0.0) == fmi3Error);

    // 5. In InitializationMode, calling fmi3DoStep is illegal
    CHECK(fmi3DoStep(c, 0.0, 0.1, true, &eh, &term, &er, &last) == fmi3Error);

    // Reset back to Instantiated and properly transition to StepMode
    CHECK(fmi3Reset(c) == fmi3OK);
    enterStepMode(c);

    // 6. In StepMode, calling fmi3EnterInitializationMode is illegal
    CHECK(fmi3EnterInitializationMode(c, false, 0.0, 0.0, false, 0.0) == fmi3Error);

    // 7. Terminate instance
    CHECK(fmi3Terminate(c) == fmi3OK);

    // 8. In Terminated state, fmi3DoStep is illegal
    CHECK(fmi3DoStep(c, 0.0, 0.1, true, &eh, &term, &er, &last) == fmi3Error);

    // 9. fmi3Terminate is idempotent
    CHECK(fmi3Terminate(c) == fmi3OK);

    // 10. fmi3Reset transitions back from Terminated to Instantiated
    CHECK(fmi3Reset(c) == fmi3OK);
    CHECK(fmi3EnterInitializationMode(c, false, 0.0, 0.0, false, 0.0) == fmi3OK);

    fmi3FreeInstance(c);
}

TEST_CASE("fmi3_dimension_and_null_arg_checks") {
    auto *c = createTestInstance();
    REQUIRE(c != nullptr);
    enterStepMode(c);

    FaultModel m({});
    const auto valVr = m.get_real_variable("val")->value_reference();
    const auto ivalVr = m.get_int_variable("ival")->value_reference();
    const auto bvalVr = m.get_bool_variable("bval")->value_reference();
    const auto svalVr = m.get_string_variable("sval")->value_reference();
    const auto binvalVr = m.get_binary_variable("binval")->value_reference();

    fmi3Float64 rval{0.0};
    fmi3Int32 ival{0};
    fmi3Boolean bval{false};
    fmi3String sval{nullptr};
    size_t binSize{0};
    const fmi3Byte *binVal{nullptr};

    // Null pointer checks for vr and values when nvr > 0
    CHECK(fmi3GetFloat64(c, nullptr, 1, &rval, 1) == fmi3Error);
    CHECK(fmi3GetFloat64(c, &valVr, 1, nullptr, 1) == fmi3Error);
    CHECK(fmi3SetFloat64(c, nullptr, 1, &rval, 1) == fmi3Error);
    CHECK(fmi3SetFloat64(c, &valVr, 1, nullptr, 1) == fmi3Error);

    // Dimension mismatch checks (nValues != nvr)
    CHECK(fmi3GetFloat64(c, &valVr, 1, &rval, 2) == fmi3Error);
    CHECK(fmi3GetFloat64(c, &valVr, 1, &rval, 0) == fmi3Error);
    CHECK(fmi3SetFloat64(c, &valVr, 1, &rval, 2) == fmi3Error);
    CHECK(fmi3SetFloat64(c, &valVr, 1, &rval, 0) == fmi3Error);

    CHECK(fmi3GetInt32(c, &ivalVr, 1, &ival, 2) == fmi3Error);
    CHECK(fmi3GetInt32(c, &ivalVr, 1, &ival, 0) == fmi3Error);
    CHECK(fmi3SetInt32(c, &ivalVr, 1, &ival, 2) == fmi3Error);
    CHECK(fmi3SetInt32(c, &ivalVr, 1, &ival, 0) == fmi3Error);

    CHECK(fmi3GetBoolean(c, &bvalVr, 1, &bval, 2) == fmi3Error);
    CHECK(fmi3GetBoolean(c, &bvalVr, 1, &bval, 0) == fmi3Error);
    CHECK(fmi3SetBoolean(c, &bvalVr, 1, &bval, 2) == fmi3Error);
    CHECK(fmi3SetBoolean(c, &bvalVr, 1, &bval, 0) == fmi3Error);

    CHECK(fmi3GetString(c, &svalVr, 1, &sval, 2) == fmi3Error);
    CHECK(fmi3GetString(c, &svalVr, 1, &sval, 0) == fmi3Error);
    CHECK(fmi3SetString(c, &svalVr, 1, &sval, 2) == fmi3Error);
    CHECK(fmi3SetString(c, &svalVr, 1, &sval, 0) == fmi3Error);

    CHECK(fmi3GetBinary(c, &binvalVr, 1, &binSize, &binVal, 2) == fmi3Error);
    CHECK(fmi3GetBinary(c, &binvalVr, 1, &binSize, &binVal, 0) == fmi3Error);
    CHECK(fmi3SetBinary(c, &binvalVr, 1, &binSize, &binVal, 2) == fmi3Error);
    CHECK(fmi3SetBinary(c, &binvalVr, 1, &binSize, &binVal, 0) == fmi3Error);

    // fmi3DoStep argument validation
    fmi3Boolean eh{false};
    fmi3Boolean term{false};
    fmi3Boolean er{false};
    fmi3Float64 last{0.0};

    // Null output pointer arguments
    CHECK(fmi3DoStep(c, 0.0, 0.1, true, nullptr, &term, &er, &last) == fmi3Error);
    CHECK(fmi3DoStep(c, 0.0, 0.1, true, &eh, nullptr, &er, &last) == fmi3Error);
    CHECK(fmi3DoStep(c, 0.0, 0.1, true, &eh, &term, nullptr, &last) == fmi3Error);
    CHECK(fmi3DoStep(c, 0.0, 0.1, true, &eh, &term, &er, nullptr) == fmi3Error);

    // Negative communication point or non-positive step size
    CHECK(fmi3DoStep(c, -1.0, 0.1, true, &eh, &term, &er, &last) == fmi3Error);
    CHECK(fmi3DoStep(c, 0.0, 0.0, true, &eh, &term, &er, &last) == fmi3Error);
    CHECK(fmi3DoStep(c, 0.0, -0.1, true, &eh, &term, &er, &last) == fmi3Error);

    fmi3FreeInstance(c);
}

TEST_CASE("fmi3_invalid_value_references") {
    auto *c = createTestInstance();
    REQUIRE(c != nullptr);
    enterStepMode(c);

    const fmi3ValueReference nonExistentVr = 999999;
    fmi3Float64 rval{0.0};

    // Accessing non-existent VR throws std::out_of_range;
    // C ABI wrapper must catch std::exception, transition state to Terminated, and return fmi3Error.
    CHECK(fmi3GetFloat64(c, &nonExistentVr, 1, &rval, 1) == fmi3Error);

    // Once in Terminated state, subsequent operations return fmi3Error
    fmi3Boolean eh{false};
    fmi3Boolean term{false};
    fmi3Boolean er{false};
    fmi3Float64 last{0.0};
    CHECK(fmi3DoStep(c, 0.0, 0.1, true, &eh, &term, &er, &last) == fmi3Error);

    fmi3FreeInstance(c);
}

TEST_CASE("fmi3_exception_isolation_runtime_error") {
    auto *c = createTestInstance();
    REQUIRE(c != nullptr);
    enterStepMode(c);

    FaultModel m({});
    const auto faultVr = m.get_int_variable("fault_type")->value_reference();
    const fmi3Int32 triggerRuntimeError = 1;

    // Set fault_type = 1 to trigger std::runtime_error during step
    REQUIRE(fmi3SetInt32(c, &faultVr, 1, &triggerRuntimeError, 1) == fmi3OK);

    fmi3Boolean eh{false};
    fmi3Boolean term{false};
    fmi3Boolean er{false};
    fmi3Float64 last{0.0};

    // std::runtime_error must be caught at C ABI boundary, return fmi3Error,
    // transition instance to Terminated, and NOT propagate out of fmi3DoStep.
    fmi3Status status = fmi3Error;
    REQUIRE_NOTHROW(status = fmi3DoStep(c, 0.0, 0.1, true, &eh, &term, &er, &last));
    CHECK(status == fmi3Error);

    // Calling doStep again on terminated instance returns fmi3Error
    CHECK(fmi3DoStep(c, 0.1, 0.1, true, &eh, &term, &er, &last) == fmi3Error);

    fmi3FreeInstance(c);
}

TEST_CASE("fmi3_exception_isolation_fatal_error") {
    auto *c = createTestInstance();
    REQUIRE(c != nullptr);
    enterStepMode(c);

    FaultModel m({});
    const auto faultVr = m.get_int_variable("fault_type")->value_reference();
    const fmi3Int32 triggerFatalError = 2;

    // Set fault_type = 2 to trigger fmu4cpp::fatal_error during step
    REQUIRE(fmi3SetInt32(c, &faultVr, 1, &triggerFatalError, 1) == fmi3OK);

    fmi3Boolean eh{false};
    fmi3Boolean term{false};
    fmi3Boolean er{false};
    fmi3Float64 last{0.0};

    // fatal_error must be caught at C ABI boundary, return fmi3Fatal,
    // transition instance to Invalid, and NOT propagate out of fmi3DoStep.
    fmi3Status status = fmi3Fatal;
    REQUIRE_NOTHROW(status = fmi3DoStep(c, 0.0, 0.1, true, &eh, &term, &er, &last));
    CHECK(status == fmi3Fatal);

    // Once Invalid, any subsequent call returns fmi3Fatal
    CHECK(fmi3DoStep(c, 0.1, 0.1, true, &eh, &term, &er, &last) == fmi3Fatal);

    fmi3FreeInstance(c);
}
