#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <fmu4cpp/fmu_base.hpp>
#include <fmu4cpp/fmu_variable.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace {

class TestModel : public fmu4cpp::fmu_base {
public:
    bool enter_event_called{false};
    bool exit_event_called{false};
    bool update_discrete_called{false};

    int intVal_{42};
    double realVal_{3.14};
    bool boolVal_{true};
    std::string str1_{"first_string_value"};
    std::string str2_{"second_string_value_which_is_longer_to_force_allocation"};
    fmu4cpp::BinaryType bin1_{0x01, 0x02, 0x03, 0x04};
    fmu4cpp::BinaryType bin2_{0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    fmu4cpp::BinaryType binEmpty_{};

    FMU4CPP_CTOR(TestModel) {
        register_integer("intVar", &intVal_).setCausality(fmu4cpp::causality_t::INPUT);
        register_real("realVar", &realVal_)
            .setCausality(fmu4cpp::causality_t::OUTPUT)
            .setAssociatedClock(11);
        register_boolean("boolVar", &boolVal_).setCausality(fmu4cpp::causality_t::LOCAL);
        register_string("strVar1", &str1_).setCausality(fmu4cpp::causality_t::OUTPUT);
        register_string("strVar2", &str2_).setCausality(fmu4cpp::causality_t::OUTPUT);
        register_binary("binVar1", &bin1_)
            .setCausality(fmu4cpp::causality_t::OUTPUT)
            .setAssociatedClock(11)
            .setMimeType("application/octet-stream")
            .setMaxSize(64);
        register_binary("binVar2", &bin2_).setCausality(fmu4cpp::causality_t::OUTPUT);
        register_binary("binEmpty", &binEmpty_).setCausality(fmu4cpp::causality_t::OUTPUT);

        // Register triggered clocks
        auto &inClock = register_clock("inClock", 10, fmu4cpp::causality_t::INPUT);
        inClock.setDescription("Input triggered clock");

        auto &outClock = register_clock("outClock", 11, fmu4cpp::causality_t::OUTPUT);
        outClock.setDescription("Output triggered clock");
        outClock.setDependencies({"intVar"});
    }

    void on_enter_event_mode() noexcept override {
        enter_event_called = true;
    }

    void on_exit_event_mode() noexcept override {
        exit_event_called = true;
    }

    void update_discrete_states(bool &discreteStatesNeedUpdate, bool &terminateSimulation) override {
        update_discrete_called = true;
        discreteStatesNeedUpdate = false;
        terminateSimulation = false;
    }

    bool do_step(double) override {
        return true;
    }

    // Expose registration methods for testing dynamic errors
    using fmu4cpp::fmu_base::register_clock;
};

} // namespace

fmu4cpp::model_info fmu4cpp::get_model_info() {
    fmu4cpp::model_info info;
    info.modelName = "clock_and_event_test_model";
    info.hasEventMode = true;
    return info;
}

std::string fmu4cpp::model_identifier() {
    return "clock_and_event";
}

FMU4CPP_INSTANTIATE(TestModel);

