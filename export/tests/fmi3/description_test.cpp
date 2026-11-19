#include "fmi3/fmi3Functions.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fmu4cpp/fmu_base.hpp>
#include <fstream>

class TestFmi3Model : public fmu4cpp::fmu_base {
public:
    FMU4CPP_CTOR(TestFmi3Model) {
        // String variable with special characters in name/description/start value
        register_string("str_var", &s_in_)
                .setCausality(fmu4cpp::causality_t::INPUT)
                .setDescription("A <string> & 'more' \"quotes\"");

        // Binary variable with start value and max size
        b_in_ = {0xDE, 0xAD, 0xBE, 0xEF};
        register_binary("bin_var", &b_in_)
                .setCausality(fmu4cpp::causality_t::INPUT)
                .setMimeType("application/octet-stream")
                .setMaxSize(16);

        // Clocks - specify an explicit VR for clk_in (e.g. 50) to make valueReferences non-consecutive
        register_clock("clk_in", 50, fmu4cpp::causality_t::INPUT);
        register_clock("clk_out", fmu4cpp::causality_t::OUTPUT);// VR will be 51

        // Variable associated with clock
        register_integer("int_clocked", &i_clocked_)
                .setCausality(fmu4cpp::causality_t::INPUT)
                .setAssociatedClock(50);

        // Output variable with CALCULATED initial: VR will be 53, but index is 6 (index - 1 would be 5)
        register_real("real_out", &r_out_)
                .setCausality(fmu4cpp::causality_t::OUTPUT)
                .setInitial(fmu4cpp::initial_t::CALCULATED);
    }

    bool do_step(double dt) override { return true; }

private:
    double r_out_{3.14};
    std::string s_in_{"foo & bar < baz > 'tick' \"quote\""};
    std::vector<uint8_t> b_in_;
    int i_clocked_{100};
};

fmu4cpp::model_info fmu4cpp::get_model_info() {
    model_info info;
    info.modelName = "FMI3 & Test <Model>";
    info.description = "Testing & Validating FMI3 \"XML\" <generation>";
    info.author = "Test & QA 'Team'";
    info.version = "1.0.0";
    info.canGetAndSetFMUstate = true;
    info.canSerializeFMUstate = true;
    info.hasEventMode = false;// Should become true because clocks are registered
    return info;
}

std::string fmu4cpp::model_identifier() {
    return "description_test";
}

FMU4CPP_INSTANTIATE(TestFmi3Model);

TEST_CASE("fmi3_description_tests") {
    TestFmi3Model model({});
    const std::string xml = model.make_description();

    SECTION("Capabilities casing and values") {
        CHECK(xml.find("canGetAndSetFMUState=\"true\"") != std::string::npos);
        CHECK(xml.find("canSerializeFMUState=\"true\"") != std::string::npos);
        CHECK(xml.find("hasEventMode=\"true\"") != std::string::npos);
        // Ensure incorrect casings are NOT present
        CHECK(xml.find("canGetAndSetFMUstate") == std::string::npos);
        CHECK(xml.find("canSerializeFMUstate") == std::string::npos);
    }

    SECTION("Special character XML escaping") {
        CHECK(xml.find("modelName=\"FMI3 &amp; Test &lt;Model&gt;\"") != std::string::npos);
        CHECK(xml.find("description=\"Testing &amp; Validating FMI3 &quot;XML&quot; &lt;generation&gt;\"") != std::string::npos);
        CHECK(xml.find("author=\"Test &amp; QA &apos;Team&apos;\"") != std::string::npos);
        CHECK(xml.find("description=\"A &lt;string&gt; &amp; &apos;more&apos; &quot;quotes&quot;\"") != std::string::npos);
        CHECK(xml.find("<Start value=\"foo &amp; bar &lt; baz &gt; &apos;tick&apos; &quot;quote&quot;\"/>") != std::string::npos);
    }

    SECTION("Clock variables and attributes") {
        // Clock elements
        CHECK(xml.find("<Clock name=\"clk_in\" valueReference=\"50\" causality=\"input\" intervalVariability=\"triggered\" canBeDeactivated=\"false\"/>") != std::string::npos);
        CHECK(xml.find("<Clock name=\"clk_out\" valueReference=\"51\" causality=\"output\" intervalVariability=\"triggered\" canBeDeactivated=\"false\"/>") != std::string::npos);

        // Clocks must NOT have variability or initial
        auto clkPos = xml.find("<Clock name=\"clk_in\"");
        REQUIRE(clkPos != std::string::npos);
        auto clkEnd = xml.find("/>", clkPos);
        std::string clkTag = xml.substr(clkPos, clkEnd - clkPos);
        CHECK(clkTag.find("variability=") == std::string::npos);
        CHECK(clkTag.find("initial=") == std::string::npos);

        // Clocked variable
        CHECK(xml.find("<Int32 name=\"int_clocked\" valueReference=\"52\" causality=\"input\" clocks=\"50\" start=\"100\"/>") != std::string::npos);
    }

    SECTION("Binary and String format - no Dimension tag for scalars") {
        CHECK(xml.find("<Binary name=\"bin_var\" valueReference=\"2\" causality=\"input\" mimeType=\"application/octet-stream\" maxSize=\"16\">") != std::string::npos);
        CHECK(xml.find("<Start value=\"DEADBEEF\"/>") != std::string::npos);

        // Ensure no Dimension tags exist in the entire XML
        CHECK(xml.find("<Dimension") == std::string::npos);
    }

    SECTION("ModelStructure and InitialUnknown valueReference") {
        // Output clock (vr=51) and real_out (vr=53) must be in ModelStructure Outputs
        CHECK(xml.find("<Output valueReference=\"53\"/>") != std::string::npos);
        CHECK(xml.find("<Output valueReference=\"51\"/>") != std::string::npos);

        // InitialUnknown must reference 53 (the actual value reference), not index - 1 (which would be 5)
        CHECK(xml.find("<InitialUnknown valueReference=\"53\"/>") != std::string::npos);
        CHECK(xml.find("<InitialUnknown valueReference=\"5\"/>") == std::string::npos);
    }

    SECTION("XSD Schema Validation via Python lxml") {
        const std::string xmlPath = "/tmp/fmi3_description_test.xml";
        std::ofstream out(xmlPath);
        out << xml;
        out.close();

        const std::string cmd =
                "/root/dev/fmu_tranport/boom_ground/.venv/bin/python -c '"
                "import lxml.etree as etree\n"
                "schema = etree.XMLSchema(etree.parse(\"/root/dev/fmu_tranport/boom_ground/.venv/lib/python3.14/site-packages/fmpy/schema/fmi3/fmi3ModelDescription.xsd\"))\n"
                "doc = etree.parse(\"" +
                xmlPath + "\")\n"
                          "schema.assertValid(doc)\n"
                          "'";

        int res = std::system(cmd.c_str());
        CHECK(res == 0);
    }
}
