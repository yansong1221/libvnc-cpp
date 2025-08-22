#pragma once
#include "stream/variant_stream.hpp"
#include <botan/aead.h>
#include <botan/auto_rng.h>
#include <botan/block_cipher.h>
#include <botan/hash.h>
#include <botan/hex.h>
#include <botan/pk_keys.h>
#include <botan/pkcs8.h>
#include <botan/pubkey.h>
#include <botan/rng.h>
#include <botan/rsa.h>
#include <botan/symkey.h>
#include <span>
#include <spdlog/spdlog.h>

namespace libvnc {

class aes_crypto_provider : public crypto_provider {
   public:
      aes_crypto_provider(int keySize, const Botan::SymmetricKey& enc_key, const Botan::SymmetricKey& dec_key) {
         if(keySize == 128) {
            dec_ = Botan::AEAD_Mode::create_or_throw("AES-128/EAX", Botan::Cipher_Dir::Decryption);
            enc_ = Botan::AEAD_Mode::create_or_throw("AES-128/EAX", Botan::Cipher_Dir::Encryption);
         } else {
            dec_ = Botan::AEAD_Mode::create_or_throw("AES-256/EAX", Botan::Cipher_Dir::Decryption);
            enc_ = Botan::AEAD_Mode::create_or_throw("AES-256/EAX", Botan::Cipher_Dir::Encryption);
         }
         dec_->set_key(dec_key);
         enc_->set_key(enc_key);
      }

   public:
      std::vector<std::uint8_t> encrypt(std::span<const std::uint8_t> plain) override {
         constexpr auto uint16_max = std::numeric_limits<uint16_t>::max();

         std::vector<std::uint8_t> result;
         boost::endian::big_uint16_buf_t data_len;

         while(!plain.empty()) {
            data_len = std::min<std::size_t>(uint16_max, plain.size());

            std::vector<uint8_t> buf(sizeof(uint16_t) + data_len.value());
            std::memcpy(buf.data(), &data_len, sizeof(data_len));
            std::memcpy(buf.data() + sizeof(uint16_t), plain.data(), data_len.value());

            enc_->set_associated_data(buf.data(), sizeof(uint16_t));
            enc_->start(enc_counter_);
            // Update nonce by incrementing the counter as a
            // 128bit little endian unsigned integer
            for(int i = 0; i < 16; ++i) {
               // increment until there is no carry
               if(++enc_counter_[i] != 0) {
                  break;
               }
            }
            enc_->finish(buf, sizeof(uint16_t));
            std::copy(buf.begin(), buf.end(), std::back_inserter(result));

            plain = plain.subspan(data_len.value());
         }
         return result;
      }

      std::vector<std::uint8_t> decrypt(std::span<const std::uint8_t> data) override {
         boost::asio::buffer_copy(decrypt_buffer_.prepare(data.size()), boost::asio::buffer(data));
         decrypt_buffer_.commit(data.size());

         std::vector<std::uint8_t> result;

         while(decrypt_buffer_.size() >= 2) {
            auto buffer = decrypt_buffer_.data();

            auto data_len = *static_cast<const boost::endian::big_uint16_buf_t*>(buffer.data());
            auto pakage_len = data_len.value() + sizeof(uint16_t) + dec_->tag_size();
            if(decrypt_buffer_.size() < pakage_len)
               break;

            buffer += sizeof(uint16_t);
            std::vector<std::uint8_t> buf(pakage_len - sizeof(uint16_t));
            boost::asio::buffer_copy(boost::asio::buffer(buf), buffer, buf.size());
            decrypt_buffer_.consume(pakage_len);

            dec_->set_associated_data(data_len.data(), sizeof(data_len));
            dec_->start(dec_counter_);

            // Update nonce by incrementing the counter as a
            // 128bit little endian unsigned integer
            for(int i = 0; i < 16; ++i) {
               // increment until there is no carry
               if(++dec_counter_[i] != 0) {
                  break;
               }
            }

            dec_->finish(buf);
            std::copy(buf.begin(), buf.end(), std::back_inserter(result));
         }
         return result;
      }

   private:
      std::unique_ptr<Botan::AEAD_Mode> dec_;
      std::unique_ptr<Botan::AEAD_Mode> enc_;

      std::array<uint8_t, 16> enc_counter_ = {};
      std::array<uint8_t, 16> dec_counter_ = {};

      boost::asio::streambuf decrypt_buffer_;
};
}  // namespace libvnc