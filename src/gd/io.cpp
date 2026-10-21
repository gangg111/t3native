#include "gd/io.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <utility>

namespace gd {

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }

std::string PBytes::get_string_from_ascii() const {
    std::string out;
    for (uint8_t b : vec()) {
        if (b == 0) break;
        out += static_cast<char>(b);
    }
    return out;
}

std::string PBytes::get_string_from_utf8() const { return std::string(vec().begin(), vec().end()); }

namespace {
AssetHooks g_hooks;
bool is_asset(const std::string& path) { return path.compare(0, ASSET_PREFIX.size(), ASSET_PREFIX) == 0; }
}  // namespace

void set_asset_hooks(const AssetHooks& hooks) { g_hooks = hooks; }

bool file_exists(const std::string& path) {
    if (is_asset(path)) return g_hooks.exists && g_hooks.exists(path.substr(ASSET_PREFIX.size()));
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

PBytes read_file(const std::string& path) {
    PBytes out;
    if (is_asset(path)) {
        if (!g_hooks.read || !g_hooks.read(path.substr(ASSET_PREFIX.size()), out)) out.clear();
        return out;
    }
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return out;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n > 0) {
        out.resize(n);
        if (std::fread(out.data(), 1, static_cast<size_t>(n), f) != static_cast<size_t>(n)) out.clear();
    }
    std::fclose(f);
    return out;
}

std::string path_join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (a.back() == '/') return a + b;
    return a + "/" + b;
}

bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// ---- inflate (RFC 1951) --------------------------------------------------------------------

namespace {

// Canonical Huffman codes (RFC 1951 3.2.2) with a lookup table for the codes up to FAST bits
// (indexed by the next FAST bits of the stream, which hold a code bit-reversed); longer codes take
// the counting walk.
constexpr int FAST = 10;

struct Huffman {
    std::array<uint16_t, 16> count{};   // codes per length
    std::array<uint16_t, 288> symbol{};  // symbols ordered by code
    std::array<uint16_t, 1 << FAST> fast{};   // (length << 9) | symbol, 0: a longer code
};

class Inflater {
public:
    Inflater(const uint8_t* src, size_t size, std::vector<uint8_t>& out) : src_(src), size_(size), out_(out) {}

    size_t run(size_t at) {
        pos_ = at;
        int last;
        do {
            last = static_cast<int>(bits(1));
            int type = static_cast<int>(bits(2));
            if (type == 0) stored();
            else if (type == 1) fixed();
            else if (type == 2) dynamic();
            else fail("inflate: invalid block type");
        } while (!last);
        // The byte after the deflate stream (partial byte dropped): the whole bytes still buffered
        // are given back.
        return pos_ - static_cast<size_t>(bitcnt_ >> 3);
    }

private:
    // Whole bytes into the 64-bit buffer while the input lasts.
    void refill() {
        while (bitcnt_ <= 56 && pos_ < size_) {
            bitbuf_ |= static_cast<uint64_t>(src_[pos_++]) << bitcnt_;
            bitcnt_ += 8;
        }
    }

    uint32_t bits(int need) {
        if (bitcnt_ < need) {
            refill();
            if (bitcnt_ < need) fail("inflate: unexpected end of data");
        }
        uint32_t v = static_cast<uint32_t>(bitbuf_ & ((uint64_t{1} << need) - 1));
        bitbuf_ >>= need;
        bitcnt_ -= need;
        return v;
    }

    void stored() {
        // To the byte boundary, then the buffered whole bytes back to the input.
        bitbuf_ >>= (bitcnt_ & 7);
        bitcnt_ -= (bitcnt_ & 7);
        pos_ -= static_cast<size_t>(bitcnt_ >> 3);
        bitbuf_ = 0;
        bitcnt_ = 0;
        if (pos_ + 4 > size_) fail("inflate: truncated stored block");
        unsigned len = src_[pos_] | (src_[pos_ + 1] << 8);
        unsigned nlen = src_[pos_ + 2] | (src_[pos_ + 3] << 8);
        pos_ += 4;
        if (len != (~nlen & 0xFFFFu)) fail("inflate: stored block length check");
        if (pos_ + len > size_) fail("inflate: truncated stored block");
        out_.insert(out_.end(), src_ + pos_, src_ + pos_ + len);
        pos_ += len;
    }

