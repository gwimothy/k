#pragma once
// Entity input/output (Source's I/O system).
//
// An output is a list of connections written in the map as
//     "OnPressed" "door1,Open,,0,-1"
// meaning: target name, input name, parameter, delay in seconds, times to fire (-1 = forever).
// Several connections on one output use keys with a "#" suffix: "OnPressed#2" "...".
// Targets can be names, wildcards ("door*"), or "!activator", "!caller", "!self".

#include "shared/kmath.h"

#include <string>
#include <vector>

namespace k::game {

class BaseEntity;

// Case-insensitive ordering for entity/output/input names.
struct NameLess {
    bool operator()(const std::string& a, const std::string& b) const;
};
bool nameEquals(const std::string& a, const std::string& b);
// "door*" matches "door1"; "!self" etc. are handled by the event queue.
bool nameMatches(const std::string& pattern, const std::string& name);

struct Connection {
    std::string target;
    std::string input;
    std::string param;
    float delay = 0;
    int timesToFire = -1; // -1 = unlimited
};

// Parses "target,Input,param,delay,times". Source also allows the ESC character as the
// separator; both are accepted. Returns false if malformed.
bool parseConnection(const std::string& text, Connection& out);

class Output {
public:
    void addConnection(const Connection& c) { connections_.push_back(c); }
    // Queue every connection. `value` overrides empty parameters (like Source's output values).
    void fire(BaseEntity* activator, BaseEntity* caller, const std::string& value = {});
    const std::vector<Connection>& connections() const { return connections_; }

private:
    std::vector<Connection> connections_;
};

} // namespace k::game
