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
            dec_ = Botan::AEAD_Mode::create_or_throw("AES-128/EAX", Botan::Cipher_Dir::DECRYPTION);
            enc_ = Botan::AEAD_Mode::create_or_throw("AES-128/EAX", Botan::Cipher_Dir::Encryption);
         } else {
            dec_ = Botan::AEAD_Mode::create_or_throw("AES-256/EAX", Botan::Cipher_Dir::DECRYPTION);
            enc_ = Botan::AEAD_Mode::create_or_throw("AES-256/EAX", Botan::Cipher_Dir::Encryption);
         }
         dec_->set_key(dec_key);
         enc_->set_key(enc_key);
      }

   public:
      std::vector<std::uint8_t> encrypt(const std::uint8_t* plain, std::size_t len) override {
         try {
            boost::endian::big_uint16_buf_t data_len;
            data_len = len;

            std::vector<uint8_t> buf(AadSize + len);
            std::memcpy(buf.data(), &data_len, sizeof(data_len));
            std::memcpy(buf.data() + AadSize, plain, len);

            enc_->set_associated_data(buf.data(), AadSize);
            enc_->start(enc_counter_, sizeof(enc_counter_));
            // Update nonce by incrementing the counter as a
            // 128bit little endian unsigned integer
            for(int i = 0; i < 16; ++i) {
               // increment until there is no carry
               if(++enc_counter_[i] != 0) {
                  break;
               }
            }
           
            enc_->finish(buf, AadSize);
            return buf;

         } catch(const std::exception& e) {
            spdlog::error(e.what());
         }
         return {};
      }

      std::vector<std::uint8_t> decrypt(const std::uint8_t* data, std::size_t len) override {
         try {
            boost::asio::buffer_copy(decrypt_buffer_.prepare(len), boost::asio::buffer(data, len));
            decrypt_buffer_.commit(len);

            if(decrypt_buffer_.size() < 2)
               return {};

            auto buffer = decrypt_buffer_.data();

            auto data_len = *static_cast<const boost::endian::big_uint16_buf_t*>(buffer.data());
            auto pakage_len = data_len.value() + AadSize + dec_->tag_size();
            if(decrypt_buffer_.size() < pakage_len)
               return {};

            buffer += 2;
            std::vector<std::uint8_t> buf(pakage_len - 2);
            boost::asio::buffer_copy(boost::asio::buffer(buf), buffer, buf.size());

            
            dec_->set_associated_data(data_len.data(), AadSize);
            dec_->start(dec_counter_, sizeof(dec_counter_));

            // Update nonce by incrementing the counter as a
            // 128bit little endian unsigned integer
            for(int i = 0; i < 16; ++i) {
               // increment until there is no carry
               if(++dec_counter_[i] != 0) {
                  break;
               }
            }

            dec_->finish(buf);
            return buf;
         } catch(const std::exception& e) {
            spdlog::error(e.what());
         }
         return {};
      }

   private:
      Botan::AutoSeeded_RNG rng_;
      std::unique_ptr<Botan::AEAD_Mode> dec_;
      std::unique_ptr<Botan::AEAD_Mode> enc_;

      constexpr static const int AadSize = 2;

      uint8_t enc_counter_[16] = {0};
      uint8_t dec_counter_[16] = {0};

      boost::asio::streambuf decrypt_buffer_;
};
}  // namespace libvnc