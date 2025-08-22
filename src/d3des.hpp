#pragma once
#include <botan/block_cipher.h>
#include <cstdint>
#include <span>
#include <string>

namespace libvnc::des {

static std::vector<uint8_t> make_key(std::span<const uint8_t> key) {
   auto new_key = key | std::views::take(8) | std::views::transform([](uint8_t b) {
                     b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
                     b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
                     b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
                     return b;
                  }) |
                  std::ranges::to<std::vector>();

   new_key.resize(8);
   return new_key;
}

static void encrypt_bytes_v1(std::string_view password, std::span<uint8_t> challenge) {
   if(challenge.size() != 16)
      throw std::runtime_error("Challenge It must be a 16 bytes");

   auto des_key = make_key({(const uint8_t*)password.data(), password.size()});

   auto enc = Botan::BlockCipher::create_or_throw("DES");
   enc->set_key(des_key);

   while(!challenge.empty()) {
      enc->encrypt(challenge.subspan(0, 8));
      challenge = challenge.subspan(8);
   }
}

static void encrypt_bytes_v2(std::span<const uint8_t> key, std::span<uint8_t> content) {
   if(content.size() % 8 != 0)
      throw std::runtime_error("Content must be a multiple of 8 bytes");

   auto des_key = make_key(key);
   auto enc = Botan::BlockCipher::create_or_throw("DES");

   enc->set_key(des_key);

   auto xor_encrypt_block = [&](size_t offset, std::span<const uint8_t> xor_with) {
      for(size_t i = 0; i < 8; ++i)
         content[offset + i] ^= xor_with[i];
      enc->encrypt(content.subspan(offset, 8));
   };

   xor_encrypt_block(0, key);

   for(size_t offset = 8; offset < content.size(); offset += 8)
      xor_encrypt_block(offset, content.subspan(offset - 8, 8));
}

}  // namespace libvnc::des