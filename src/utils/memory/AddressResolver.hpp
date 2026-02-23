#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace arc_autoplay::mem {

class AddressResolver {
public:
    explicit AddressResolver(uintptr_t lib_base = 0) : lib_base_(lib_base) {}

    void SetLibBase(uintptr_t lib_base) { lib_base_ = lib_base; }
    uintptr_t GetLibBase() const { return lib_base_; }

    uintptr_t ResolveBySignature(uintptr_t hint_offset,
                                 const uint8_t *sig,
                                 size_t sig_len,
                                 std::string_view soname) const;

private:
    uintptr_t lib_base_ = 0;
};

} // namespace arc_autoplay::mem
