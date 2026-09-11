#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <fmu4cpp/fmu_base.hpp>
#include <fmu4cpp/fmu_except.hpp>
#include "fmi3/fmi3Functions.h"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

class SizedTypesModel : public fmu4cpp::fmu_base {
public:
    int8_t i8{10};
    uint8_t u8{20};
    int16_t i16{300};
    uint16_t u16{400};
    int32_t i32{50000};
    uint32_t u32{60000};
    int64_t i64{7000000000LL};
    uint64_t u64{8000000000ULL};
    float f32{1.5f};
    double f64{2.75};
    bool bval{true};

    FMU4CPP_CTOR(SizedTypesModel) {
        register_int8("i8", &i8).setCausality(fmu4cpp::causality_t::INPUT).setMin(-50).setMax(50);
        register_uint8("u8", &u8).setCausality(fmu4cpp::causality_t::INPUT).setMin(0).setMax(250);
        register_int16("i16", &i16).setCausality(fmu4cpp::causality_t::INPUT).setMin(-1000).setMax(1000);
        register_uint16("u16", &u16).setCausality(fmu4cpp::causality_t::INPUT).setMin(0).setMax(5000);
        register_int32("i32", &i32).setCausality(fmu4cpp::causality_t::INPUT);
        register_uint32("u32", &u32).setCausality(fmu4cpp::causality_t::INPUT);
        register_int64("i64", &i64).setCausality(fmu4cpp::causality_t::INPUT);
        register_uint64("u64", &u64).setCausality(fmu4cpp::causality_t::INPUT);
        register_float32("f32", &f32).setCausality(fmu4cpp::causality_t::INPUT).setUnit("m/s");
        register_float64("f64", &f64).setCausality(fmu4cpp::causality_t::INPUT).setUnit("m");
        register_boolean("bval", &bval).setCausality(fmu4cpp::causality_t::INPUT);
    }

    bool do_step(double dt) override {
        return true;
    }
};

void testLogger(fmi3InstanceEnvironment, fmi3Status status, fmi3String cat, fmi3String msg) {
    if (status >= fmi3Warning) {
        std::cerr << "LOG [" << status << "][" << (cat ? cat : "") << "]: " << (msg ? msg : "") << std::endl;
    }
}

fmi3Instance createTestInstance() {
    SizedTypesModel probe({});
    static std::string guid = probe.guid();
    return fmi3InstantiateCoSimulation(
            "inst_sized_types",
            guid.c_str(),
            nullptr,
            fmi3False,
            fmi3True,
            fmi3False,
            fmi3False,
            nullptr,
            0,
            nullptr,
            testLogger,
            nullptr);
}

void enterStepMode(fmi3Instance c) {
    REQUIRE(fmi3EnterInitializationMode(c, false, 0.0, 0.0, false, 0.0) == fmi3OK);
    REQUIRE(fmi3ExitInitializationMode(c) == fmi3OK);
}

} // namespace

fmu4cpp::model_info fmu4cpp::get_model_info() {
    fmu4cpp::model_info info;
    info.modelName = "sized_types_test";
    return info;
}

std::string fmu4cpp::model_identifier() {
    return "sized_types_test";
}

FMU4CPP_INSTANTIATE(SizedTypesModel);

