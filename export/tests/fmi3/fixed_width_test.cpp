#include "fmi3/fmi3Functions.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fmu4cpp/fmu_base.hpp>
#include <iostream>

class FixedWidthModel : public fmu4cpp::fmu_base {
public:
    FMU4CPP_CTOR(FixedWidthModel) {
        // Pointer registrations
        register_float32("f32_in", &f32_val)
                .setCausality(fmu4cpp::causality_t::INPUT)
                .setMin(0.0f)
                .setMax(100.0f)
                .setUnit("m/s");
        register_int8("i8_in", &i8_val)
                .setCausality(fmu4cpp::causality_t::INPUT)
                .setMin(static_cast<int8_t>(-10))
                .setMax(static_cast<int8_t>(10));
        register_uint8("u8_in", &u8_val)
                .setCausality(fmu4cpp::causality_t::INPUT)
                .setMin(static_cast<uint8_t>(0))
                .setMax(static_cast<uint8_t>(200));
        register_int16("i16_in", &i16_val)
                .setCausality(fmu4cpp::causality_t::INPUT)
                .setMin(static_cast<int16_t>(-1000))
                .setMax(static_cast<int16_t>(1000));
        register_uint16("u16_in", &u16_val)
                .setCausality(fmu4cpp::causality_t::INPUT)
                .setMin(static_cast<uint16_t>(0))
                .setMax(static_cast<uint16_t>(50000));
        register_uint32("u32_in", &u32_val)
                .setCausality(fmu4cpp::causality_t::INPUT)
                .setMin(static_cast<uint32_t>(0))
                .setMax(static_cast<uint32_t>(4000000000U));
        register_int64("i64_in", &i64_val)
                .setCausality(fmu4cpp::causality_t::INPUT)
                .setMin(static_cast<int64_t>(-9000000000000000000LL))
                .setMax(static_cast<int64_t>(9000000000000000000LL));
        register_uint64("u64_in", &u64_val)
                .setCausality(fmu4cpp::causality_t::INPUT)
                .setMin(static_cast<uint64_t>(0))
                .setMax(static_cast<uint64_t>(18000000000000000000ULL));

        // Lambda registrations for outputs
        register_float32("f32_out", [this] { return f32_val * 2.0f; })
                .setCausality(fmu4cpp::causality_t::OUTPUT);
        register_int8("i8_out", [this] { return static_cast<int8_t>(i8_val + 1); })
                .setCausality(fmu4cpp::causality_t::OUTPUT);
        register_uint8("u8_out", [this] { return static_cast<uint8_t>(u8_val + 2); })
                .setCausality(fmu4cpp::causality_t::OUTPUT);
        register_int16("i16_out", [this] { return static_cast<int16_t>(i16_val * 2); })
                .setCausality(fmu4cpp::causality_t::OUTPUT);
        register_uint16("u16_out", [this] { return static_cast<uint16_t>(u16_val * 2); })
                .setCausality(fmu4cpp::causality_t::OUTPUT);
        register_uint32("u32_out", [this] { return u32_val + 100U; })
                .setCausality(fmu4cpp::causality_t::OUTPUT);
        register_int64("i64_out", [this] { return i64_val + 1000LL; })
                .setCausality(fmu4cpp::causality_t::OUTPUT);
        register_uint64("u64_out", [this] { return u64_val + 10000ULL; })
                .setCausality(fmu4cpp::causality_t::OUTPUT);
    }

    bool do_step(double dt) override {
        return true;
    }

    float f32_val = 1.5f;
    int8_t i8_val = -5;
    uint8_t u8_val = 42;
    int16_t i16_val = -300;
    uint16_t u16_val = 12345;
    uint32_t u32_val = 3000000000U;
    int64_t i64_val = -5000000000000LL;
    uint64_t u64_val = 10000000000000000000ULL;
};

fmu4cpp::model_info fmu4cpp::get_model_info() {
    model_info info;
    info.modelName = "FixedWidthTest";
    return info;
}

std::string fmu4cpp::model_identifier() {
    return "fixed_width_test";
}

FMU4CPP_INSTANTIATE(FixedWidthModel);

namespace {
    void dummyLogger(fmi3InstanceEnvironment, fmi3Status status, fmi3String /*category*/, fmi3String message) {
        if (status >= fmi3Warning) {
            std::cout << status << ": " << message << std::endl;
        }
    }
}// namespace

