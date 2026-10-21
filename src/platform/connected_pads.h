#pragma once
// Godot's Input.get_connected_joypads() natively: the pad devices the platform reported connected
// (App::pad_connected). Input.joy_connection_changed is emitted only when a pad comes or goes, so a
// repeated report changes nothing.
#include <set>

#include "gd/gd.h"

namespace t3 {

class ConnectedPads {
public:
    // The platform's report; true when it is a change (joy_connection_changed is emitted).
    bool report(int device, bool connected) {
        if (connected) return devices_.insert(device).second;
        return devices_.erase(device) != 0;
    }
    // Input.get_connected_joypads().size()
    gd::Int size() const { return static_cast<gd::Int>(devices_.size()); }
    bool has(int device) const { return devices_.count(device) != 0; }

private:
    std::set<int> devices_;
};

}  // namespace t3