TEST_CASE("fmi3_sized_types_c_abi_get_set") {
    auto *c = createTestInstance();
    REQUIRE(c != nullptr);
    enterStepMode(c);

    SizedTypesModel probe({});
    const auto vr_i8 = probe.get_variable("i8")->value_reference();
    const auto vr_u8 = probe.get_variable("u8")->value_reference();
    const auto vr_i16 = probe.get_variable("i16")->value_reference();
    const auto vr_u16 = probe.get_variable("u16")->value_reference();
    const auto vr_i32 = probe.get_variable("i32")->value_reference();
    const auto vr_u32 = probe.get_variable("u32")->value_reference();
    const auto vr_i64 = probe.get_variable("i64")->value_reference();
    const auto vr_u64 = probe.get_variable("u64")->value_reference();
    const auto vr_f32 = probe.get_variable("f32")->value_reference();
    const auto vr_f64 = probe.get_variable("f64")->value_reference();
    const auto vr_b = probe.get_variable("bval")->value_reference();

    // Int8
    fmi3Int8 val_i8{0};
    REQUIRE(fmi3GetInt8(c, &vr_i8, 1, &val_i8, 1) == fmi3OK);
    CHECK(val_i8 == 10);
    val_i8 = -15;
    REQUIRE(fmi3SetInt8(c, &vr_i8, 1, &val_i8, 1) == fmi3OK);
    val_i8 = 0;
    REQUIRE(fmi3GetInt8(c, &vr_i8, 1, &val_i8, 1) == fmi3OK);
    CHECK(val_i8 == -15);

    // UInt8
    fmi3UInt8 val_u8{0};
    REQUIRE(fmi3GetUInt8(c, &vr_u8, 1, &val_u8, 1) == fmi3OK);
    CHECK(val_u8 == 20);
    val_u8 = 200;
    REQUIRE(fmi3SetUInt8(c, &vr_u8, 1, &val_u8, 1) == fmi3OK);
    val_u8 = 0;
    REQUIRE(fmi3GetUInt8(c, &vr_u8, 1, &val_u8, 1) == fmi3OK);
    CHECK(val_u8 == 200);

    // Int16
    fmi3Int16 val_i16{0};
    REQUIRE(fmi3GetInt16(c, &vr_i16, 1, &val_i16, 1) == fmi3OK);
    CHECK(val_i16 == 300);
    val_i16 = -450;
    REQUIRE(fmi3SetInt16(c, &vr_i16, 1, &val_i16, 1) == fmi3OK);
    val_i16 = 0;
    REQUIRE(fmi3GetInt16(c, &vr_i16, 1, &val_i16, 1) == fmi3OK);
    CHECK(val_i16 == -450);

    // UInt16
    fmi3UInt16 val_u16{0};
    REQUIRE(fmi3GetUInt16(c, &vr_u16, 1, &val_u16, 1) == fmi3OK);
    CHECK(val_u16 == 400);
    val_u16 = 3200;
    REQUIRE(fmi3SetUInt16(c, &vr_u16, 1, &val_u16, 1) == fmi3OK);
    val_u16 = 0;
    REQUIRE(fmi3GetUInt16(c, &vr_u16, 1, &val_u16, 1) == fmi3OK);
    CHECK(val_u16 == 3200);

    // Int32
    fmi3Int32 val_i32{0};
    REQUIRE(fmi3GetInt32(c, &vr_i32, 1, &val_i32, 1) == fmi3OK);
    CHECK(val_i32 == 50000);
    val_i32 = -123456;
    REQUIRE(fmi3SetInt32(c, &vr_i32, 1, &val_i32, 1) == fmi3OK);
    val_i32 = 0;
    REQUIRE(fmi3GetInt32(c, &vr_i32, 1, &val_i32, 1) == fmi3OK);
    CHECK(val_i32 == -123456);

    // UInt32
    fmi3UInt32 val_u32{0};
    REQUIRE(fmi3GetUInt32(c, &vr_u32, 1, &val_u32, 1) == fmi3OK);
    CHECK(val_u32 == 60000);
    val_u32 = 999999;
    REQUIRE(fmi3SetUInt32(c, &vr_u32, 1, &val_u32, 1) == fmi3OK);
    val_u32 = 0;
    REQUIRE(fmi3GetUInt32(c, &vr_u32, 1, &val_u32, 1) == fmi3OK);
    CHECK(val_u32 == 999999);

    // Int64
    fmi3Int64 val_i64{0};
    REQUIRE(fmi3GetInt64(c, &vr_i64, 1, &val_i64, 1) == fmi3OK);
    CHECK(val_i64 == 7000000000LL);
    val_i64 = -5000000000LL;
    REQUIRE(fmi3SetInt64(c, &vr_i64, 1, &val_i64, 1) == fmi3OK);
    val_i64 = 0;
    REQUIRE(fmi3GetInt64(c, &vr_i64, 1, &val_i64, 1) == fmi3OK);
    CHECK(val_i64 == -5000000000LL);

    // UInt64
    fmi3UInt64 val_u64{0};
    REQUIRE(fmi3GetUInt64(c, &vr_u64, 1, &val_u64, 1) == fmi3OK);
    CHECK(val_u64 == 8000000000ULL);
    val_u64 = 12345678901234ULL;
    REQUIRE(fmi3SetUInt64(c, &vr_u64, 1, &val_u64, 1) == fmi3OK);
    val_u64 = 0;
    REQUIRE(fmi3GetUInt64(c, &vr_u64, 1, &val_u64, 1) == fmi3OK);
    CHECK(val_u64 == 12345678901234ULL);

    // Float32
    fmi3Float32 val_f32{0.0f};
    REQUIRE(fmi3GetFloat32(c, &vr_f32, 1, &val_f32, 1) == fmi3OK);
    CHECK(val_f32 == Catch::Approx(1.5f));
    val_f32 = 3.25f;
    REQUIRE(fmi3SetFloat32(c, &vr_f32, 1, &val_f32, 1) == fmi3OK);
    val_f32 = 0.0f;
    REQUIRE(fmi3GetFloat32(c, &vr_f32, 1, &val_f32, 1) == fmi3OK);
    CHECK(val_f32 == Catch::Approx(3.25f));

    // Float64
    fmi3Float64 val_f64{0.0};
    REQUIRE(fmi3GetFloat64(c, &vr_f64, 1, &val_f64, 1) == fmi3OK);
    CHECK(val_f64 == Catch::Approx(2.75));
    val_f64 = 9.125;
    REQUIRE(fmi3SetFloat64(c, &vr_f64, 1, &val_f64, 1) == fmi3OK);
    val_f64 = 0.0;
    REQUIRE(fmi3GetFloat64(c, &vr_f64, 1, &val_f64, 1) == fmi3OK);
    CHECK(val_f64 == Catch::Approx(9.125));

    // Boolean
    fmi3Boolean val_b{false};
    REQUIRE(fmi3GetBoolean(c, &vr_b, 1, &val_b, 1) == fmi3OK);
    CHECK(val_b == true);
    val_b = false;
    REQUIRE(fmi3SetBoolean(c, &vr_b, 1, &val_b, 1) == fmi3OK);
    val_b = true;
    REQUIRE(fmi3GetBoolean(c, &vr_b, 1, &val_b, 1) == fmi3OK);
    CHECK(val_b == false);

    fmi3FreeInstance(c);
}

