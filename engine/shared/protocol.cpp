#include "shared/protocol.h"

#include <algorithm>

namespace k::proto {
namespace {

enum Field : uint8_t {
    F_MODEL = 1u << 0,
    F_ORIGIN = 1u << 1,
    F_ANGLES = 1u << 2,
    F_EFFECTS = 1u << 3,
    F_REMOVE = 1u << 7,
};
constexpr uint16_t kEnd = 0xFFFF;

uint8_t diff(const EntityState& a, const EntityState& b) {
    uint8_t m = 0;
    if (a.model != b.model) m |= F_MODEL;
    if (a.origin != b.origin) m |= F_ORIGIN;
    if (!(a.angles == b.angles)) m |= F_ANGLES;
    if (a.effects != b.effects) m |= F_EFFECTS;
    return m;
}

void writeFields(net::ByteWriter& w, const EntityState& s, uint8_t mask) {
    w.write(s.index);
    w.write(mask);
    if (mask & F_MODEL) w.write(s.model);
    if (mask & F_ORIGIN) w.write(s.origin);
    if (mask & F_ANGLES) w.write(s.angles);
    if (mask & F_EFFECTS) w.write(s.effects);
}

} // namespace

void writeEntityDelta(net::ByteWriter& w, const std::vector<EntityState>& base, const std::vector<EntityState>& cur) {
    size_t i = 0, j = 0;
    while (i < base.size() || j < cur.size()) {
        if (j >= cur.size() || (i < base.size() && base[i].index < cur[j].index)) {
            // In base, gone now.
            w.write(base[i].index);
            w.write(static_cast<uint8_t>(F_REMOVE));
            ++i;
        } else if (i >= base.size() || cur[j].index < base[i].index) {
            // New: send everything.
            writeFields(w, cur[j], F_MODEL | F_ORIGIN | F_ANGLES | F_EFFECTS);
            ++j;
        } else {
            uint8_t mask = diff(base[i], cur[j]);
            if (mask) writeFields(w, cur[j], mask);
            ++i;
            ++j;
        }
    }
    w.write(kEnd);
}

bool readEntityDelta(net::ByteReader& r, const std::vector<EntityState>& base, std::vector<EntityState>& out) {
    out = base;
    for (;;) {
        uint16_t index = r.read<uint16_t>();
        if (r.overflowed()) return false;
        if (index == kEnd) break;
        uint8_t mask = r.read<uint8_t>();
        auto it = std::lower_bound(out.begin(), out.end(), index, [](const EntityState& e, uint16_t i) { return e.index < i; });
        bool exists = it != out.end() && it->index == index;
        if (mask & F_REMOVE) {
            if (exists) out.erase(it);
            continue;
        }
        if (!exists) {
            EntityState fresh;
            fresh.index = index;
            it = out.insert(it, fresh);
        }
        if (mask & F_MODEL) it->model = r.read<uint16_t>();
        if (mask & F_ORIGIN) it->origin = r.read<Vec3>();
        if (mask & F_ANGLES) it->angles = r.read<QAngle>();
        if (mask & F_EFFECTS) it->effects = r.read<uint32_t>();
        if (r.overflowed()) return false;
    }
    return true;
}

} // namespace k::proto