TEST_CASE("fmi3_fixed_width_types_c_abi") {
    FixedWidthModel model({});
    const auto guid = model.guid();

    auto c = fmi3InstantiateCoSimulation(
            fmu4cpp::model_identifier().c_str(),
            guid.c_str(),
            "",
            false,
            true,
            false,
            false,
            nullptr,
            0,
            nullptr,
            dummyLogger,
            nullptr);
    REQUIRE(c);

    REQUIRE(fmi3EnterInitializationMode(c, false, 0, 0, false, 0) == fmi3OK);
    REQUIRE(fmi3ExitInitializationMode(c) == fmi3OK);

    // Retrieve value references
    auto f32_in_var = model.get_float32_variable("f32_in");
    auto f32_out_var = model.get_float32_variable("f32_out");
    auto i8_in_var = model.get_int8_variable("i8_in");
    auto i8_out_var = model.get_int8_variable("i8_out");
    auto u8_in_var = model.get_uint8_variable("u8_in");
    auto u8_out_var = model.get_uint8_variable("u8_out");
    auto i16_in_var = model.get_int16_variable("i16_in");
    auto i16_out_var = model.get_int16_variable("i16_out");
    auto u16_in_var = model.get_uint16_variable("u16_in");
    auto u16_out_var = model.get_uint16_variable("u16_out");
    auto u32_in_var = model.get_uint32_variable("u32_in");
    auto u32_out_var = model.get_uint32_variable("u32_out");
    auto i64_in_var = model.get_int64_variable("i64_in");
    auto i64_out_var = model.get_int64_variable("i64_out");
    auto u64_in_var = model.get_uint64_variable("u64_in");
    auto u64_out_var = model.get_uint64_variable("u64_out");

    REQUIRE(f32_in_var.has_value());
    REQUIRE(f32_out_var.has_value());
    REQUIRE(i8_in_var.has_value());
    REQUIRE(i8_out_var.has_value());
    REQUIRE(u8_in_var.has_value());
    REQUIRE(u8_out_var.has_value());
    REQUIRE(i16_in_var.has_value());
    REQUIRE(i16_out_var.has_value());
    REQUIRE(u16_in_var.has_value());
    REQUIRE(u16_out_var.has_value());
    REQUIRE(u32_in_var.has_value());
    REQUIRE(u32_out_var.has_value());
    REQUIRE(i64_in_var.has_value());
    REQUIRE(i64_out_var.has_value());
    REQUIRE(u64_in_var.has_value());
    REQUIRE(u64_out_var.has_value());

    // Test Float32
    {
        fmi3ValueReference vr = f32_in_var->value_reference();
        fmi3Float32 val = 12.5f;
        REQUIRE(fmi3SetFloat32(c, &vr, 1, &val, 1) == fmi3OK);

        fmi3Float32 got = 0;
        REQUIRE(fmi3GetFloat32(c, &vr, 1, &got, 1) == fmi3OK);
        CHECK(got == Catch::Approx(12.5f));

        fmi3ValueReference out_vr = f32_out_var->value_reference();
        fmi3Float32 out_got = 0;
        REQUIRE(fmi3GetFloat32(c, &out_vr, 1, &out_got, 1) == fmi3OK);
        CHECK(out_got == Catch::Approx(25.0f));
    }

    // Test Int8
    {
        fmi3ValueReference vr = i8_in_var->value_reference();
        fmi3Int8 val = -7;
        REQUIRE(fmi3SetInt8(c, &vr, 1, &val, 1) == fmi3OK);

        fmi3Int8 got = 0;
        REQUIRE(fmi3GetInt8(c, &vr, 1, &got, 1) == fmi3OK);
        CHECK(got == -7);

        fmi3ValueReference out_vr = i8_out_var->value_reference();
        fmi3Int8 out_got = 0;
        REQUIRE(fmi3GetInt8(c, &out_vr, 1, &out_got, 1) == fmi3OK);
        CHECK(out_got == -6);
    }

    // Test UInt8
    {
        fmi3ValueReference vr = u8_in_var->value_reference();
        fmi3UInt8 val = 150;
        REQUIRE(fmi3SetUInt8(c, &vr, 1, &val, 1) == fmi3OK);

        fmi3UInt8 got = 0;
        REQUIRE(fmi3GetUInt8(c, &vr, 1, &got, 1) == fmi3OK);
        CHECK(got == 150);

        fmi3ValueReference out_vr = u8_out_var->value_reference();
        fmi3UInt8 out_got = 0;
        REQUIRE(fmi3GetUInt8(c, &out_vr, 1, &out_got, 1) == fmi3OK);
        CHECK(out_got == 152);
    }

    // Test Int16
    {
        fmi3ValueReference vr = i16_in_var->value_reference();
        fmi3Int16 val = -555;
        REQUIRE(fmi3SetInt16(c, &vr, 1, &val, 1) == fmi3OK);

        fmi3Int16 got = 0;
        REQUIRE(fmi3GetInt16(c, &vr, 1, &got, 1) == fmi3OK);
        CHECK(got == -555);

        fmi3ValueReference out_vr = i16_out_var->value_reference();
        fmi3Int16 out_got = 0;
        REQUIRE(fmi3GetInt16(c, &out_vr, 1, &out_got, 1) == fmi3OK);
        CHECK(out_got == -1110);
    }

    // Test UInt16
    {
        fmi3ValueReference vr = u16_in_var->value_reference();
        fmi3UInt16 val = 30000;
        REQUIRE(fmi3SetUInt16(c, &vr, 1, &val, 1) == fmi3OK);

        fmi3UInt16 got = 0;
        REQUIRE(fmi3GetUInt16(c, &vr, 1, &got, 1) == fmi3OK);
        CHECK(got == 30000);

        fmi3ValueReference out_vr = u16_out_var->value_reference();
        fmi3UInt16 out_got = 0;
        REQUIRE(fmi3GetUInt16(c, &out_vr, 1, &out_got, 1) == fmi3OK);
        CHECK(out_got == 60000);
    }

    // Test UInt32
    {
        fmi3ValueReference vr = u32_in_var->value_reference();
        fmi3UInt32 val = 3500000000U;
        REQUIRE(fmi3SetUInt32(c, &vr, 1, &val, 1) == fmi3OK);

        fmi3UInt32 got = 0;
        REQUIRE(fmi3GetUInt32(c, &vr, 1, &got, 1) == fmi3OK);
        CHECK(got == 3500000000U);

        fmi3ValueReference out_vr = u32_out_var->value_reference();
        fmi3UInt32 out_got = 0;
        REQUIRE(fmi3GetUInt32(c, &out_vr, 1, &out_got, 1) == fmi3OK);
        CHECK(out_got == 3500000100U);
    }

    // Test Int64
    {
        fmi3ValueReference vr = i64_in_var->value_reference();
        fmi3Int64 val = -8000000000000LL;
        REQUIRE(fmi3SetInt64(c, &vr, 1, &val, 1) == fmi3OK);

        fmi3Int64 got = 0;
        REQUIRE(fmi3GetInt64(c, &vr, 1, &got, 1) == fmi3OK);
        CHECK(got == -8000000000000LL);

        fmi3ValueReference out_vr = i64_out_var->value_reference();
        fmi3Int64 out_got = 0;
        REQUIRE(fmi3GetInt64(c, &out_vr, 1, &out_got, 1) == fmi3OK);
        CHECK(out_got == -7999999999000LL);
    }

    // Test UInt64
    {
        fmi3ValueReference vr = u64_in_var->value_reference();
        fmi3UInt64 val = 15000000000000000000ULL;
        REQUIRE(fmi3SetUInt64(c, &vr, 1, &val, 1) == fmi3OK);

        fmi3UInt64 got = 0;
        REQUIRE(fmi3GetUInt64(c, &vr, 1, &got, 1) == fmi3OK);
        CHECK(got == 15000000000000000000ULL);

        fmi3ValueReference out_vr = u64_out_var->value_reference();
        fmi3UInt64 out_got = 0;
        REQUIRE(fmi3GetUInt64(c, &out_vr, 1, &out_got, 1) == fmi3OK);
        CHECK(out_got == 15000000000000010000ULL);
    }

    fmi3Terminate(c);
    fmi3FreeInstance(c);
}