TEST_CASE("fmi3_sized_types_type_mismatch_rejection") {
    auto *c = createTestInstance();
    REQUIRE(c != nullptr);
    enterStepMode(c);

    SizedTypesModel probe({});
    const auto vr_i8 = probe.get_variable("i8")->value_reference();
    const auto vr_f32 = probe.get_variable("f32")->value_reference();
    const auto vr_u64 = probe.get_variable("u64")->value_reference();

    fmi3Int32 i32{0};
    fmi3Float64 f64{0.0};
    fmi3Int64 i64{0};

    // Calling Int32 getter/setter on an Int8 variable must fail
    CHECK(fmi3GetInt32(c, &vr_i8, 1, &i32, 1) == fmi3Error);
    CHECK(fmi3Reset(c) == fmi3OK);
    enterStepMode(c);
    CHECK(fmi3SetInt32(c, &vr_i8, 1, &i32, 1) == fmi3Error);

    // Calling Float64 getter on a Float32 variable must fail
    CHECK(fmi3Reset(c) == fmi3OK);
    enterStepMode(c);
    CHECK(fmi3GetFloat64(c, &vr_f32, 1, &f64, 1) == fmi3Error);

    // Calling Int64 getter on a UInt64 variable must fail
    CHECK(fmi3Reset(c) == fmi3OK);
    enterStepMode(c);
    CHECK(fmi3GetInt64(c, &vr_u64, 1, &i64, 1) == fmi3Error);

    fmi3FreeInstance(c);
}

