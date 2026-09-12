#include "infraforge/viewport/ViewportApplication.hpp"

#include <iostream>
#include <string>

namespace {

void printUsage(std::ostream& stream) {
    stream
        << "InfraForge native viewport\n\n"
        << "Usage:\n"
        << "  infraforge-viewport --parent-window <hex-hwnd> --screen-x <px> --screen-y <px>\n"
        << "                      --width <px> --height <px> --dpi-scale <milli-percent>\n\n"
        << "Control commands arrive as one JSON object per stdin line:\n"
        << "  {\"type\":\"place\",\"screenX\":..,\"screenY\":..,\"width\":..,\"height\":..,\"dpiScale\":..}\n"
        << "  {\"type\":\"visibility\",\"visible\":true}\n"
        << "  {\"type\":\"shutdown\"}\n";
}

} // namespace

int main(int argc, char** argv) {
    infraforge::viewport::ApplicationArguments arguments;
    std::string errorMessage;
    if (!infraforge::viewport::parseApplicationArguments(argc, argv, arguments, errorMessage)) {
        std::cerr << "Invalid viewport arguments: " << errorMessage << '\n';
        printUsage(std::cerr);
        return 2;
    }
    return infraforge::viewport::runViewportApplication(arguments);
}
