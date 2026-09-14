#ifndef FMU4CPP_CLOCKS_MODEL_HPP
#define FMU4CPP_CLOCKS_MODEL_HPP

#include <fmu4cpp/fmu_base.hpp>

using namespace fmu4cpp;

class ClocksModel : public fmu_base {
public:
    FMU4CPP_CTOR(ClocksModel) {
        register_clock("clockIn", &clockIn_).setCausality(causality_t::INPUT);
        register_clock("clockOut", &clockOut_).setCausality(causality_t::OUTPUT);
        register_clock("clockPeriodic", &clockPeriodic_)
                .setIntervalVariability(interval_variability_t::TUNABLE)
                .setShiftDecimal(0.05);
        register_real("clockedVar", &clockedVar_)
                .setCausality(causality_t::INPUT)
                .setVariability(variability_t::DISCRETE)
                .setClocks({1});

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

    void update_discrete_states(discrete_states_info &info) override {
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

model_info fmu4cpp::get_model_info() {
    model_info info;
    info.modelName = "ClocksModel";
    info.description = "A model demonstrating FMI 3 clocks and event mode";
    info.defaultExperiment = {0.0, 10.0};
    info.hasEventMode = true;
    return info;
}

FMU4CPP_INSTANTIATE(ClocksModel);

#endif// FMU4CPP_CLOCKS_MODEL_HPP
