#pragma once
// remake/content/game_ram.gd
// The game's memory as the screens' drawing reads it (their ports read tables, strings and
// state by address, as `tools/research/*_sim.py` do): the live byte blocks of the remake's screens
// first (a screen context, the progress block, the mode region), then the loaded overlays, then
// the executable, whose image spans the overlay slots (`imported/screens/ram/`, converted by
// `tools/remake_import/screen_vram.py`). The images are in Japan Rev.1's layout whichever release
// was converted; a text of another release that does not fit there is kept aside by address
// (`ram.json` "strings"). Strings read from the executable and the overlays are shown through the
// text locale (English when on, TextLocale).
#include <string>

#include "gd/gd.h"

namespace t3 {

class ByteBlock;
class TextLocale;

class GameRam {
public:
    class Block {
    public:
        std::string name = "";        // the executable's or overlay's name ("" for a live block)
        gd::Int base = 0;
        gd::PBytes bytes;
        gd::Ref<ByteBlock> live;      // a remake block read in place (its `bytes` change)
        gd::Dict<gd::Int, std::string> strings;   // address -> text kept aside (another release's, too long to fit)

        gd::Int size() const;
        gd::PBytes data() const;
    };

    static constexpr gd::Int MAX_STRING = 256;

    gd::Arr<gd::Ref<Block>> blocks;               // searched in order: the live blocks, then the overlays
    gd::Ref<Block> exe;                           // searched last: the executable's image spans the overlay slots
    gd::Dict<std::string, gd::Ref<Block>> _files; // overlay name -> Block
    std::string directory = "";
    gd::Ref<TextLocale> locale;                   // null: the Japanese release's texts
    gd::Dict<gd::Int, std::string> _shown;        // address -> string() of an image block (not live), as last localized
    gd::Ref<TextLocale> _shown_locale;            // the locale and English flag _shown was made with
    bool _shown_english = false;

    static gd::Ref<GameRam> load_from(const std::string& dir);

    // Another view of the same memory images (shared, not re-read) with no live block or overlay.
    gd::Ref<GameRam> fresh() const;
    // An overlay in its slot (the previous occupant of the same slot is dropped).
    void load_overlay(const std::string& name);
    // Takes a mapped byte block out of the search (a resident screen's context that shares an
    // overlay slot: the quick select's at 0x800B9378 only while it is shown).
    void unmap(const gd::Ref<ByteBlock>& block);
    // A remake byte block read at its game address (first in the search).
    void map(gd::Int base, const gd::Ref<ByteBlock>& block);
    gd::Ref<Block> _find(gd::Int address, gd::Int size) const;
    gd::Int u8(gd::Int address) const;
    gd::Int s8(gd::Int address) const;
    gd::Int u16(gd::Int address) const;
    gd::Int s16(gd::Int address) const;
    gd::Int u32(gd::Int address) const;
    gd::Int s32(gd::Int address) const;
    // A zero-terminated string (at most MAX_STRING bytes, within the block it starts in), localized
    // by that block.
    std::string string(gd::Int address);
    // `n` bytes from `address`.
    gd::PBytes bytes(gd::Int address, gd::Int n) const;
};

}  // namespace t3
