// Copyright 2020-2023 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

/*
 * This code implements OCB-AES128.
 * The algorithm design was dedicated to the public domain.
 * https://www.cs.ucdavis.edu/~rogaway/ocb/license.htm
 * https://www.cs.ucdavis.edu/~rogaway/ocb/ocb-faq.htm
 */

#include "ocb.h"

#include <arpa/inet.h>
#include <openssl/rand.h>
#include <stdint.h>
#include <string.h>

mumble_crypt* crypt_new() {
	mumble_crypt* crypt = malloc(sizeof(mumble_crypt));
	if (crypt == NULL) return crypt;
	crypt_init(crypt);
	return crypt;
}

void crypt_init(mumble_crypt *crypt) {
	for (int i = 0; i < 0x100; i++)
		crypt->decrypt_history[i] = 0;

	crypt->bInit = false;
	memset(crypt->raw_key, 0, AES_KEY_SIZE_BYTES);
	memset(crypt->encrypt_iv, 0, AES_BLOCK_SIZE);
	memset(crypt->decrypt_iv, 0, AES_BLOCK_SIZE);
	crypt->uiGood = crypt->uiLate = crypt->uiLost = crypt->uiResync = 0;

	crypt->enc_ctx_ocb_enc = EVP_CIPHER_CTX_new();
	crypt->dec_ctx_ocb_enc = EVP_CIPHER_CTX_new();
	crypt->enc_ctx_ocb_dec = EVP_CIPHER_CTX_new();
	crypt->dec_ctx_ocb_dec = EVP_CIPHER_CTX_new();
}

void crypt_uninitialize(mumble_crypt *crypt) {
	EVP_CIPHER_CTX_free(crypt->enc_ctx_ocb_enc);
	EVP_CIPHER_CTX_free(crypt->dec_ctx_ocb_enc);
	EVP_CIPHER_CTX_free(crypt->enc_ctx_ocb_dec);
	EVP_CIPHER_CTX_free(crypt->dec_ctx_ocb_dec);
	crypt->bInit = false;
}

void crypt_free(mumble_crypt *crypt) {
	crypt_uninitialize(crypt);;
	free(crypt);
}

bool crypt_isValid(mumble_crypt *crypt) {
	return crypt->bInit;
}

void crypt_genKey(mumble_crypt *crypt) {
	RAND_bytes(crypt->raw_key, AES_KEY_SIZE_BYTES);
	RAND_bytes(crypt->encrypt_iv, AES_BLOCK_SIZE);
	RAND_bytes(crypt->decrypt_iv, AES_BLOCK_SIZE);
	crypt->bInit = true;
}

bool crypt_setKey(mumble_crypt *crypt, const uint8_t *rkey, size_t rkey_len,
                  const uint8_t *eiv, size_t eiv_len,
                  const uint8_t *div, size_t div_len) {
	if (rkey_len == AES_KEY_SIZE_BYTES && eiv_len == AES_BLOCK_SIZE && div_len == AES_BLOCK_SIZE) {
		memcpy(crypt->raw_key, rkey, AES_KEY_SIZE_BYTES);
		memcpy(crypt->encrypt_iv, eiv, AES_BLOCK_SIZE);
		memcpy(crypt->decrypt_iv, div, AES_BLOCK_SIZE);
		crypt->bInit = true;
		return true;
	}
	return false;
}

bool crypt_setRawKey(mumble_crypt *crypt, const uint8_t *rkey, size_t rkey_len) {
	if (rkey_len == AES_KEY_SIZE_BYTES) {
		memcpy(crypt->raw_key, rkey, AES_KEY_SIZE_BYTES);
		return true;
	}
	return false;
}

bool crypt_setEncryptIV(mumble_crypt *crypt, const uint8_t *iv, size_t iv_len) {
	if (iv_len == AES_BLOCK_SIZE) {
		memcpy(crypt->encrypt_iv, iv, AES_BLOCK_SIZE);
		return true;
	}
	return false;
}

bool crypt_setDecryptIV(mumble_crypt *crypt, const uint8_t *iv, size_t iv_len) {
	if (iv_len == AES_BLOCK_SIZE) {
		memcpy(crypt->decrypt_iv, iv, AES_BLOCK_SIZE);
		return true;
	}
	return false;
}

const uint8_t* crypt_getRawKey(mumble_crypt *crypt) {
	return crypt->raw_key;
}

const uint8_t* crypt_getEncryptIV(mumble_crypt *crypt) {
	return crypt->encrypt_iv;
}

const uint8_t* crypt_getDecryptIV(mumble_crypt *crypt) {
	return crypt->decrypt_iv;
}

