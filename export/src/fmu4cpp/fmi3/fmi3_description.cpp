#include "fmu4cpp/fmu_base.hpp"
#include "fmu4cpp/lib_info.hpp"
#include "fmu4cpp/time.hpp"
#include "fmu4cpp/util.hpp"

#include <algorithm>
#include <sstream>

using namespace fmu4cpp;


namespace {

    bool is_integer_type(data_type dt) {
        switch (dt) {
            case data_type::INT8:
            case data_type::UINT8:
            case data_type::INT16:
            case data_type::UINT16:
            case data_type::INT32:
            case data_type::UINT32:
            case data_type::INT64:
            case data_type::UINT64:
                return true;
            default:
                return false;
        }
    }

    bool is_float_type(data_type dt) {
        return dt == data_type::FLOAT32 || dt == data_type::FLOAT64;
    }

    static std::string escape_xml(const std::string &data) {
        std::string buffer;
        buffer.reserve(data.size());
        for (char c: data) {
            switch (c) {
                case '&':
                    buffer.append("&amp;");
                    break;
                case '"':
                    buffer.append("&quot;");
                    break;
                case '\'':
                    buffer.append("&apos;");
                    break;
                case '<':
                    buffer.append("&lt;");
                    break;
                case '>':
                    buffer.append("&gt;");
                    break;
                default:
                    buffer.push_back(c);
                    break;
            }
        }
        return buffer;
    }

}// namespace