TEST_CASE("fmi3_fixed_width_xml_description") {
    FixedWidthModel model({});
    const std::string xml = model.make_description();

    CHECK(xml.find("<Float32 name=\"f32_in\"") != std::string::npos);
    CHECK(xml.find("start=\"1.5\"") != std::string::npos);
    CHECK(xml.find("unit=\"m/s\"") != std::string::npos);

    CHECK(xml.find("<Int8 name=\"i8_in\"") != std::string::npos);
    CHECK(xml.find("start=\"-5\"") != std::string::npos);

    CHECK(xml.find("<UInt8 name=\"u8_in\"") != std::string::npos);
    CHECK(xml.find("start=\"42\"") != std::string::npos);

    CHECK(xml.find("<Int16 name=\"i16_in\"") != std::string::npos);
    CHECK(xml.find("start=\"-300\"") != std::string::npos);

    CHECK(xml.find("<UInt16 name=\"u16_in\"") != std::string::npos);
    CHECK(xml.find("start=\"12345\"") != std::string::npos);

    CHECK(xml.find("<UInt32 name=\"u32_in\"") != std::string::npos);
    CHECK(xml.find("start=\"3000000000\"") != std::string::npos);

    CHECK(xml.find("<Int64 name=\"i64_in\"") != std::string::npos);
    CHECK(xml.find("start=\"-5000000000000\"") != std::string::npos);

    CHECK(xml.find("<UInt64 name=\"u64_in\"") != std::string::npos);
    CHECK(xml.find("start=\"10000000000000000000\"") != std::string::npos);


}