size_t crypt_getGood(mumble_crypt *crypt) {
	return crypt->uiGood;
}

size_t crypt_getLate(mumble_crypt *crypt) {
	return crypt->uiLate;
}

size_t crypt_getLost(mumble_crypt *crypt) {
	return crypt->uiLost;
}

bool crypt_encrypt(mumble_crypt *crypt, const uint8_t *source, uint8_t *dst, size_t plain_length) {
	uint8_t tag[AES_BLOCK_SIZE];

	// First, increase our IV.
	for (int i = 0; i < AES_BLOCK_SIZE; i++)
		if (++crypt->encrypt_iv[i])
			break;

	if (!crypt_ocb_encrypt(crypt, source, dst + 4, plain_length, crypt->encrypt_iv, tag, true)) {
		return false;
	}

	dst[0] = crypt->encrypt_iv[0];
	dst[1] = tag[0];
	dst[2] = tag[1];
	dst[3] = tag[2];
	return true;
}

bool crypt_decrypt(mumble_crypt *crypt, const uint8_t *source, uint8_t *dst, size_t crypted_length) {
	if (crypted_length < 4)
		return false;

	size_t plain_length = crypted_length - 4;

	uint8_t saveiv[AES_BLOCK_SIZE];
	uint8_t ivbyte = source[0];
	bool restore         = false;
	uint8_t tag[AES_BLOCK_SIZE];

	int lost = 0;
	int late = 0;

	memcpy(saveiv, crypt->decrypt_iv, AES_BLOCK_SIZE);

	if (((crypt->decrypt_iv[0] + 1) & 0xFF) == ivbyte) {
		// In order as expected.
		if (ivbyte > crypt->decrypt_iv[0]) {
			crypt->decrypt_iv[0] = ivbyte;
		} else if (ivbyte < crypt->decrypt_iv[0]) {
			crypt->decrypt_iv[0] = ivbyte;
			for (int i = 1; i < AES_BLOCK_SIZE; i++)
				if (++crypt->decrypt_iv[i])
					break;
		} else {
			return false;
		}
	} else {
		// This is either out of order or a repeat.

		int diff = ivbyte - crypt->decrypt_iv[0];
		if (diff > 128)
			diff = diff - 256;
		else if (diff < -128)
			diff = diff + 256;

		if ((ivbyte < crypt->decrypt_iv[0]) && (diff > -30) && (diff < 0)) {
			// Late packet, but no wraparound.
			late          = 1;
			lost          = -1;
			crypt->decrypt_iv[0] = ivbyte;
			restore       = true;
		} else if ((ivbyte > crypt->decrypt_iv[0]) && (diff > -30) && (diff < 0)) {
			// Last was 0x02, here comes 0xff from last round
			late          = 1;
			lost          = -1;
			crypt->decrypt_iv[0] = ivbyte;
			for (int i = 1; i < AES_BLOCK_SIZE; i++)
				if (crypt->decrypt_iv[i]--)
					break;
			restore = true;
		} else if ((ivbyte > crypt->decrypt_iv[0]) && (diff > 0)) {
			// Lost a few packets, but beyond that we're good.
			lost          = ivbyte - crypt->decrypt_iv[0] - 1;
			crypt->decrypt_iv[0] = ivbyte;
		} else if ((ivbyte < crypt->decrypt_iv[0]) && (diff > 0)) {
			// Lost a few packets, and wrapped around
			lost          = 256 - crypt->decrypt_iv[0] + ivbyte - 1;
			crypt->decrypt_iv[0] = ivbyte;
			for (int i = 1; i < AES_BLOCK_SIZE; i++)
				if (++crypt->decrypt_iv[i])
					break;
		} else {
			return false;
		}

		if (crypt->decrypt_history[crypt->decrypt_iv[0]] == crypt->decrypt_iv[1]) {
			memcpy(crypt->decrypt_iv, saveiv, AES_BLOCK_SIZE);
			return false;
		}
	}

	bool ocb_success = crypt_ocb_decrypt(crypt, source + 4, dst, plain_length, crypt->decrypt_iv, tag);

	if (!ocb_success || memcmp(tag, source + 1, 3) != 0) {
		memcpy(crypt->decrypt_iv, saveiv, AES_BLOCK_SIZE);
		return false;
	}
	crypt->decrypt_history[crypt->decrypt_iv[0]] = crypt->decrypt_iv[1];

	if (restore)
		memcpy(crypt->decrypt_iv, saveiv, AES_BLOCK_SIZE);

	crypt->uiGood++;
	// crypt->uiLate += late, but we have to make sure we don't cause wrap-arounds on the unsigned lhs
	if (late > 0) {
		crypt->uiLate += late;
	} else if (crypt->uiLate > abs(late)) {
		crypt->uiLate -= abs(late);
	}
	// crypt->uiLost += lost, but we have to make sure we don't cause wrap-arounds on the unsigned lhs
	if (lost > 0) {
		crypt->uiLost += lost;
	} else if (crypt->uiLost > abs(lost)) {
		crypt->uiLost -= abs(lost);
	}
	return true;
}