std::string fmu_base::make_description() const {

    const model_info m = get_model_info();
    std::stringstream ss;
    ss << R"(<?xml version="1.0" encoding="UTF-8"?>)" << "\n"
       << "<fmiModelDescription fmiVersion=\"3.0\"\n"
       << "\tmodelName=\"" << escape_xml(m.modelName) << "\"\n"
       << "\tinstantiationToken=\"" << guid() << "\"\n"
       << "\tgenerationTool=\"fmu4cpp v" << to_string(library_version()) << "\"\n"
       << "\tgenerationDateAndTime=\"" << now() << "\"\n"
       << "\tdescription=\"" << escape_xml(m.description) << "\"\n"
       << "\tauthor=\"" << escape_xml(m.author) << "\"\n"
       << "\tvariableNamingConvention=\"" << escape_xml(m.variableNamingConvention) << "\""
       << ">\n\n";

    ss << std::boolalpha
       << "\t<CoSimulation\n"
       << "\t\tneedsExecutionTool=\"" << m.needsExecutionTool << "\"\n"
       << "\t\tmodelIdentifier=\"" << escape_xml(model_identifier()) << "\"\n"
       << "\t\tcanHandleVariableCommunicationStepSize=\"" << m.canHandleVariableCommunicationStepSize << "\"\n"
       << "\t\tcanBeInstantiatedOnlyOncePerProcess=\"" << m.canBeInstantiatedOnlyOncePerProcess << "\"\n"
       << "\t\tcanGetAndSetFMUstate=\"" << m.canGetAndSetFMUstate << "\"\n"
       << "\t\tcanSerializeFMUstate=\"" << m.canSerializeFMUstate << "\"\n"
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
        std::vector<const VariableBase *> vars;
        vars.reserve(variables_.size());
        for (const auto &v: variables_) {
            vars.emplace_back(v.get());
        }
        std::sort(vars.begin(), vars.end(), [](const VariableBase *v1, const VariableBase *v2) {
            return v1->index() < v2->index();
        });
        return vars;
    }();

    for (const auto &v: allVars) {
        const auto variability = v->variability();
        const auto initial = v->initial();
        const auto annotations = v->getAnnotations();
        ss << "\t\t<!--"
           << "index=" << v->index() << "-->\n"
           << "\t\t<" << v->type_name() << " name=\""
           << escape_xml(v->name()) << "\" valueReference=\"" << v->value_reference() << "\""
           << " causality=\"" << to_string(v->causality()) << "\"";

        if (variability) {
            ss << " variability=\"" << to_string(*variability) << "\"";
        }
        if (initial) {
            ss << " initial=\"" << to_string(*initial) << "\"";
        }

        if (auto desc = v->getDescription(); !desc.empty()) {
            ss << " description=\"" << escape_xml(desc) << "\"";
        }

        bool with_start = requires_start(*v);
        const auto startStr = v->get_start_as_string();
        if (is_integer_type(v->type())) {
            if (with_start && startStr) {
                ss << " start=\"" << *startStr << "\"";
            }
            if (const auto min = v->get_min_as_string()) {
                ss << " min=\"" << *min << "\"";
            }
            if (const auto max = v->get_max_as_string()) {
                ss << " max=\"" << *max << "\"";
            }
        } else if (is_float_type(v->type())) {
            if (with_start && startStr) {
                ss << " start=\"" << *startStr << "\"";
            }
            if (const auto unit = v->getUnit()) {
                ss << " unit=\"" << escape_xml(*unit) << "\"";
            }
            if (const auto min = v->get_min_as_string()) {
                ss << " min=\"" << *min << "\"";
            }
            if (const auto max = v->get_max_as_string()) {
                ss << " max=\"" << *max << "\"";
            }
        } else if (v->type() == data_type::BOOLEAN) {
            if (with_start && startStr) {
                ss << " start=\"" << *startStr << "\"";
            }
        }

        const bool is_str_with_start = (v->type() == data_type::STRING) && with_start;
        const bool is_bin_with_start = (v->type() == data_type::BINARY) && with_start;
        const bool has_children = !annotations.empty() || is_str_with_start || is_bin_with_start;

        if (has_children) {
            ss << ">\n";
            if (!annotations.empty()) {
                ss << "\t\t\t<Annotations>\n";
                for (const auto &annotation: annotations) {
                    std::string indentedAnnotation = indent_multiline_string(annotation, 4);
                    ss << indentedAnnotation << "\n";
                }
                ss << "\t\t\t</Annotations>\n";
            }
            if (is_str_with_start && startStr) {
                ss << "\t\t\t<Start value=\"" << escape_xml(*startStr) << "\"/>\n";
            } else if (is_bin_with_start && startStr) {
                ss << "\t\t\t<Start value=\"" << *startStr << "\"/>\n";
            }
            ss << "\t\t</" << v->type_name() << ">\n";
        } else {
            ss << "/>\n";
        }
    }

    ss << "\t</ModelVariables>\n\n";

    ss << "\t<ModelStructure>\n";

    std::vector<const VariableBase *> unknowns;
    for (const auto &v: variables_) {
        if (v->causality() == causality_t::OUTPUT) {
            unknowns.emplace_back(v.get());
        }
    }

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

    std::vector<const VariableBase *> initialUnknowns;
    for (const auto &v: variables_) {
        if ((v->causality() == causality_t::OUTPUT && (v->initial() == initial_t::APPROX || v->initial() == initial_t::CALCULATED)) || v->causality() == causality_t::CALCULATED_PARAMETER) {
            initialUnknowns.emplace_back(v.get());
        }
    }
    if (!initialUnknowns.empty()) {
        for (const auto &v: initialUnknowns) {
            ss << "\t\t<InitialUnknown valueReference=\"" << v->value_reference() << "\"";
            ss << "/>\n";
        }
    }

    ss << "\t</ModelStructure>\n\n";

    if (!m.vendorAnnotations.empty()) {
        ss << "\t<Annotations>\n";
        for (const auto &annotation: m.vendorAnnotations) {
            std::string indentedAnnotation = indent_multiline_string(annotation, 2);
            ss << indentedAnnotation << "\n";
        }
        ss << "\t</Annotations>\n\n";
    }

    ss << "</fmiModelDescription>\n";

    return ss.str();
}
