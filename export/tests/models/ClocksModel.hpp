#ifndef FMU4CPP_CLOCKS_MODEL_HPP
#define FMU4CPP_CLOCKS_MODEL_HPP

#include <fmu4cpp/fmu_base.hpp>

using namespace fmu4cpp;

class ClocksModel : public fmu_base {
public:
    struct State {
        double clockedVar{42.0};
        int clock_activated_count{0};
        int clock_deactivated_count{0};
    };

    FMU4CPP_CTOR(ClocksModel) {
        register_clock("clockIn", &clockIn_).setCausality(causality_t::INPUT);
        register_clock("clockOut", &clockOut_).setCausality(causality_t::OUTPUT);
        register_clock("clockPeriodic", &clockPeriodic_)
                .setIntervalVariability(interval_variability_t::TUNABLE)
                .setShiftDecimal(0.05);
        register_real("clockedVar", &state_.clockedVar)
                .setCausality(causality_t::INPUT)
                .setVariability(variability_t::DISCRETE)
                .setClocks({1});
        register_state(&ClocksModel::state_);
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
        state_.clockedVar = 42.0;
        state_.clock_activated_count = 0;
        state_.clock_deactivated_count = 0;
    }

    void on_clock_activated(unsigned int vr) override {
        if (vr == 1) {
            state_.clock_activated_count++;
            // When clockIn activates, trigger clockOut as an event response
            clockOut_ = true;
        }
    }

    void on_clock_deactivated(unsigned int vr) override {
        if (vr == 1) {
            state_.clock_deactivated_count++;
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
    State state_{};
};

model_info fmu4cpp::get_model_info() {
    model_info info;
    info.modelName = "ClocksModel";
    info.description = "A model demonstrating FMI 3 clocks and event mode";
    info.defaultExperiment = {0.0, 10.0};
    info.hasEventMode = true;
    info.canGetAndSetFMUstate = true;
    info.canSerializeFMUstate = true;
    return info;
}

FMU4CPP_INSTANTIATE(ClocksModel);

#endif// FMU4CPP_CLOCKS_MODEL_HPP
