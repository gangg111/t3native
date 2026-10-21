// remake/content/game_ram.gd
#include "content/game_ram.h"

#include "content/json_file.h"
#include "content/text_locale.h"
#include "core/game/byte_block.h"
#include "core/math/fx.h"
#include "gd/io.h"

namespace t3 {

namespace {
// `str(v)` of a JSON value the converter writes as a string.
std::string jstr(const gd::Json& v) { return v.is_string() ? v.str() : std::string(); }
}  // namespace

gd::Int GameRam::Block::size() const {
    return live != nullptr ? live->bytes.size() : bytes.size();
}

gd::PBytes GameRam::Block::data() const {
    return live != nullptr ? live->bytes : bytes;
}

gd::Ref<GameRam> GameRam::load_from(const std::string& dir) {
    auto ram = gd::make<GameRam>();
    ram->directory = dir;
    gd::Json index = JsonFile::read(gd::path_join(dir, "ram.json"));
    for (const auto& [name, entry] : index.members()) {
        auto b = gd::make<Block>();
        b->name = name;
        b->base = JsonFile::number(entry["base"]);
        b->bytes = gd::read_file(gd::path_join(dir, jstr(entry["file"])));
        const gd::Json& aside = entry.get("strings");
        for (const auto& [key, text] : aside.members()) {
            b->strings.set(std::stoll(key), jstr(text));   // key.to_int(): decimal addresses
        }
        ram->_files.set(name, b);
    }
    ram->exe = ram->_files.at("exe");
    return ram;
}

gd::Ref<GameRam> GameRam::fresh() const {
    auto ram = gd::make<GameRam>();
    ram->directory = directory;
    ram->_files = _files;
    ram->exe = exe;
    return ram;
}

void GameRam::load_overlay(const std::string& name) {
    gd::Ref<Block> b = _files.get(name, nullptr);
    if (b == nullptr) {
        return;
    }
    for (gd::Int i = blocks.size() - 1; i > -1; --i) {
        if (blocks[i]->live == nullptr && blocks[i]->base == b->base) {
            blocks.remove_at(i);
        }
    }
    blocks.append(b);
    _shown.clear();
}

void GameRam::unmap(const gd::Ref<ByteBlock>& block) {
    for (gd::Int i = blocks.size() - 1; i > -1; --i) {
        if (blocks[i]->live == block) {
            blocks.remove_at(i);
            _shown.clear();
        }
    }
}

void GameRam::map(gd::Int base, const gd::Ref<ByteBlock>& block) {
    for (const gd::Ref<Block>& other : blocks) {
        if (other->live == block) {
            return;
        }
    }
    auto b = gd::make<Block>();
    b->base = base;
    b->live = block;
    blocks.push_front(b);
    _shown.clear();
}

gd::Ref<GameRam::Block> GameRam::_find(gd::Int address, gd::Int size) const {
    address &= 0xFFFFFFFFLL;
    for (const gd::Ref<Block>& b : blocks) {
        if (address >= b->base && address + size <= b->base + b->size()) {
            return b;
        }
    }
    if (exe != nullptr && address >= exe->base && address + size <= exe->base + exe->size()) {
        return exe;
    }
    return nullptr;
}

gd::Int GameRam::u8(gd::Int address) const {
    gd::Ref<Block> b = _find(address, 1);
    return b == nullptr ? 0 : gd::Int(b->data()[(address & 0xFFFFFFFFLL) - b->base]);
}

gd::Int GameRam::s8(gd::Int address) const {
    return Fx::s8(u8(address));
}

gd::Int GameRam::u16(gd::Int address) const {
    gd::Ref<Block> b = _find(address, 2);
    return b == nullptr ? 0 : b->data().decode_u16((address & 0xFFFFFFFFLL) - b->base);
}

gd::Int GameRam::s16(gd::Int address) const {
    return Fx::s16(u16(address));
}

gd::Int GameRam::u32(gd::Int address) const {
    gd::Ref<Block> b = _find(address, 4);
    return b == nullptr ? 0 : b->data().decode_u32((address & 0xFFFFFFFFLL) - b->base);
}

gd::Int GameRam::s32(gd::Int address) const {
    return Fx::w32(u32(address));
}

std::string GameRam::string(gd::Int address) {
    address &= 0xFFFFFFFFLL;
    bool english = locale != nullptr && locale->english;
    if (locale != _shown_locale || english != _shown_english) {
        _shown.clear();
        _shown_locale = locale;
        _shown_english = english;
    }
    if (_shown.has(address)) {
        return _shown.at(address);
    }
    gd::Ref<Block> b = _find(address, 1);
    if (b == nullptr) {
        return "";
    }
    std::string s = b->strings.get(address, "");
    if (s.empty()) {
        gd::PBytes data = b->data();
        gd::Int start = address - b->base;
        gd::Int end = data.find(0, start);
        end = gd::mini(end < 0 ? data.size() : end, start + MAX_STRING);
        s = data.slice(start, end).get_string_from_ascii();
    }
    if (english && !b->name.empty()) {
        s = locale->text(b->name, s);
    }
    if (b->live == nullptr) {
        _shown.set(address, s);
    }
    return s;
}

gd::PBytes GameRam::bytes(gd::Int address, gd::Int n) const {
    gd::Int a = address & 0xFFFFFFFFLL;
    // The first block the range touches (as u8 searches) gives it all when it holds it all.
    gd::Arr<gd::Ref<Block>> search = blocks.duplicate();   // `blocks + [exe]`
    search.append(exe);
    for (const gd::Ref<Block>& b : search) {
        if (b != nullptr && a < b->base + b->size() && a + n > b->base) {
            if (a >= b->base && a + n <= b->base + b->size()) {
                return b->data().slice(a - b->base, a - b->base + n);
            }
            break;
        }
    }
    // Across blocks or partly unmapped (0): byte by byte.
    gd::PBytes out;
    out.resize(n);
    for (gd::Int i = 0; i < n; ++i) {
        out[i] = u8(address + i);
    }
    return out;
}

}  // namespace t3