    static void build(Huffman& h, const uint8_t* lengths, int n) {
        h.count.fill(0);
        h.fast.fill(0);
        for (int s = 0; s < n; ++s) h.count[lengths[s]]++;
        h.count[0] = 0;
        std::array<uint16_t, 16> offs{};
        for (int len = 1; len < 16; ++len) offs[len] = static_cast<uint16_t>(offs[len - 1] + h.count[len - 1]);
        for (int s = 0; s < n; ++s)
            if (lengths[s]) h.symbol[offs[lengths[s]]++] = static_cast<uint16_t>(s);
        // The codes themselves (3.2.2's next_code), bit-reversed into the table.
        std::array<uint32_t, 16> next{};
        uint32_t code = 0;
        for (int len = 1; len < 16; ++len) {
            code = (code + h.count[len - 1]) << 1;
            next[len] = code;
        }
        for (int s = 0; s < n; ++s) {
            int len = lengths[s];
            if (len == 0 || len > FAST) continue;
            uint32_t c = next[len]++;
            uint32_t rev = 0;
            for (int k = 0; k < len; ++k) rev |= ((c >> k) & 1u) << (len - 1 - k);
            for (uint32_t i = rev; i < (1u << FAST); i += 1u << len) h.fast[i] = static_cast<uint16_t>((len << 9) | s);
        }
    }

