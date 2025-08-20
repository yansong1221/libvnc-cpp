#pragma once
#include <span>
#include "stream/variant_stream.hpp"
#include <botan/rsa.h>
#include <botan/auto_rng.h>
#include <botan/aead.h>
#include <botan/symkey.h>
#include <botan/pubkey.h>
#include <botan/hex.h>
#include <botan/pk_keys.h>
#include <botan/pkcs8.h>
#include <botan/rng.h>
#include <botan/hash.h>
#include <botan/block_cipher.h>

namespace libvnc {

class aes_crypto_provider : public crypto_provider {
public:
	aes_crypto_provider(int keySize, const Botan::SymmetricKey &enc_key, const Botan::SymmetricKey &dec_key)
	{

		if (keySize == 128) {

			dec_ = Botan::BlockCipher::create("AES-128");
			enc_ = Botan::BlockCipher::create("AES-128");
		} else {
			dec_ = Botan::BlockCipher::create("AES-256");
			enc_ = Botan::BlockCipher::create("AES-256");
		}
		dec_->set_key(dec_key);
		enc_->set_key(enc_key);
	}

public:
	std::vector<std::uint8_t> encrypt(const std::uint8_t *plain, std::size_t len) override
	{
		try {
			std::vector<uint8_t> buf;
			buf.assign(plain, plain + len);
			enc_->encrypt(buf);
			return buf;

		} catch (const std::exception &e) {
			spdlog::error(e.what());
		}
		return {};
	}

	std::vector<std::uint8_t> decrypt(const std::uint8_t *data, std::size_t len) override
	{
		std::vector<uint8_t> buf;
		buf.assign(data, data + len);
		dec_->decrypt(buf);
		return buf;
	}

private:
	std::unique_ptr<Botan::BlockCipher> dec_;
	std::unique_ptr<Botan::BlockCipher> enc_;
};
} // namespace libvnc