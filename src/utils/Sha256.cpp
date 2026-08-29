#include "utils/Sha256.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <span>

namespace arc_helper::crypto {
namespace {

constexpr std::array<uint32_t, 64> K = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2,
};

uint32_t Ror(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

class Sha256 {
public:
    // Spanning the input collapses the pointer/length pair the caller used to
    // advance by hand: consuming n bytes is one subspan, not two assignments.
    void Update(std::span<const uint8_t> data) {
        total_ += data.size();
        while (!data.empty()) {
            const size_t n = std::min(data.size(), block_.size() - used_);
            std::memcpy(block_.data() + used_, data.data(), n);
            used_ += n;
            data = data.subspan(n);
            if (used_ == block_.size()) {
                Compress(block_);
                used_ = 0;
            }
        }
    }

    std::array<uint8_t, 32> Finish() {
        const uint64_t bits = static_cast<uint64_t>(total_) * 8;
        block_[used_++] = 0x80;
        if (used_ > 56) {
            std::fill(block_.begin() + used_, block_.end(), 0);
            Compress(block_);
            used_ = 0;
        }
        std::fill(block_.begin() + used_, block_.begin() + 56, 0);
        for (int i = 0; i < 8; ++i) block_[56 + i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
        Compress(block_);
        std::array<uint8_t, 32> out{};
        for (size_t i = 0; i < state_.size(); ++i) {
            out[i * 4] = static_cast<uint8_t>(state_[i] >> 24);
            out[i * 4 + 1] = static_cast<uint8_t>(state_[i] >> 16);
            out[i * 4 + 2] = static_cast<uint8_t>(state_[i] >> 8);
            out[i * 4 + 3] = static_cast<uint8_t>(state_[i]);
        }
        return out;
    }

private:
    // A fixed-extent span states the 64-byte requirement in the type, so the
    // block cannot be under-sized by accident the way a bare pointer allowed.
    void Compress(std::span<const uint8_t, 64> p) {
        uint32_t w[64]{};
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(p[i * 4]) << 24) |
                   (static_cast<uint32_t>(p[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(p[i * 4 + 2]) << 8) |
                   static_cast<uint32_t>(p[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = Ror(w[i - 15], 7) ^ Ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = Ror(w[i - 2], 17) ^ Ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a=state_[0],b=state_[1],c=state_[2],d=state_[3],e=state_[4],f=state_[5],g=state_[6],h=state_[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t s1 = Ror(e,6)^Ror(e,11)^Ror(e,25);
            const uint32_t ch = (e&f)^((~e)&g);
            const uint32_t t1 = h+s1+ch+K[i]+w[i];
            const uint32_t s0 = Ror(a,2)^Ror(a,13)^Ror(a,22);
            const uint32_t maj = (a&b)^(a&c)^(b&c);
            const uint32_t t2 = s0+maj;
            h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        state_[0]+=a; state_[1]+=b; state_[2]+=c; state_[3]+=d;
        state_[4]+=e; state_[5]+=f; state_[6]+=g; state_[7]+=h;
    }

    std::array<uint32_t, 8> state_{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::array<uint8_t, 64> block_{};
    size_t used_ = 0;
    size_t total_ = 0;
};

std::string ToHex(const std::array<uint8_t, 32> &digest) {
    constexpr char hex[] = "0123456789abcdef";
    std::string out(64, '0');
    for (size_t i = 0; i < digest.size(); ++i) {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 15];
    }
    return out;
}

} // namespace

std::string Sha256Hex(const void *data, size_t size) {
    Sha256 sha;
    sha.Update({static_cast<const uint8_t *>(data), size});
    return ToHex(sha.Finish());
}

std::string Sha256FileHex(const std::string &path, std::string *error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error) *error = "open failed";
        return {};
    }
    Sha256 sha;
    std::array<uint8_t, 64 * 1024> buffer{};
    while (file) {
        file.read(reinterpret_cast<char *>(buffer.data()), buffer.size());
        const auto count = file.gcount();
        if (count > 0) sha.Update({buffer.data(), static_cast<size_t>(count)});
    }
    if (!file.eof()) {
        if (error) *error = "read failed";
        return {};
    }
    return ToHex(sha.Finish());
}

} // namespace arc_helper::crypto
