// Copyright 2020-2021 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#pragma once

#include <cstddef>
#include <cstdint>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <optional>
#include <string>

#define AES_KEY_SIZE_BITS   128
#define AES_KEY_SIZE_BYTES  (AES_KEY_SIZE_BITS/8)

class CryptState {
public:
	CryptState();
	~CryptState();

	CryptState(const CryptState&) = delete;
	CryptState& operator=(const CryptState&) = delete;

	bool isValid() const;
	void genKey();
	bool setKey(const uint8_t *rkey, size_t rkey_len,
	            const uint8_t *eiv, size_t eiv_len,
	            const uint8_t *div, size_t div_len);
	bool setRawKey(const uint8_t *rkey, size_t rkey_len);
	bool setEncryptIV(const uint8_t *iv, size_t iv_len);
	bool setDecryptIV(const uint8_t *iv, size_t iv_len);

	const uint8_t* getRawKey() const;
	const uint8_t* getEncryptIV() const;
	const uint8_t* getDecryptIV() const;

	size_t getGood() const;
	size_t getLate() const;
	size_t getLost() const;

	bool decrypt(const uint8_t* source, uint8_t* dst, size_t crypted_length);
	bool encrypt(const uint8_t* source, uint8_t* dst, size_t plain_length);

	// Encrypt or decrypt a whole packet, returning nothing on failure
	std::optional<std::string> encrypt(const uint8_t* source, size_t plain_length);
	std::optional<std::string> decrypt(const uint8_t* source, size_t crypted_length);

private:
	bool ocbEncrypt(const uint8_t* plain, uint8_t* encrypted, size_t len, const uint8_t* nonce, uint8_t* tag, bool modifyPlainOnXEXStarAttack);
	bool ocbDecrypt(const uint8_t* encrypted, uint8_t* plain, size_t len, const uint8_t* nonce, uint8_t* tag);

	uint8_t raw_key[AES_KEY_SIZE_BYTES];
	uint8_t encrypt_iv[AES_BLOCK_SIZE];
	uint8_t decrypt_iv[AES_BLOCK_SIZE];
	uint8_t decrypt_history[0x100];
	EVP_CIPHER_CTX *enc_ctx_ocb_enc;
	EVP_CIPHER_CTX *dec_ctx_ocb_enc;
	EVP_CIPHER_CTX *enc_ctx_ocb_dec;
	EVP_CIPHER_CTX *dec_ctx_ocb_dec;
	size_t uiGood;
	size_t uiLate;
	size_t uiLost;
	size_t uiResync;
	bool bInit;
};
