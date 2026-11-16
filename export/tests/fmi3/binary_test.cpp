
#include "fmi3/fmi3Functions.h"

#include <nlohmann/json.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

#include <cstdarg>
#include <iostream>

#include <fmu4cpp/fmu_base.hpp>

class Model : public fmu4cpp::fmu_base {

public:
    FMU4CPP_CTOR(Model) {

        register_binary("binaryIn", &binaryIn_).setCausality(fmu4cpp::causality_t::INPUT);
        register_real("realOut", &realOut_).setCausality(fmu4cpp::causality_t::OUTPUT);
        register_integer("integerOut", &integerOut_).setCausality(fmu4cpp::causality_t::OUTPUT);
        register_string("stringOut", &stringOut_).setCausality(fmu4cpp::causality_t::OUTPUT);
        register_binary("boundedBinary", &boundedBinary_).setCausality(fmu4cpp::causality_t::INPUT).setMaxSize(8);
        register_binary("rawBinaryOut", &rawBinaryOut_).setCausality(fmu4cpp::causality_t::OUTPUT);

        Model::reset();
    }

    bool do_step(double dt) override {

        if (!binaryIn_.empty()) {
            try {
                nlohmann::json j = nlohmann::json::from_msgpack(binaryIn_);
                if (j.contains("real")) realOut_ = j["real"];
                if (j.contains("integer")) integerOut_ = j["integer"];
                if (j.contains("string")) stringOut_ = j["string"];
            } catch (...) {}
        }
        rawBinaryOut_ = boundedBinary_;

        return true;
    }

    void reset() override {
        binaryIn_ = {};
        boundedBinary_ = {};
        rawBinaryOut_ = {};
        realOut_ = 0;
        integerOut_ = 0;
        stringOut_ = "empty";
    }

private:
    std::vector<uint8_t> binaryIn_;
    std::vector<uint8_t> boundedBinary_;
    std::vector<uint8_t> rawBinaryOut_;

    double realOut_;
    int integerOut_;
    std::string stringOut_;
};

fmu4cpp::model_info fmu4cpp::get_model_info() {
    return {};
}

std::string fmu4cpp::model_identifier() {
    return "binary";
}

FMU4CPP_INSTANTIATE(Model);


void fmilogger(fmi3InstanceEnvironment, fmi3Status status, fmi3String /*category*/, fmi3String message) {

    std::cout << status << ": " << message << std::endl;
}


