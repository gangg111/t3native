#include "gd/io.h"

#include <array>
#include <cstdio>

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

struct Huffman {
    std::array<uint16_t, 16> count{};   // codes per length
    std::array<uint16_t, 288> symbol{};  // symbols ordered by code
};

class Inflater {
public:
    Inflater(const uint8_t* src, size_t size, std::vector<uint8_t>& out) : src_(src), size_(size), out_(out) {}

    size_t run(size_t at) {
        pos_ = at;
        int last;
        do {
            last = bits(1);
            int type = bits(2);
            if (type == 0) stored();
            else if (type == 1) fixed();
            else if (type == 2) dynamic();
            else fail("inflate: invalid block type");
        } while (!last);
        return pos_;   // the byte after the deflate stream (partial byte dropped)
    }

private:
    int bits(int need) {
        int v = bitbuf_;
        while (bitcnt_ < need) {
            if (pos_ >= size_) fail("inflate: unexpected end of data");
            v |= static_cast<int>(src_[pos_++]) << bitcnt_;
            bitcnt_ += 8;
        }
        bitbuf_ = v >> need;
        bitcnt_ -= need;
        return v & ((1 << need) - 1);
    }

    void stored() {
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
        for (int s = 0; s < n; ++s) h.count[lengths[s]]++;
        h.count[0] = 0;
        std::array<uint16_t, 16> offs{};
        for (int len = 1; len < 16; ++len) offs[len] = static_cast<uint16_t>(offs[len - 1] + h.count[len - 1]);
        for (int s = 0; s < n; ++s)
            if (lengths[s]) h.symbol[offs[lengths[s]]++] = static_cast<uint16_t>(s);
    }

    int decode(const Huffman& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len) {
            code |= bits(1);
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
            size_t from = out_.size() - d;
            for (size_t k = 0; k < len; ++k) out_.push_back(out_[from + k]);
        }
    }

    void fixed() {
        uint8_t lengths[288 + 30];
        int s = 0;
        for (; s < 144; ++s) lengths[s] = 8;
        for (; s < 256; ++s) lengths[s] = 9;
        for (; s < 280; ++s) lengths[s] = 7;
        for (; s < 288; ++s) lengths[s] = 8;
        Huffman lit, dist;
        build(lit, lengths, 288);
        for (s = 0; s < 30; ++s) lengths[s] = 5;
        build(dist, lengths, 30);
        codes(lit, dist);
    }

    void dynamic() {
        static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        int nlen = bits(5) + 257;
        int ndist = bits(5) + 1;
        int ncode = bits(4) + 4;
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
                repeat = 3 + bits(2);
            } else if (sym == 17) repeat = 3 + bits(3);
            else repeat = 11 + bits(7);
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
    int bitbuf_ = 0;
    int bitcnt_ = 0;
};

uint32_t crc32(const std::vector<uint8_t>& data) {
    static std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    uint32_t c = 0xFFFFFFFFu;
    for (uint8_t b : data) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
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
    size_t end = Inflater(p, n, out.vec()).run(at);
    if (end + 8 > n) fail("gunzip: missing trailer");
    uint32_t crc = p[end] | (p[end + 1] << 8) | (p[end + 2] << 16) | (static_cast<uint32_t>(p[end + 3]) << 24);
    uint32_t isize = p[end + 4] | (p[end + 5] << 8) | (p[end + 6] << 16) | (static_cast<uint32_t>(p[end + 7]) << 24);
    if (crc != crc32(out.vec()) || isize != static_cast<uint32_t>(out.size())) fail("gunzip: CRC or size mismatch");
    return out;
}

PBytes unzlib(const PBytes& data) {
    const uint8_t* p = data.data();
    size_t n = static_cast<size_t>(data.size());
    if (n < 6 || (p[0] & 0x0F) != 8 || ((p[0] << 8) | p[1]) % 31 != 0) fail("unzlib: not a zlib stream");
    if (p[1] & 0x20) fail("unzlib: preset dictionary not supported");
    PBytes out;
    size_t end = Inflater(p, n, out.vec()).run(2);
    if (end + 4 > n) fail("unzlib: missing Adler-32");
    uint32_t a = 1, b = 0;
    for (uint8_t byte : out.vec()) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    uint32_t want = (static_cast<uint32_t>(p[end]) << 24) | (p[end + 1] << 16) | (p[end + 2] << 8) | p[end + 3];
    if (((b << 16) | a) != want) fail("unzlib: Adler-32 mismatch");
    return out;
}

}  // namespace gd