#if defined(__LP64__)

#define BLOCKSIZE 2
#define SHIFTBITS 63
typedef uint64_t subblock;

#define SWAP64(x) __builtin_bswap64(x)

#define SWAPPED(x) SWAP64(x)

#else
#define BLOCKSIZE 4
#define SHIFTBITS 31
typedef uint32_t subblock;
#define SWAPPED(x) htonl(x)
#endif

typedef subblock keyblock[BLOCKSIZE];

#define HIGHBIT (1 << SHIFTBITS);

static void inline XOR(subblock *dst, const subblock *a, const subblock *b) {
	for (int i = 0; i < BLOCKSIZE; i++) {
		dst[i] = a[i] ^ b[i];
	}
}

static void inline S2(subblock *block) {
	subblock carry = SWAPPED(block[0]) >> SHIFTBITS;
	for (int i = 0; i < BLOCKSIZE - 1; i++)
		block[i] = SWAPPED((SWAPPED(block[i]) << 1) | (SWAPPED(block[i + 1]) >> SHIFTBITS));
	block[BLOCKSIZE - 1] = SWAPPED((SWAPPED(block[BLOCKSIZE - 1]) << 1) ^ (carry * 0x87));
}

static void inline S3(subblock *block) {
	subblock carry = SWAPPED(block[0]) >> SHIFTBITS;
	for (int i = 0; i < BLOCKSIZE - 1; i++)
		block[i] ^= SWAPPED((SWAPPED(block[i]) << 1) | (SWAPPED(block[i + 1]) >> SHIFTBITS));
	block[BLOCKSIZE - 1] ^= SWAPPED((SWAPPED(block[BLOCKSIZE - 1]) << 1) ^ (carry * 0x87));
}

static void inline ZERO(keyblock *block) {
	memset(block, 0, BLOCKSIZE * sizeof(block));
}

#define AESencrypt_ctx(src, dst, key, enc_ctx)                                                      \
	{                                                                                               \
		int outlen = 0;                                                                             \
		EVP_EncryptInit_ex(enc_ctx, EVP_aes_128_ecb(), NULL, key, NULL);                            \
		EVP_CIPHER_CTX_set_padding(enc_ctx, 0);                                                     \
		EVP_EncryptUpdate(enc_ctx, (uint8_t*)dst, &outlen,               \
						  (const uint8_t*)src, AES_BLOCK_SIZE);          \
		EVP_EncryptFinal_ex(enc_ctx, ((uint8_t*)dst + outlen), &outlen); \
	}
#define AESdecrypt_ctx(src, dst, key, dec_ctx)                                                      \
	{                                                                                               \
		int outlen = 0;                                                                             \
		EVP_DecryptInit_ex(dec_ctx, EVP_aes_128_ecb(), NULL, key, NULL);                            \
		EVP_CIPHER_CTX_set_padding(dec_ctx, 0);                                                     \
		EVP_DecryptUpdate(dec_ctx, (uint8_t*)dst, &outlen,               \
						  (const uint8_t*)src, AES_BLOCK_SIZE);          \
		EVP_DecryptFinal_ex(dec_ctx, ((uint8_t*)dst + outlen), &outlen); \
	}

#define AESencrypt(src, dst, key) AESencrypt_ctx(src, dst, key, crypt->enc_ctx_ocb_enc)
#define AESdecrypt(src, dst, key) AESdecrypt_ctx(src, dst, key, crypt->dec_ctx_ocb_enc)