TEST_CASE("test_binary") {

    Model model({});
    const auto guid = model.guid();

    auto c = fmi3InstantiateCoSimulation(fmu4cpp::model_identifier().c_str(), guid.c_str(), "", false, true, false, false, nullptr, 0, nullptr, fmilogger, nullptr);
    REQUIRE(c);

    REQUIRE(fmi3EnterInitializationMode(c, false, 0, 0, false, 0) == fmi3OK);
    REQUIRE(fmi3ExitInitializationMode(c) == fmi3OK);

    double realIn = 3.9;
    int integerIn = 42;
    std::string stringIn = "hello";

    nlohmann::json j;
    j["real"] = realIn;
    j["integer"] = integerIn;
    j["string"] = stringIn;

    const auto data = nlohmann::json::to_msgpack(j);
    const uint8_t *binaryPtr = data.data();

    fmi3ValueReference inVr = 1;
    size_t valueSize = data.size();

    fmi3SetBinary(
            c,
            &inVr,     // array of value references
            1,         // nValueReferences
            &valueSize,// array of sizes
            &binaryPtr,// array of binary pointers
            1          // nValues
    );


    bool eventhandlingNeeded;
    bool terminateSimulation;
    bool earlyReturn;
    double lastSucessfulTime;
    REQUIRE(fmi3DoStep(c, 0, 0.1, true, &eventhandlingNeeded, &terminateSimulation, &earlyReturn, &lastSucessfulTime) == fmi3OK);


    double realOut;
    fmi3ValueReference realVr = 2;
    CHECK(fmi3GetFloat64(c, &realVr, 1, &realOut, 1) == fmi3OK);

    int integerOut;
    fmi3ValueReference integerVr = 3;
    CHECK(fmi3GetInt32(c, &integerVr, 1, &integerOut, 1) == fmi3OK);

    fmi3String stringOut;
    fmi3ValueReference stringVr = 4;
    CHECK(fmi3GetString(c, &stringVr, 1, &stringOut, 1) == fmi3OK);

    CHECK(realOut == Catch::Approx(realIn));
    CHECK(integerOut == integerIn);
    CHECK(stringOut == stringIn);

    // Test 1: Bounded binary variable (maxSize = 8)
    fmi3ValueReference boundedVr = 5;
    const uint8_t validPayload[4] = {0x01, 0x02, 0x03, 0x04};
    const uint8_t *validPtr = validPayload;
    size_t validSize = sizeof(validPayload);
    REQUIRE(fmi3SetBinary(c, &boundedVr, 1, &validSize, &validPtr, 1) == fmi3OK);

    // Exceeding maxSize: 10 bytes > 8 bytes -> must fail with fmi3Error
    const uint8_t oversizePayload[10] = {0};
    const uint8_t *oversizePtr = oversizePayload;
    size_t oversizeSize = sizeof(oversizePayload);
    CHECK(fmi3SetBinary(c, &boundedVr, 1, &oversizeSize, &oversizePtr, 1) == fmi3Error);

    // Test 2: Binary with embedded null bytes (0x00)
    const uint8_t nullPayload[] = {0xAA, 0x00, 0xBB, 0x00, 0xCC};
    const uint8_t *nullPtr = nullPayload;
    size_t nullSize = sizeof(nullPayload);
    REQUIRE(fmi3SetBinary(c, &boundedVr, 1, &nullSize, &nullPtr, 1) == fmi3OK);

    REQUIRE(fmi3DoStep(c, 0.1, 0.1, true, &eventhandlingNeeded, &terminateSimulation, &earlyReturn, &lastSucessfulTime) == fmi3OK);

    // Read back from rawBinaryOut (vr = 6)
    fmi3ValueReference rawOutVr = 6;
    size_t readSize = 0;
    fmi3Binary readPtr = nullptr;
    REQUIRE(fmi3GetBinary(c, &rawOutVr, 1, &readSize, &readPtr, 1) == fmi3OK);
    REQUIRE(readSize == sizeof(nullPayload));
    REQUIRE(readPtr != nullptr);
    for (size_t i = 0; i < sizeof(nullPayload); i++) {
        CHECK(static_cast<const uint8_t *>(readPtr)[i] == nullPayload[i]);
    }

    // Test 3: Empty binary payload (size == 0)
    size_t emptySize = 0;
    const uint8_t *emptyPtr = nullptr;
    REQUIRE(fmi3SetBinary(c, &boundedVr, 1, &emptySize, &emptyPtr, 1) == fmi3OK);

    REQUIRE(fmi3GetBinary(c, &boundedVr, 1, &readSize, &readPtr, 1) == fmi3OK);
    CHECK(readSize == 0);

    // Test 4: Null pointer defensive checks
    CHECK(fmi3SetBinary(c, nullptr, 1, &validSize, &validPtr, 1) == fmi3Error);
    CHECK(fmi3SetBinary(c, &boundedVr, 1, nullptr, &validPtr, 1) == fmi3Error);
    CHECK(fmi3SetBinary(c, &boundedVr, 1, &validSize, nullptr, 1) == fmi3Error);
    // When size > 0 but value pointer is null
    const uint8_t *nullValPtr = nullptr;
    CHECK(fmi3SetBinary(c, &boundedVr, 1, &validSize, &nullValPtr, 1) == fmi3Error);

    CHECK(fmi3GetBinary(c, nullptr, 1, &readSize, &readPtr, 1) == fmi3Error);
    CHECK(fmi3GetBinary(c, &boundedVr, 1, nullptr, &readPtr, 1) == fmi3Error);
    CHECK(fmi3GetBinary(c, &boundedVr, 1, &readSize, nullptr, 1) == fmi3Error);

    REQUIRE(fmi3Terminate(c) == fmi3OK);

    fmi3FreeInstance(c);
}
