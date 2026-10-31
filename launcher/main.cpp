// Entry point. All the work happens in k::Host.

#include "host/host.h"

#include <SDL3/SDL_main.h>

#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    std::vector<std::string> args(argv, argv + argc);
    k::Host host;
    return host.run(args);
}
