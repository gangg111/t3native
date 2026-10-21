// remake/core/game/byte_block.gd
#include "core/game/byte_block.h"

namespace t3 {

ByteBlock::ByteBlock(gd::Int size) {
    bytes.resize(size);
}

void ByteBlock::copy_from(const gd::Ref<ByteBlock>& other) {
    bytes = other->bytes.duplicate();
}

}  // namespace t3