    int decode(const Huffman& h) {
        if (bitcnt_ < 15) refill();
        uint16_t e = h.fast[bitbuf_ & ((1u << FAST) - 1)];
        int len = e >> 9;
        if (e != 0 && len <= bitcnt_) {
            bitbuf_ >>= len;
            bitcnt_ -= len;
            return e & 0x1FF;
        }
        // A longer code (or the stream's last bits): one bit at a time.
        int code = 0, first = 0, index = 0;
        for (len = 1; len < 16; ++len) {
            code |= static_cast<int>(bits(1));
            int count = h.count[len];
            if (code - count < first) return h.symbol[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        fail("inflate: invalid Huffman code");
    }

    void codes(const Huffman& lit, const Huffman& dist) {
        static const uint16_t len_base[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                              35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const uint8_t len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                              3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static const uint16_t dist_base[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                               193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
                                               6145, 8193, 12289, 16385, 24577};
        static const uint8_t dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                               6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        while (true) {
            int sym = decode(lit);
            if (sym < 256) { out_.push_back(static_cast<uint8_t>(sym)); continue; }
            if (sym == 256) return;
            sym -= 257;
            if (sym >= 29) fail("inflate: invalid length symbol");
            size_t len = len_base[sym] + bits(len_extra[sym]);
            int ds = decode(dist);
            if (ds >= 30) fail("inflate: invalid distance symbol");
            size_t d = dist_base[ds] + bits(dist_extra[ds]);
            if (d > out_.size()) fail("inflate: distance too far back");
            size_t at = out_.size();
            out_.resize(at + len);
            uint8_t* o = out_.data();
            if (d >= len) {
                std::memcpy(o + at, o + at - d, len);
            } else {
                for (size_t k = 0; k < len; ++k) o[at + k] = o[at + k - d];   // overlapping: byte by byte
            }
        }
    }

    void fixed() {
        static const std::pair<Huffman, Huffman> tables = [] {
            uint8_t lengths[288];
            int s = 0;
            for (; s < 144; ++s) lengths[s] = 8;
            for (; s < 256; ++s) lengths[s] = 9;
            for (; s < 280; ++s) lengths[s] = 7;
            for (; s < 288; ++s) lengths[s] = 8;
            std::pair<Huffman, Huffman> t;
            build(t.first, lengths, 288);
            for (s = 0; s < 30; ++s) lengths[s] = 5;
            build(t.second, lengths, 30);
            return t;
        }();
        codes(tables.first, tables.second);
    }

    void dynamic() {
        static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        int nlen = static_cast<int>(bits(5)) + 257;
        int ndist = static_cast<int>(bits(5)) + 1;
        int ncode = static_cast<int>(bits(4)) + 4;
        if (nlen > 286 || ndist > 30) fail("inflate: bad code counts");
        uint8_t lengths[320] = {};
        for (int k = 0; k < ncode; ++k) lengths[order[k]] = static_cast<uint8_t>(bits(3));
        Huffman lencode;
        build(lencode, lengths, 19);
        int k = 0;
        while (k < nlen + ndist) {
            int sym = decode(lencode);
            if (sym < 16) { lengths[k++] = static_cast<uint8_t>(sym); continue; }
            uint8_t len = 0;
            int repeat;
            if (sym == 16) {
                if (k == 0) fail("inflate: repeat with no first length");
                len = lengths[k - 1];
                repeat = 3 + static_cast<int>(bits(2));
            } else if (sym == 17) repeat = 3 + static_cast<int>(bits(3));
            else repeat = 11 + static_cast<int>(bits(7));
            if (k + repeat > nlen + ndist) fail("inflate: too many lengths");
            while (repeat--) lengths[k++] = len;
        }
        if (lengths[256] == 0) fail("inflate: no end-of-block code");
        Huffman lit, dist;
        build(lit, lengths, nlen);
        build(dist, lengths + nlen, ndist);
        codes(lit, dist);
    }

    const uint8_t* src_;
    size_t size_;
    std::vector<uint8_t>& out_;
    size_t pos_ = 0;
    uint64_t bitbuf_ = 0;
    int bitcnt_ = 0;
};

// CRC-32 (IEEE), eight bytes a step (slicing by 8).
uint32_t crc32(const std::vector<uint8_t>& data) {
    static const auto tables = [] {
        std::array<std::array<uint32_t, 256>, 8> t{};
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[0][n] = c;
        }
        for (uint32_t n = 0; n < 256; ++n)
            for (int k = 1; k < 8; ++k) t[k][n] = (t[k - 1][n] >> 8) ^ t[0][t[k - 1][n] & 0xFF];
        return t;
    }();
    uint32_t c = 0xFFFFFFFFu;
    const uint8_t* p = data.data();
    size_t n = data.size();
    while (n >= 8) {
        uint32_t lo = c ^ (p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24));
        uint32_t hi = p[4] | (p[5] << 8) | (p[6] << 16) | (static_cast<uint32_t>(p[7]) << 24);
        c = tables[7][lo & 0xFF] ^ tables[6][(lo >> 8) & 0xFF] ^ tables[5][(lo >> 16) & 0xFF] ^ tables[4][lo >> 24] ^
            tables[3][hi & 0xFF] ^ tables[2][(hi >> 8) & 0xFF] ^ tables[1][(hi >> 16) & 0xFF] ^ tables[0][hi >> 24];
        p += 8;
        n -= 8;
    }
    while (n--) c = tables[0][(c ^ *p++) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

}  // namespace

PBytes gunzip(const PBytes& data) {
    const uint8_t* p = data.data();
    size_t n = static_cast<size_t>(data.size());
    if (n < 18 || p[0] != 0x1F || p[1] != 0x8B || p[2] != 8) fail("gunzip: not a gzip member");
    uint8_t flags = p[3];
    size_t at = 10;
    if (flags & 4) { if (at + 2 > n) fail("gunzip: truncated"); at += 2 + (p[at] | (p[at + 1] << 8)); }
    if (flags & 8) { while (at < n && p[at]) ++at; ++at; }
    if (flags & 16) { while (at < n && p[at]) ++at; ++at; }
    if (flags & 2) at += 2;
    if (at >= n) fail("gunzip: truncated header");
    PBytes out;
    // The trailer's size (mod 2^32) as a first guess of the output's.
    uint32_t hint = p[n - 4] | (p[n - 3] << 8) | (p[n - 2] << 16) | (static_cast<uint32_t>(p[n - 1]) << 24);
    out.vec().reserve(std::min<size_t>(hint, size_t{1} << 30));
    size_t end = Inflater(p, n, out.vec()).run(at);
    if (end + 8 > n) fail("gunzip: missing trailer");
    uint32_t crc = p[end] | (p[end + 1] << 8) | (p[end + 2] << 16) | (static_cast<uint32_t>(p[end + 3]) << 24);
    uint32_t isize = p[end + 4] | (p[end + 5] << 8) | (p[end + 6] << 16) | (static_cast<uint32_t>(p[end + 7]) << 24);
    if (crc != crc32(out.vec()) || isize != static_cast<uint32_t>(out.size())) fail("gunzip: CRC or size mismatch");
    return out;
}

PBytes unzlib(const PBytes& data, size_t size_hint) {
    const uint8_t* p = data.data();
    size_t n = static_cast<size_t>(data.size());
    if (n < 6 || (p[0] & 0x0F) != 8 || ((p[0] << 8) | p[1]) % 31 != 0) fail("unzlib: not a zlib stream");
    if (p[1] & 0x20) fail("unzlib: preset dictionary not supported");
    PBytes out;
    if (size_hint > 0) out.vec().reserve(std::min<size_t>(size_hint, size_t{1} << 30));
    size_t end = Inflater(p, n, out.vec()).run(2);
    if (end + 4 > n) fail("unzlib: missing Adler-32");
    // Adler-32 with the modulo taken every 5552 bytes (zlib's NMAX: the sums cannot overflow 32 bits).
    uint32_t a = 1, b = 0;
    const uint8_t* q = out.vec().data();
    size_t left = out.vec().size();
    while (left > 0) {
        size_t chunk = std::min<size_t>(left, 5552);
        left -= chunk;
        while (chunk--) {
            a += *q++;
            b += a;
        }
        a %= 65521;
        b %= 65521;
    }
    uint32_t want = (static_cast<uint32_t>(p[end]) << 24) | (p[end + 1] << 16) | (p[end + 2] << 8) | p[end + 3];
    if (((b << 16) | a) != want) fail("unzlib: Adler-32 mismatch");
    return out;
}

}  // namespace gd
