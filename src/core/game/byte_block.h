#pragma once
// remake/core/game/byte_block.gd
// A block of the game's memory kept with its original layout: the save block and records, and
// the mode context with the fight's globals. Their structures are unions of per-mode and
// per-page layouts in the game, so the remake keeps the bytes and names the fields with
// accessors (subclasses); the flow traces compare the blocks byte for byte.
#include "gd/gd.h"

namespace t3 {

class ByteBlock {
public:
    // The accessor a ByteField reads with (u8, u16, u32, s32); every setter is put8/16/32.
    enum FieldKind : gd::Int { U8 = 1, U16 = 2, U32 = 4, S32 = -4 };

    gd::PBytes bytes;

    explicit ByteBlock(gd::Int size = 0);
    virtual ~ByteBlock() = default;
    ByteBlock(const ByteBlock&) = delete;
    ByteBlock& operator=(const ByteBlock&) = delete;

    gd::Int u8(gd::Int at) const { return bytes.decode_u8(at); }
    gd::Int s8(gd::Int at) const { return bytes.decode_s8(at); }
    gd::Int u16(gd::Int at) const { return bytes.decode_u16(at); }
    gd::Int s16(gd::Int at) const { return bytes.decode_s16(at); }
    gd::Int u32(gd::Int at) const { return bytes.decode_u32(at); }
    gd::Int s32(gd::Int at) const { return bytes.decode_s32(at); }
    void put8(gd::Int at, gd::Int v) const { bytes.encode_u8(at, v & 0xFF); }
    void put16(gd::Int at, gd::Int v) const { bytes.encode_u16(at, v & 0xFFFF); }
    void put32(gd::Int at, gd::Int v) const { bytes.encode_u32(at, v & 0xFFFFFFFFLL); }

    void copy_from(const gd::Ref<ByteBlock>& other);
};

// A GDScript property over the block's bytes (`var x: int: get: return u8(AT)  set(v): put8(AT, v)`),
// translated as a slot like gd::ElemRef: `gd::Int v = block->x;` reads through the getter,
// `block->x = v;` (and += -= |= &= ^=) stores through the setter. Members are declared
// `ByteField<AT, KIND> x{this};`. In ternaries and template calls wrap it: `gd::Int(block->x)`.
template <gd::Int AT, gd::Int KIND>
class ByteField {
public:
    explicit ByteField(ByteBlock* block) : block_(block) {}
    ByteField(const ByteField&) = delete;

    gd::Int get() const {
        if constexpr (KIND == ByteBlock::U8) return block_->u8(AT);
        else if constexpr (KIND == ByteBlock::U16) return block_->u16(AT);
        else if constexpr (KIND == ByteBlock::U32) return block_->u32(AT);
        else return block_->s32(AT);
    }
    void set(gd::Int v) const {
        if constexpr (KIND == ByteBlock::U8) block_->put8(AT, v);
        else if constexpr (KIND == ByteBlock::U16) block_->put16(AT, v);
        else block_->put32(AT, v);
    }
    operator gd::Int() const { return get(); }
    const ByteField& operator=(gd::Int v) const { set(v); return *this; }
    const ByteField& operator=(const ByteField& o) const { set(o.get()); return *this; }
    const ByteField& operator+=(gd::Int v) const { set(get() + v); return *this; }
    const ByteField& operator-=(gd::Int v) const { set(get() - v); return *this; }
    const ByteField& operator|=(gd::Int v) const { set(get() | v); return *this; }
    const ByteField& operator&=(gd::Int v) const { set(get() & v); return *this; }
    const ByteField& operator^=(gd::Int v) const { set(get() ^ v); return *this; }

private:
    ByteBlock* block_;
};

}  // namespace t3