bool crypt_ocb_encrypt(mumble_crypt *crypt, const uint8_t *plain, uint8_t *encrypted, size_t len, const uint8_t *nonce, uint8_t *tag, bool modifyPlainOnXEXStarAttack) {
	keyblock checksum, delta, tmp, pad;
	bool success = true;

	// Initialize
	AESencrypt(nonce, delta, crypt->raw_key);
	ZERO(&checksum);

	while (len > AES_BLOCK_SIZE) {
		// Counter-cryptanalysis described in section 9 of https://eprint.iacr.org/2019/311
		// For an attack, the second to last block (i.e. the last iteration of this loop)
		// must be all 0 except for the last byte (which may be 0 - 128).
		bool flipABit = false; // *plain is const, so we can't directly modify it
		if (len - AES_BLOCK_SIZE <= AES_BLOCK_SIZE) {
			uint8_t sum = 0;
			for (int i = 0; i < AES_BLOCK_SIZE - 1; ++i) {
				sum |= plain[i];
			}
			if (sum == 0) {
				if (modifyPlainOnXEXStarAttack) {
					// The assumption that critical packets do not turn up by pure chance turned out to be incorrect
					// since digital silence appears to produce them in mass.
					// So instead we now modify the packet in a way which should not affect the audio but will
					// prevent the attack.
					flipABit = true;
				} else {
					// This option still exists but only to allow us to test ocb_decrypt's detection.
					success = false;
				}
			}
		}

		S2(delta);
		XOR(tmp, delta, (const subblock*)plain);
		if (flipABit) {
			*(subblock*)tmp ^= 1;
		}
		AESencrypt(tmp, tmp, crypt->raw_key);
		XOR((subblock*)encrypted, delta, tmp);
		XOR(checksum, checksum, (const subblock*)plain);
		if (flipABit) {
			*(subblock*)checksum ^= 1;
		}

		len -= AES_BLOCK_SIZE;
		plain += AES_BLOCK_SIZE;
		encrypted += AES_BLOCK_SIZE;
	}

	S2(delta);
	ZERO(&tmp);
	tmp[BLOCKSIZE - 1] = SWAPPED(len * 8);
	XOR(tmp, tmp, delta);
	AESencrypt(tmp, pad, crypt->raw_key);
	memcpy(tmp, plain, len);
	memcpy((uint8_t *)tmp + len, (const uint8_t *)pad + len,
	       AES_BLOCK_SIZE - len);
	XOR(checksum, checksum, tmp);
	XOR(tmp, pad, tmp);
	memcpy(encrypted, tmp, len);

	S3(delta);
	XOR(tmp, delta, checksum);
	AESencrypt(tmp, tag, crypt->raw_key);

	return success;
}

#undef AESencrypt
#undef AESdecrypt

#define AESencrypt(src, dst, key) AESencrypt_ctx(src, dst, key, crypt->enc_ctx_ocb_dec)
#define AESdecrypt(src, dst, key) AESdecrypt_ctx(src, dst, key, crypt->dec_ctx_ocb_dec)

bool crypt_ocb_decrypt(mumble_crypt *crypt, const uint8_t *encrypted, uint8_t *plain, size_t len, const uint8_t *nonce, uint8_t *tag) {
	keyblock checksum, delta, tmp, pad;
	bool success = true;

	// Initialize
	AESencrypt(nonce, delta, crypt->raw_key);
	ZERO(&checksum);

	while (len > AES_BLOCK_SIZE) {
		S2(delta);
		XOR(tmp, delta, (const subblock*)encrypted);
		AESdecrypt(tmp, tmp, crypt->raw_key);
		XOR((subblock*)plain, delta, tmp);
		XOR(checksum, checksum, (const subblock*)plain);
		len -= AES_BLOCK_SIZE;
		plain += AES_BLOCK_SIZE;
		encrypted += AES_BLOCK_SIZE;
	}

	S2(delta);
	ZERO(&tmp);
	tmp[BLOCKSIZE - 1] = SWAPPED(len * 8);
	XOR(tmp, tmp, delta);
	AESencrypt(tmp, pad, crypt->raw_key);
	memset(tmp, 0, AES_BLOCK_SIZE);
	memcpy(tmp, encrypted, len);
	XOR(tmp, tmp, pad);
	XOR(checksum, checksum, tmp);
	memcpy(plain, tmp, len);

	// Counter-cryptanalysis described in section 9 of https://eprint.iacr.org/2019/311
	// In an attack, the decrypted last block would need to equal `delta ^ len(128)`.
	// With a bit of luck (or many packets), smaller values than 128 (i.e. non-full blocks) are also
	// feasible, so we check `tmp` instead of `plain`.
	// Since our `len` only ever modifies the last byte, we simply check all remaining ones.
	if (memcmp(tmp, delta, AES_BLOCK_SIZE - 1) == 0) {
		success = false;
	}

	S3(delta);
	XOR(tmp, delta, checksum);
	AESencrypt(tmp, tag, crypt->raw_key);

	return success;
}

#undef AESencrypt
#undef AESdecrypt
#undef BLOCKSIZE
#undef SHIFTBITS
#undef SWAPPED
#undef HIGHBIT