TEST_CASE("clock_registration_and_properties") {
    TestModel model({});

    auto inClockOpt = model.get_clock_variable("inClock");
    REQUIRE(inClockOpt.has_value());
    const auto &inClock = *inClockOpt;
    CHECK(inClock.name() == "inClock");
    CHECK(inClock.value_reference() == 10);
    CHECK(inClock.causality() == fmu4cpp::causality_t::INPUT);
    CHECK(inClock.variability() == fmu4cpp::variability_t::DISCRETE);
    CHECK(inClock.interval() == fmu4cpp::interval_t::TRIGGERED);
    CHECK(inClock.can_be_deactivated() == false);
    CHECK(inClock.canBeDeactivated() == false);
    CHECK(inClock.getDescription() == "Input triggered clock");
    CHECK(inClock.get() == false);

    auto outClockOpt = model.get_clock_variable("outClock");
    REQUIRE(outClockOpt.has_value());
    const auto &outClock = *outClockOpt;
    CHECK(outClock.name() == "outClock");
    CHECK(outClock.value_reference() == 11);
    CHECK(outClock.causality() == fmu4cpp::causality_t::OUTPUT);
    CHECK(outClock.variability() == fmu4cpp::variability_t::DISCRETE);
    CHECK(outClock.interval() == fmu4cpp::interval_t::TRIGGERED);
    CHECK(outClock.can_be_deactivated() == false);
    CHECK(outClock.getDescription() == "Output triggered clock");
    CHECK(outClock.get() == false);
    CHECK(outClock.getDependencies() == std::vector<std::string>{"intVar"});

    // Stringification checks
    CHECK(fmu4cpp::to_string(fmu4cpp::interval_t::TRIGGERED) == "triggered");
    CHECK(fmu4cpp::to_string(fmu4cpp::causality_t::INPUT) == "input");
    CHECK(fmu4cpp::to_string(fmu4cpp::causality_t::OUTPUT) == "output");

    // Invalid causality rejection
    CHECK_THROWS_AS(model.register_clock("badClock1", 20, fmu4cpp::causality_t::PARAMETER), std::invalid_argument);
    CHECK_THROWS_AS(model.register_clock("badClock2", 21, fmu4cpp::causality_t::LOCAL), std::invalid_argument);

    // Duplicate VR rejection
    CHECK_THROWS_AS(model.register_clock("dupClock1", 10, fmu4cpp::causality_t::INPUT), std::invalid_argument);
    CHECK_THROWS_AS(model.register_clock("dupClock2", 11, fmu4cpp::causality_t::OUTPUT), std::invalid_argument);

    // Input clock dependencies rejection
    fmu4cpp::ClockVariable testInputClock("testIn", 50, 1, fmu4cpp::causality_t::INPUT);
    CHECK_THROWS_AS(testInputClock.setDependencies({"foo"}), std::logic_error);
    CHECK_THROWS_AS(testInputClock.getDependencies(), std::logic_error);
}

TEST_CASE("clock_mutation_and_getters") {
    TestModel model({});

    CHECK(model.get_clock(10) == false);
    model.set_clock(10, true);
    CHECK(model.get_clock(10) == true);
    model.set_clock(10, false);
    CHECK(model.get_clock(10) == false);

    CHECK_THROWS_AS(model.get_clock(999), std::out_of_range);
    CHECK_THROWS_AS(model.set_clock(999, true), std::out_of_range);
}

TEST_CASE("pending_output_event_signaling") {
    TestModel model({});

    CHECK(model.has_pending_output_events() == false);

    // Notify output clock
    model.notify_output_event(11);
    CHECK(model.has_pending_output_events() == true);
    CHECK(model.get_clock(11) == true);

    // Attempting notify on input clock must throw logic_error
    CHECK_THROWS_AS(model.notify_output_event(10), std::logic_error);

    // Attempting notify on unknown clock must throw invalid_argument
    CHECK_THROWS_AS(model.notify_output_event(999), std::invalid_argument);

    // Clear triggered clocks
    model.clear_triggered_clocks();
    CHECK(model.has_pending_output_events() == false);
    CHECK(model.get_clock(11) == false);
}

TEST_CASE("associated_clock_support") {
    TestModel model({});

    auto realVar = model.get_real_variable("realVar");
    REQUIRE(realVar.has_value());
    CHECK(realVar->associated_clock() == std::optional<fmu4cpp::value_ref>{11});
    CHECK(realVar->getAssociatedClock() == std::optional<fmu4cpp::value_ref>{11});

    auto binVar1 = model.get_binary_variable("binVar1");
    REQUIRE(binVar1.has_value());
    CHECK(binVar1->associated_clock() == std::optional<fmu4cpp::value_ref>{11});
    CHECK(binVar1->mime_type() == "application/octet-stream");
    CHECK(binVar1->max_size() == std::optional<size_t>{64});

    auto intVar = model.get_int_variable("intVar");
    REQUIRE(intVar.has_value());
    CHECK(intVar->associated_clock() == std::nullopt);
}

