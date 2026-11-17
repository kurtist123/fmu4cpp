#include "fmi2/fmi2Functions.h"

#include <catch2/catch_test_macros.hpp>
#include <fmu4cpp/fmu_base.hpp>
#include <stdexcept>

class ClockModel : public fmu4cpp::fmu_base {
public:
    FMU4CPP_CTOR(ClockModel) {
        register_clock("clk", fmu4cpp::causality_t::INPUT);
    }
    bool do_step(double dt) override { return true; }
};

class BinaryModel : public fmu4cpp::fmu_base {
public:
    FMU4CPP_CTOR(BinaryModel) {
        register_binary("bin", &bin_).setCausality(fmu4cpp::causality_t::INPUT);
    }
    bool do_step(double dt) override { return true; }
private:
    std::vector<uint8_t> bin_{};
};

class Float32Model : public fmu4cpp::fmu_base {
public:
    FMU4CPP_CTOR(Float32Model) {
        register_float32("f32", &f32_).setCausality(fmu4cpp::causality_t::INPUT);
    }
    bool do_step(double dt) override { return true; }
private:
    float f32_{0.0f};
};

class Int8Model : public fmu4cpp::fmu_base {
public:
    FMU4CPP_CTOR(Int8Model) {
        register_int8("i8", &i8_).setCausality(fmu4cpp::causality_t::INPUT);
    }
    bool do_step(double dt) override { return true; }
private:
    int8_t i8_{0};
};

class UInt64Model : public fmu4cpp::fmu_base {
public:
    FMU4CPP_CTOR(UInt64Model) {
        register_uint64("u64", &u64_).setCausality(fmu4cpp::causality_t::INPUT);
    }
    bool do_step(double dt) override { return true; }
private:
    uint64_t u64_{0};
};

class NormalModel : public fmu4cpp::fmu_base {
public:
    FMU4CPP_CTOR(NormalModel) {
        register_real("real_var", &real_).setCausality(fmu4cpp::causality_t::INPUT);
        register_integer("int_var", &int_).setCausality(fmu4cpp::causality_t::OUTPUT);
        register_boolean("bool_var", &bool_).setCausality(fmu4cpp::causality_t::LOCAL);
        register_string("str_var", &str_).setCausality(fmu4cpp::causality_t::PARAMETER);
    }
    bool do_step(double dt) override { return true; }
private:
    double real_{0.0};
    int int_{42};
    bool bool_{true};
    std::string str_{"hello"};
};

fmu4cpp::model_info fmu4cpp::get_model_info() {
    model_info info;
    info.modelName = "RejectionTest";
    return info;
}

std::string fmu4cpp::model_identifier() {
    return "rejection_test";
}

FMU4CPP_INSTANTIATE(NormalModel);

TEST_CASE("fmi2_rejection_tests") {
    SECTION("Reject Clock variable in FMI2") {
        ClockModel model({});
        REQUIRE_THROWS_AS(model.make_description(), std::runtime_error);
    }

    SECTION("Reject Binary variable in FMI2") {
        BinaryModel model({});
        REQUIRE_THROWS_AS(model.make_description(), std::runtime_error);
    }

    SECTION("Reject Float32 variable in FMI2") {
        Float32Model model({});
        REQUIRE_THROWS_AS(model.make_description(), std::runtime_error);
    }

    SECTION("Reject Int8 variable in FMI2") {
        Int8Model model({});
        REQUIRE_THROWS_AS(model.make_description(), std::runtime_error);
    }

    SECTION("Reject UInt64 variable in FMI2") {
        UInt64Model model({});
        REQUIRE_THROWS_AS(model.make_description(), std::runtime_error);
    }

    SECTION("Accept normal variables in FMI2") {
        NormalModel model({});
        std::string xml;
        REQUIRE_NOTHROW(xml = model.make_description());
        CHECK(xml.find("<fmiModelDescription") != std::string::npos);
        CHECK(xml.find("fmiVersion=\"2.0\"") != std::string::npos);
        CHECK(xml.find("<ScalarVariable name=\"real_var\"") != std::string::npos);
        CHECK(xml.find("<ScalarVariable name=\"int_var\"") != std::string::npos);
    }
}
