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
	unsigned char raw_key[AES_KEY_SIZE_BYTES];
	unsigned char encrypt_iv[AES_BLOCK_SIZE];
	unsigned char decrypt_iv[AES_BLOCK_SIZE];
	unsigned char decrypt_history[0x100];

	EVP_CIPHER_CTX *enc_ctx_ocb_enc;
	EVP_CIPHER_CTX *dec_ctx_ocb_enc;
	EVP_CIPHER_CTX *enc_ctx_ocb_dec;
	EVP_CIPHER_CTX *dec_ctx_ocb_dec;

	unsigned int uiGood;
	unsigned int uiLate;
	unsigned int uiLost;
	unsigned int uiResync;

	bool bInit;
};

mumble_crypt* crypt_new();
void crypt_free(mumble_crypt *crypt);

bool crypt_isValid(mumble_crypt *crypt);
void crypt_genKey(mumble_crypt *crypt);
bool crypt_setKey(mumble_crypt *crypt, ProtobufCBinaryData rkey, ProtobufCBinaryData eiv, ProtobufCBinaryData div);
bool crypt_setRawKey(mumble_crypt *crypt, ProtobufCBinaryData rkey);
bool crypt_setEncryptIV(mumble_crypt *crypt, ProtobufCBinaryData iv);
bool crypt_setDecryptIV(mumble_crypt *crypt, ProtobufCBinaryData iv);

const unsigned char* crypt_getRawKey(mumble_crypt *crypt);
const unsigned char* crypt_getEncryptIV(mumble_crypt *crypt);
const unsigned char* crypt_getDecryptIV(mumble_crypt *crypt);

unsigned int crypt_getGood(mumble_crypt *crypt);
unsigned int crypt_getLate(mumble_crypt *crypt);
unsigned int crypt_getLost(mumble_crypt *crypt);

bool crypt_ocb_encrypt(mumble_crypt *crypt, const unsigned char* plain, unsigned char* encrypted, unsigned int len, const unsigned char* nonce, unsigned char* tag, bool modifyPlainOnXEXStarAttack);
bool crypt_ocb_decrypt(mumble_crypt *crypt, const unsigned char* encrypted, unsigned char* plain, unsigned int len, const unsigned char* nonce, unsigned char* tag);

bool crypt_decrypt(mumble_crypt *crypt, const unsigned char* source, unsigned char* dst, unsigned int crypted_length);
bool crypt_encrypt(mumble_crypt *crypt, const unsigned char* source, unsigned char* dst, unsigned int plain_length);
