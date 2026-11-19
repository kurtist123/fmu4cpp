
#include "fmu4cpp/fmu_base.hpp"
#include "fmu4cpp/lib_info.hpp"
#include "fmu4cpp/time.hpp"
#include "fmu4cpp/util.hpp"

#include <algorithm>
#include <sstream>

using namespace fmu4cpp;


namespace {

    std::string type(const VariableBase *v) {
        if (dynamic_cast<const IntVariable *>(v)) {
            return "Int32";
        } else if (dynamic_cast<const RealVariable *>(v)) {
            return "Float64";
        } else if (dynamic_cast<const Float32Variable *>(v)) {
            return "Float32";
        } else if (dynamic_cast<const Int8Variable *>(v)) {
            return "Int8";
        } else if (dynamic_cast<const UInt8Variable *>(v)) {
            return "UInt8";
        } else if (dynamic_cast<const Int16Variable *>(v)) {
            return "Int16";
        } else if (dynamic_cast<const UInt16Variable *>(v)) {
            return "UInt16";
        } else if (dynamic_cast<const UInt32Variable *>(v)) {
            return "UInt32";
        } else if (dynamic_cast<const Int64Variable *>(v)) {
            return "Int64";
        } else if (dynamic_cast<const UInt64Variable *>(v)) {
            return "UInt64";
        } else if (dynamic_cast<const StringVariable *>(v)) {
            return "String";
        } else if (dynamic_cast<const BoolVariable *>(v)) {
            return "Boolean";
        } else if (dynamic_cast<const BinaryVariable *>(v)) {
            return "Binary";
        } else if (dynamic_cast<const ClockVariable *>(v)) {
            return "Clock";
        }
        throw std::runtime_error("Unknown variable type");
    }

    static std::string hex_encode(const std::vector<unsigned char> &in) {
        static const char hex[] = "0123456789ABCDEF";
        std::string out;
        out.reserve(in.size() * 2);
        for (unsigned char c: in) {
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0F]);
        }
        return out;
    }

}// namespace