TEST_CASE("fmi3_sized_types_dimension_checks") {
    auto *c = createTestInstance();
    REQUIRE(c != nullptr);
    enterStepMode(c);

    SizedTypesModel probe({});
    const auto vr_i8 = probe.get_variable("i8")->value_reference();
    fmi3Int8 val_i8[2] = {0, 0};

    // nValues mismatch (nValues != nvr for scalar)
    CHECK(fmi3GetInt8(c, &vr_i8, 1, val_i8, 2) == fmi3Error);
    CHECK(fmi3Reset(c) == fmi3OK);
    enterStepMode(c);
    CHECK(fmi3GetInt8(c, &vr_i8, 1, val_i8, 0) == fmi3Error);
    CHECK(fmi3Reset(c) == fmi3OK);
    enterStepMode(c);
    CHECK(fmi3SetInt8(c, &vr_i8, 1, val_i8, 2) == fmi3Error);
    CHECK(fmi3Reset(c) == fmi3OK);
    enterStepMode(c);
    CHECK(fmi3SetInt8(c, &vr_i8, 1, val_i8, 0) == fmi3Error);

    fmi3FreeInstance(c);
}

TEST_CASE("fmi3_sized_types_model_description_xml") {
    SizedTypesModel model({});
    const std::string xml = model.make_description();

    // Verify all XML element names exist
    CHECK(xml.find("<Int8 name=\"i8\"") != std::string::npos);
    CHECK(xml.find("start=\"10\"") != std::string::npos);
    CHECK(xml.find("min=\"-50\"") != std::string::npos);
    CHECK(xml.find("max=\"50\"") != std::string::npos);

    CHECK(xml.find("<UInt8 name=\"u8\"") != std::string::npos);
    CHECK(xml.find("min=\"0\"") != std::string::npos);
    CHECK(xml.find("max=\"250\"") != std::string::npos);

    CHECK(xml.find("<Int16 name=\"i16\"") != std::string::npos);
    CHECK(xml.find("<UInt16 name=\"u16\"") != std::string::npos);
    CHECK(xml.find("<Int32 name=\"i32\"") != std::string::npos);
    CHECK(xml.find("<UInt32 name=\"u32\"") != std::string::npos);
    CHECK(xml.find("<Int64 name=\"i64\"") != std::string::npos);
    CHECK(xml.find("<UInt64 name=\"u64\"") != std::string::npos);

    CHECK(xml.find("<Float32 name=\"f32\"") != std::string::npos);
    CHECK(xml.find("unit=\"m/s\"") != std::string::npos);

    CHECK(xml.find("<Float64 name=\"f64\"") != std::string::npos);
    CHECK(xml.find("unit=\"m\"") != std::string::npos);

    CHECK(xml.find("<Boolean name=\"bval\"") != std::string::npos);
    CHECK(xml.find("start=\"true\"") != std::string::npos);
}

TEST_CASE("fmi3_sized_types_unified_registry_lookups") {
    SizedTypesModel model({});

    // Polymorphic lookup by name
    auto v_i8 = model.get_variable("i8");
    REQUIRE(v_i8 != nullptr);
    CHECK(v_i8->type() == fmu4cpp::data_type::INT8);
    CHECK(v_i8->type_name() == "Int8");

    // Templated lookup by type
    auto v_typed = model.get_variable<fmu4cpp::Int8Variable>("i8");
    REQUIRE(v_typed.has_value());
    CHECK(v_typed->get() == 10);

    // Mismatched type lookup returns nullopt
    auto v_wrong = model.get_variable<fmu4cpp::Float32Variable>("i8");
    CHECK(!v_wrong.has_value());

    // Lookup by value reference
    auto v_by_vr = model.get_variable(v_i8->value_reference());
    REQUIRE(v_by_vr != nullptr);
    CHECK(v_by_vr->name() == "i8");
}
