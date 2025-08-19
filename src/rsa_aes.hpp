#pragma once
#include <openssl/rsa.h>
#include <openssl/evp.h>
#include <openssl/bn.h>
#include <span>
namespace libvnc {

class rsa_aes {
public:
	static EVP_PKEY *make_rsa_pubkey(const std::span<uint8_t> &modulus, const std::span<uint8_t> &exponent)
	{
		BIGNUM *n = BN_bin2bn(modulus.data(), (int)modulus.size(), nullptr);
		BIGNUM *e = BN_bin2bn(exponent.data(), (int)exponent.size(), nullptr);

		if (!n || !e) {
			BN_free(n);
			BN_free(e);
			return nullptr;
		}

		RSA *rsa = RSA_new();
		if (!RSA_set0_key(rsa, n, e, nullptr)) {
			RSA_free(rsa);
			BN_free(n);
			BN_free(e);
			return nullptr;
		}

		EVP_PKEY *pkey = EVP_PKEY_new();
		if (!EVP_PKEY_assign_RSA(pkey, rsa)) {
			EVP_PKEY_free(pkey);
			RSA_free(rsa);
			return nullptr;
		}

		return pkey;
	}
	static void extract_rsa_pubkey(EVP_PKEY *pkey, std::vector<uint8_t> &modulus, std::vector<uint8_t> &exponent)
	{
		RSA *rsa = EVP_PKEY_get1_RSA(pkey);
		const BIGNUM *n = nullptr;
		const BIGNUM *e = nullptr;
		RSA_get0_key(rsa, &n, &e, nullptr);

		int mod_len = RSA_size(rsa);

		modulus.assign(mod_len, 0);
		exponent.assign(mod_len, 0);

		BN_bn2binpad(n, modulus.data(), mod_len);
		BN_bn2binpad(e, exponent.data(), mod_len);

		RSA_free(rsa);
	}
	static EVP_PKEY *generate_rsa_key(int bits = 2048, unsigned long pubexp = RSA_F4)
	{
		EVP_PKEY *pkey = nullptr;

		std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(
			EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr),
										EVP_PKEY_CTX_free);
		if (!ctx) {
			ERR_print_errors_fp(stderr);
			return nullptr;
		}

		if (EVP_PKEY_keygen_init(ctx.get()) <= 0) {
			ERR_print_errors_fp(stderr);
			return nullptr;
		}

		if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx.get(), bits) <= 0) {
			ERR_print_errors_fp(stderr);
			return nullptr;
		}

		BIGNUM *e = BN_new();
		if (!e) {
			return nullptr;
		}

		BN_set_word(e, pubexp);
		if (EVP_PKEY_CTX_set_rsa_keygen_pubexp(ctx.get(), e) <= 0) {
			BN_free(e);
			ERR_print_errors_fp(stderr);
			return nullptr;
		}
		BN_free(e);

		if (EVP_PKEY_keygen(ctx.get(), &pkey) <= 0) {
			ERR_print_errors_fp(stderr);
			return nullptr;
		}
		return pkey;
	}

	static bool rsa_encrypt(EVP_PKEY *pubkey, const std::vector<uint8_t> &plaintext,
				std::vector<uint8_t> &ciphertext)
	{
		if (!pubkey)
			return false;

		std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(EVP_PKEY_CTX_new(pubkey, nullptr),
										EVP_PKEY_CTX_free);
		if (!ctx)
			return false;

		if (EVP_PKEY_encrypt_init(ctx.get()) <= 0)
			return false;

		size_t outlen = 0;
		if (EVP_PKEY_encrypt(ctx.get(), nullptr, &outlen, plaintext.data(), plaintext.size()) <= 0)
			return false;

		ciphertext.resize(outlen);

		if (EVP_PKEY_encrypt(ctx.get(), ciphertext.data(), &outlen, plaintext.data(), plaintext.size()) <= 0)
			return false;

		ciphertext.resize(outlen);
		return true;
	}

	static bool rsa_decrypt(EVP_PKEY *privkey, const std::vector<uint8_t> &ciphertext,
				std::vector<uint8_t> &plaintext)
	{
		if (!privkey)
			return false;

		std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(EVP_PKEY_CTX_new(privkey, nullptr),
										EVP_PKEY_CTX_free);
		if (!ctx)
			return false;

		if (EVP_PKEY_decrypt_init(ctx.get()) <= 0)
			return false;

		size_t outlen = 0;
		if (EVP_PKEY_decrypt(ctx.get(), nullptr, &outlen, ciphertext.data(), ciphertext.size()) <= 0)
			return false;

		plaintext.resize(outlen);

		if (EVP_PKEY_decrypt(ctx.get(), plaintext.data(), &outlen, ciphertext.data(), ciphertext.size()) <= 0)
			return false;

		plaintext.resize(outlen);
		return true;
	}
};
} // namespace libvnc