std::string fmu_base::make_description() const {

    const model_info m = get_model_info();
    std::stringstream ss;
    ss << R"(<?xml version="1.0" encoding="UTF-8"?>)" << "\n"
       << "<fmiModelDescription fmiVersion=\"3.0\"\n"
       << "\tmodelName=\"" << xml_escape(m.modelName) << "\"\n"
       << "\tinstantiationToken=\"" << guid() << "\"\n"
       << "\tgenerationTool=\"fmu4cpp v" << to_string(library_version()) << "\"\n"
       << "\tgenerationDateAndTime=\"" << now() << "\"\n"
       << "\tdescription=\"" << xml_escape(m.description) << "\"\n"
       << "\tauthor=\"" << xml_escape(m.author) << "\"\n";
    if (!m.version.empty()) {
        ss << "\tversion=\"" << xml_escape(m.version) << "\"\n";
    }
    ss << "\tvariableNamingConvention=\"" << xml_escape(m.variableNamingConvention) << "\""
       << ">\n\n";

    const bool hasEventMode = m.hasEventMode || !clocks_.empty();

    ss << std::boolalpha
       << "\t<CoSimulation\n"
       << "\t\tneedsExecutionTool=\"" << m.needsExecutionTool << "\"\n"
       << "\t\tmodelIdentifier=\"" << model_identifier() << "\"\n"
       << "\t\tcanHandleVariableCommunicationStepSize=\"" << m.canHandleVariableCommunicationStepSize << "\"\n"
       << "\t\tcanBeInstantiatedOnlyOncePerProcess=\"" << m.canBeInstantiatedOnlyOncePerProcess << "\"\n"
       << "\t\tcanGetAndSetFMUState=\"" << m.canGetAndSetFMUstate << "\"\n"
       << "\t\tcanSerializeFMUState=\"" << m.canSerializeFMUstate << "\"\n"
       << "\t\thasEventMode=\"" << hasEventMode << "\"\n"
       << "\t\tprovidesDirectionalDerivatives=\"false\"" << "\n"
       << "\t\tprovidesAdjointDerivatives=\"false\"" << "\n"
       << "\t\tprovidesPerElementDependencies=\"false\"" << "\n"
       << "\t\tprovidesEvaluateDiscreteStates=\"false\""
       << "/>\n\n";


    if (m.defaultExperiment) {
        ss << "\t<DefaultExperiment ";

        ss << "startTime=\"" << m.defaultExperiment->startTime << "\"";
        if (m.defaultExperiment->stopTime) ss << " stopTime=\"" << *m.defaultExperiment->stopTime << "\"";
        if (m.defaultExperiment->stepSize) ss << " stepSize=\"" << *m.defaultExperiment->stepSize << "\"";
        if (m.defaultExperiment->tolerance) ss << " tolerance=\"" << *m.defaultExperiment->tolerance << "\"";

        ss << "/>\n\n";
    }

    ss << "\t<ModelVariables>\n";

    const auto allVars = [&] {
        auto allVars = collect_variables();
        std::sort(allVars.begin(), allVars.end(), [](const VariableBase *v1, const VariableBase *v2) {
            return v1->index() < v2->index();
        });
        return allVars;
    }();

    for (const auto &v: allVars) {
        const auto variability = v->variability();
        const auto initial = v->initial();
        const auto annotations = v->getAnnotations();
        const bool isClock = (dynamic_cast<const ClockVariable *>(v) != nullptr);

        ss << "\t\t<!--"
           << "index=" << v->index() << "-->\n"
           << "\t\t<" << type(v) << " name=\""
           << xml_escape(v->name()) << "\" valueReference=\"" << v->value_reference() << "\""
           << " causality=\"" << to_string(v->causality()) << "\"";

        if (isClock) {
            const auto clk = dynamic_cast<const ClockVariable *>(v);
            ss << " intervalVariability=\"triggered\"";
            ss << " canBeDeactivated=\"" << (clk->can_be_deactivated() ? "true" : "false") << "\"";
        } else {
            if (variability) {
                ss << " variability=\"" << to_string(*variability) << "\"";
            }
            if (initial) {
                ss << " initial=\"" << to_string(*initial) << "\"";
            }
            if (v->associated_clock().has_value()) {
                ss << " clocks=\"" << *v->associated_clock() << "\"";
            }
        }

        if (auto desc = v->getDescription(); !desc.empty()) {
            ss << " description=\"" << xml_escape(desc) << "\"";
        }

        bool with_start = requires_start(*v);
        if (auto i = dynamic_cast<const IntVariable *>(v)) {

            if (with_start) {
                ss << " start=\"" << i->get() << "\"";
            }
            const auto min = i->getMin();
            const auto max = i->getMax();
            if (min && max) {
                ss << " min=\"" << *min << "\" max=\"" << *max << "\"";
            }

        } else if (auto r = dynamic_cast<const RealVariable *>(v)) {

            if (with_start) {
                ss << " start=\"" << r->get() << "\"";
            }
            const auto min = r->getMin();
            const auto max = r->getMax();
            if (min && max) {
                ss << " min=\"" << *min << "\" max=\"" << *max << "\"";
            }
            if (auto u = r->getUnit(); u.has_value()) {
                ss << " unit=\"" << xml_escape(*u) << "\"";
            }

        } else if (auto f32 = dynamic_cast<const Float32Variable *>(v)) {

            if (with_start) {
                ss << " start=\"" << f32->get() << "\"";
            }
            if (f32->getMin() && f32->getMax()) {
                ss << " min=\"" << *f32->getMin() << "\" max=\"" << *f32->getMax() << "\"";
            }
            if (auto u = f32->getUnit(); u.has_value()) {
                ss << " unit=\"" << xml_escape(*u) << "\"";
            }

        } else if (auto i8 = dynamic_cast<const Int8Variable *>(v)) {

            if (with_start) {
                ss << " start=\"" << static_cast<int>(i8->get()) << "\"";
            }
            if (i8->getMin() && i8->getMax()) {
                ss << " min=\"" << static_cast<int>(*i8->getMin()) << "\" max=\"" << static_cast<int>(*i8->getMax()) << "\"";
            }

        } else if (auto u8 = dynamic_cast<const UInt8Variable *>(v)) {

            if (with_start) {
                ss << " start=\"" << static_cast<unsigned int>(u8->get()) << "\"";
            }
            if (u8->getMin() && u8->getMax()) {
                ss << " min=\"" << static_cast<unsigned int>(*u8->getMin()) << "\" max=\"" << static_cast<unsigned int>(*u8->getMax()) << "\"";
            }

        } else if (auto i16 = dynamic_cast<const Int16Variable *>(v)) {

            if (with_start) {
                ss << " start=\"" << i16->get() << "\"";
            }
            if (i16->getMin() && i16->getMax()) {
                ss << " min=\"" << *i16->getMin() << "\" max=\"" << *i16->getMax() << "\"";
            }

        } else if (auto u16 = dynamic_cast<const UInt16Variable *>(v)) {

            if (with_start) {
                ss << " start=\"" << u16->get() << "\"";
            }
            if (u16->getMin() && u16->getMax()) {
                ss << " min=\"" << *u16->getMin() << "\" max=\"" << *u16->getMax() << "\"";
            }

        } else if (auto u32 = dynamic_cast<const UInt32Variable *>(v)) {

            if (with_start) {
                ss << " start=\"" << u32->get() << "\"";
            }
            if (u32->getMin() && u32->getMax()) {
                ss << " min=\"" << *u32->getMin() << "\" max=\"" << *u32->getMax() << "\"";
            }

        } else if (auto i64 = dynamic_cast<const Int64Variable *>(v)) {

            if (with_start) {
                ss << " start=\"" << i64->get() << "\"";
            }
            if (i64->getMin() && i64->getMax()) {
                ss << " min=\"" << *i64->getMin() << "\" max=\"" << *i64->getMax() << "\"";
            }

        } else if (auto u64 = dynamic_cast<const UInt64Variable *>(v)) {

            if (with_start) {
                ss << " start=\"" << u64->get() << "\"";
            }
            if (u64->getMin() && u64->getMax()) {
                ss << " min=\"" << *u64->getMin() << "\" max=\"" << *u64->getMax() << "\"";
            }

        } else if (auto b = dynamic_cast<const BoolVariable *>(v)) {

            if (with_start) {
                ss << " start=\"" << b->get() << "\"";
            }

        } else if (auto s = dynamic_cast<const StringVariable *>(v)) {

            if (with_start) {
                ss << ">\n";
                ss << "\t\t\t<Start value=\"" << xml_escape(s->get()) << "\"/>\n";
            }
        } else if (auto bin = dynamic_cast<const BinaryVariable *>(v)) {
            ss << " mimeType=\"" << xml_escape(bin->mime_type()) << "\"";
            if (bin->max_size()) {
                ss << " maxSize=\"" << *bin->max_size() << "\"";
            }
            if (with_start) {
                ss << ">\n";
                ss << "\t\t\t<Start value=\"" << hex_encode(bin->get()) << "\"/>\n";
            }
        }

        if (with_start && dynamic_cast<const StringVariable *>(v)) {
            ss << "\t\t</String>\n";
        } else if (with_start && dynamic_cast<const BinaryVariable *>(v)) {
            ss << "\t\t</Binary>\n";
        } else {
            ss << "/>\n";
        }
    }

    ss << "\t</ModelVariables>\n\n";

    ss << "\t<ModelStructure>\n";

    const auto unknowns = collect_variables([](auto &v) {
        return v.causality() == causality_t::OUTPUT;
    });

    if (!unknowns.empty()) {
        for (const auto &v: unknowns) {
            ss << "\t\t<Output valueReference=\"" << v->value_reference() << "\"";
            if (const auto deps = v->getDependencies(); !deps.empty()) {
                ss << " dependencies=\"";
                for (unsigned i = 0; i < deps.size(); i++) {
                    const auto &depName = deps[i];
                    const auto dep = std::find_if(allVars.begin(), allVars.end(), [depName](const auto &v) {
                        return v->name() == depName;
                    });
                    if (dep == allVars.end()) {
                        throw std::runtime_error("Unknown dependency: " + depName);
                    }
                    ss << (*dep)->value_reference();
                    if (i != deps.size() - 1) {
                        ss << " ";
                    }
                }
                ss << "\"";
            }
            ss << "/>\n";
        }
    }

    const auto initialUnknowns = collect_variables([](auto &v) {
        return (v.causality() == causality_t::OUTPUT && (v.initial() == initial_t::APPROX || v.initial() == initial_t::CALCULATED)) || v.causality() == causality_t::CALCULATED_PARAMETER;
    });
    if (!initialUnknowns.empty()) {
        for (const auto &v: initialUnknowns) {
            ss << "\t\t<InitialUnknown valueReference=\"" << v->value_reference() << "\"";
            ss << "/>\n";
        }
    }

    ss << "\t</ModelStructure>\n\n";

    // if (!m.vendorAnnotations.empty()) {
    //     ss << "\t<Annotations>\n";
    //     for (const auto &annotation: m.vendorAnnotations) {
    //         std::string indentedAnnotation = indent_multiline_string(annotation, 2);
    //         ss << indentedAnnotation << "\n";
    //     }
    //     ss << "\t</Annotations>\n\n";
    // }

    ss << "</fmiModelDescription>\n";

    return ss.str();
}
