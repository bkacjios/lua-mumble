// Copyright 2020-2021 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#pragma once
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <stdbool.h>
#include <protobuf-c/protobuf-c.h>

#define AES_KEY_SIZE_BITS   128
#define AES_KEY_SIZE_BYTES  (AES_KEY_SIZE_BITS/8)

typedef struct mumble_crypt mumble_crypt;

struct mumble_crypt {
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

mumble_crypt* crypt_new();
void crypt_init(mumble_crypt *crypt);
void crypt_uninitialize(mumble_crypt *crypt);
void crypt_free(mumble_crypt *crypt);

bool crypt_isValid(mumble_crypt *crypt);
void crypt_genKey(mumble_crypt *crypt);

bool crypt_setKey(mumble_crypt *crypt, const uint8_t *rkey, size_t rkey_len,
                  const uint8_t *eiv, size_t eiv_len,
                  const uint8_t *div, size_t div_len);

bool crypt_setRawKey(mumble_crypt *crypt, const uint8_t *rkey, size_t rkey_len);

bool crypt_setEncryptIV(mumble_crypt *crypt, const uint8_t *iv, size_t iv_len);

bool crypt_setDecryptIV(mumble_crypt *crypt, const uint8_t *iv, size_t iv_len);

const uint8_t* crypt_getRawKey(mumble_crypt *crypt);
const uint8_t* crypt_getEncryptIV(mumble_crypt *crypt);
const uint8_t* crypt_getDecryptIV(mumble_crypt *crypt);

size_t crypt_getGood(mumble_crypt *crypt);
size_t crypt_getLate(mumble_crypt *crypt);
size_t crypt_getLost(mumble_crypt *crypt);

bool crypt_ocb_encrypt(mumble_crypt *crypt, const uint8_t* plain, uint8_t* encrypted, size_t len, const uint8_t* nonce, uint8_t* tag, bool modifyPlainOnXEXStarAttack);
bool crypt_ocb_decrypt(mumble_crypt *crypt, const uint8_t* encrypted, uint8_t* plain, size_t len, const uint8_t* nonce, uint8_t* tag);

bool crypt_decrypt(mumble_crypt *crypt, const uint8_t* source, uint8_t* dst, size_t crypted_length);
bool crypt_encrypt(mumble_crypt *crypt, const uint8_t* source, uint8_t* dst, size_t plain_length);