TEST_CASE("event_mode_lifecycle_hooks") {
    TestModel model({});

    CHECK(model.enter_event_called == false);
    model.on_enter_event_mode();
    CHECK(model.enter_event_called == true);

    CHECK(model.update_discrete_called == false);
    bool discreteStatesNeedUpdate = true;
    bool terminateSimulation = true;
    model.update_discrete_states(discreteStatesNeedUpdate, terminateSimulation);
    CHECK(model.update_discrete_called == true);
    CHECK(discreteStatesNeedUpdate == false);
    CHECK(terminateSimulation == false);

    CHECK(model.exit_event_called == false);
    model.on_exit_event_mode();
    CHECK(model.exit_event_called == true);
}

TEST_CASE("batch_buffer_pointer_stability") {
    TestModel model({});

    auto str1 = model.get_string_variable("strVar1");
    auto str2 = model.get_string_variable("strVar2");
    REQUIRE(str1.has_value());
    REQUIRE(str2.has_value());

    const unsigned int strVrs[] = {str1->value_reference(), str2->value_reference()};
    const char *strVals[2] = {nullptr, nullptr};

    model.get_string(strVrs, 2, strVals);
    REQUIRE(strVals[0] != nullptr);
    REQUIRE(strVals[1] != nullptr);
    CHECK(std::string(strVals[0]) == "first_string_value");
    CHECK(std::string(strVals[1]) == "second_string_value_which_is_longer_to_force_allocation");

    const char *savedPtr0 = strVals[0];
    const char *savedPtr1 = strVals[1];

    // Second call for str1 only
    const char *newVal[1] = {nullptr};
    model.get_string(&strVrs[0], 1, newVal);
    CHECK(newVal[0] == savedPtr0);
    // savedPtr1 must still point to valid content
    CHECK(std::string(savedPtr1) == "second_string_value_which_is_longer_to_force_allocation");

    // Binary batch retrieval
    auto bin1 = model.get_binary_variable("binVar1");
    auto bin2 = model.get_binary_variable("binVar2");
    auto binEmpty = model.get_binary_variable("binEmpty");
    REQUIRE(bin1.has_value());
    REQUIRE(bin2.has_value());
    REQUIRE(binEmpty.has_value());

    const unsigned int binVrs[] = {bin1->value_reference(), bin2->value_reference(), binEmpty->value_reference()};
    size_t binSizes[3] = {0, 0, 0};
    const uint8_t *binVals[3] = {nullptr, nullptr, nullptr};

    model.get_binary(binVrs, 3, binSizes, binVals);
    CHECK(binSizes[0] == 4);
    CHECK(binSizes[1] == 5);
    CHECK(binSizes[2] == 0);
    CHECK(binVals[2] == nullptr);

    CHECK(std::vector<uint8_t>(binVals[0], binVals[0] + binSizes[0]) == fmu4cpp::BinaryType{0x01, 0x02, 0x03, 0x04});
    CHECK(std::vector<uint8_t>(binVals[1], binVals[1] + binSizes[1]) == fmu4cpp::BinaryType{0xAA, 0xBB, 0xCC, 0xDD, 0xEE});

    const uint8_t *savedBin0 = binVals[0];
    const uint8_t *savedBin1 = binVals[1];

    // Second call for bin1 only
    size_t newBinSize[1] = {0};
    const uint8_t *newBinVal[1] = {nullptr};
    model.get_binary(&binVrs[0], 1, newBinSize, newBinVal);
    CHECK(newBinVal[0] == savedBin0);
    // savedBin1 must still point to valid content
    CHECK(std::vector<uint8_t>(savedBin1, savedBin1 + 5) == fmu4cpp::BinaryType{0xAA, 0xBB, 0xCC, 0xDD, 0xEE});
}

TEST_CASE("variable_enumeration") {
    TestModel model({});

    auto vrs = model.get_value_refs();

    // Check that clocks and binary variables are enumerated in get_value_refs()
    CHECK(std::find(vrs.begin(), vrs.end(), 10) != vrs.end());
    CHECK(std::find(vrs.begin(), vrs.end(), 11) != vrs.end());

    auto bin1 = model.get_binary_variable("binVar1");
    REQUIRE(bin1.has_value());
    CHECK(std::find(vrs.begin(), vrs.end(), bin1->value_reference()) != vrs.end());

    // GUID generation must not throw and must be non-empty
    std::string guid = model.guid();
    CHECK(!guid.empty());
}
