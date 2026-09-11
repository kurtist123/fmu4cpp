
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

        Model::reset();
    }

    bool do_step(double dt) override {

        nlohmann::json j = nlohmann::json::from_msgpack(binaryIn_);
        realOut_ = j["real"];
        integerOut_ = j["integer"];
        stringOut_ = j["string"];

        return true;
    }

    void reset() override {
        binaryIn_ = {};
        realOut_ = 0;
        integerOut_ = 0;
        stringOut_ = "empty";
    }

private:
    std::vector<uint8_t> binaryIn_;

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

    REQUIRE(fmi3Terminate(c) == fmi3OK);

    fmi3FreeInstance(c);
}

TEST_CASE("test_model_description_conformance") {

    class DescriptionModel : public fmu4cpp::fmu_base {
    public:
        DescriptionModel() : fmu_base({}) {
            register_integer("intWithMin", &iVal_).setMin(-10);
            register_integer("intWithMax", &iVal_).setMax(100);
            register_real("realWithUnitAndMin", &rVal_).setUnit("m/s").setMin(0.0);
            register_string("strSpecial", &sVal_).setCausality(fmu4cpp::causality_t::INPUT);
            register_binary("binOut", &binVal_)
                    .setCausality(fmu4cpp::causality_t::OUTPUT)
                    .setInitial(fmu4cpp::initial_t::CALCULATED);

            auto &annotatedVar = register_real("annotatedReal", &rVal_);
            annotatedVar.addAnnotation("<Annotation type=\"org.example.display\"><Color>blue</Color></Annotation>");
        }

        bool do_step(double) override { return true; }

    private:
        int iVal_ = 0;
        double rVal_ = 0.0;
        std::string sVal_ = "test & <test> \"quoted\" 'apostrophe'";
        std::vector<uint8_t> binVal_ = {0xAA, 0xBB};
    };

    DescriptionModel dm;
    const std::string xml = dm.make_description();

    // Verify independent min / max
    CHECK(xml.find("min=\"-10\"") != std::string::npos);
    CHECK(xml.find("max=\"100\"") != std::string::npos);

    // Verify Real unit metadata
    CHECK(xml.find("unit=\"m/s\"") != std::string::npos);

    // Verify XML escaping of special characters
    CHECK(xml.find("&amp;") != std::string::npos);
    CHECK(xml.find("&lt;test&gt;") != std::string::npos);
    CHECK(xml.find("&quot;quoted&quot;") != std::string::npos);
    CHECK(xml.find("&apos;apostrophe&apos;") != std::string::npos);

    // Verify scalar String has no Dimension element
    CHECK(xml.find("<Dimension") == std::string::npos);

    // Verify binary output is present in ModelStructure Output and InitialUnknown
    const auto binVr = dm.get_binary_variable("binOut")->value_reference();
    std::string expectedOutput = "<Output valueReference=\"" + std::to_string(binVr) + "\"";
    std::string expectedInitUnknown = "<InitialUnknown valueReference=\"" + std::to_string(binVr) + "\"";
    CHECK(xml.find(expectedOutput) != std::string::npos);
    CHECK(xml.find(expectedInitUnknown) != std::string::npos);

    // Verify annotations
    CHECK(xml.find("<Annotations>") != std::string::npos);
    CHECK(xml.find("org.example.display") != std::string::npos);
}

