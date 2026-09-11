
#ifndef FMU4CPP_UTIL_HPP
#define FMU4CPP_UTIL_HPP

#include <string>

namespace fmu4cpp {

    inline std::string indent_multiline_string(const std::string &input, const int indents) {
        const std::string tabs(indents, '\t');
        std::string indentedString = tabs + input;// add initial indentation
        size_t pos = 0;
        while ((pos = indentedString.find('\n', pos)) != std::string::npos) {
            indentedString.replace(pos, 1, "\n" + tabs);
            pos += 3;
        }
        return indentedString;
    }

}// namespace fmu4cpp

#endif//FMU4CPP_UTIL_HPP
