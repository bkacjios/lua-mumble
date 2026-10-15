// Copyright 2020-2023 The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "net/crypt.h"


#include <arpa/inet.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <openssl/rand.h>

/*
 * This code implements OCB-AES128.
 * The algorithm design was dedicated to the public domain.
 * https://www.cs.ucdavis.edu/~rogaway/ocb/license.htm
 * https://www.cs.ucdavis.edu/~rogaway/ocb/ocb-faq.htm
 */

CryptState::CryptState() {
	for (int i = 0; i < 0x100; i++)
		decrypt_history[i] = 0;

	bInit = false;
	memset(raw_key, 0, AES_KEY_SIZE_BYTES);
	memset(encrypt_iv, 0, AES_BLOCK_SIZE);
	memset(decrypt_iv, 0, AES_BLOCK_SIZE);
	uiGood = uiLate = uiLost = uiResync = 0;

	enc_ctx_ocb_enc = EVP_CIPHER_CTX_new();
	dec_ctx_ocb_enc = EVP_CIPHER_CTX_new();
	enc_ctx_ocb_dec = EVP_CIPHER_CTX_new();
	dec_ctx_ocb_dec = EVP_CIPHER_CTX_new();
}

CryptState::~CryptState() {
	EVP_CIPHER_CTX_free(enc_ctx_ocb_enc);
	EVP_CIPHER_CTX_free(dec_ctx_ocb_enc);
	EVP_CIPHER_CTX_free(enc_ctx_ocb_dec);
	EVP_CIPHER_CTX_free(dec_ctx_ocb_dec);
	bInit = false;
}

bool CryptState::isValid() const {
	return bInit;
}

void CryptState::genKey() {
	RAND_bytes(raw_key, AES_KEY_SIZE_BYTES);
	RAND_bytes(encrypt_iv, AES_BLOCK_SIZE);
	RAND_bytes(decrypt_iv, AES_BLOCK_SIZE);
	bInit = true;
}

bool CryptState::setKey(const uint8_t *rkey, size_t rkey_len,
                        const uint8_t *eiv, size_t eiv_len,
                        const uint8_t *div, size_t div_len) {
	if (rkey_len == AES_KEY_SIZE_BYTES && eiv_len == AES_BLOCK_SIZE && div_len == AES_BLOCK_SIZE) {
		memcpy(raw_key, rkey, AES_KEY_SIZE_BYTES);
		memcpy(encrypt_iv, eiv, AES_BLOCK_SIZE);
		memcpy(decrypt_iv, div, AES_BLOCK_SIZE);
		bInit = true;
		return true;
	}
	return false;
}

bool CryptState::setRawKey(const uint8_t *rkey, size_t rkey_len) {
	if (rkey_len == AES_KEY_SIZE_BYTES) {
		memcpy(raw_key, rkey, AES_KEY_SIZE_BYTES);
		return true;
	}
	return false;
}

bool CryptState::setEncryptIV(const uint8_t *iv, size_t iv_len) {
	if (iv_len == AES_BLOCK_SIZE) {
		memcpy(encrypt_iv, iv, AES_BLOCK_SIZE);
		return true;
	}
	return false;
}

bool CryptState::setDecryptIV(const uint8_t *iv, size_t iv_len) {
	if (iv_len == AES_BLOCK_SIZE) {
		memcpy(decrypt_iv, iv, AES_BLOCK_SIZE);
		return true;
	}
	return false;
}

const uint8_t* CryptState::getRawKey() const {
	return raw_key;
}

const uint8_t* CryptState::getEncryptIV() const {
	return encrypt_iv;
}

const uint8_t* CryptState::getDecryptIV() const {
	return decrypt_iv;
}

size_t CryptState::getGood() const {
	return uiGood;
}

size_t CryptState::getLate() const {
	return uiLate;
}

size_t CryptState::getLost() const {
	return uiLost;
}

std::optional<std::string> CryptState::encrypt(const uint8_t *source, size_t plain_length) {
	std::string encrypted(plain_length + 4, '\0');
	if (!encrypt(source, (uint8_t*) encrypted.data(), plain_length))
		return std::nullopt;
	return encrypted;
}

std::optional<std::string> CryptState::decrypt(const uint8_t *source, size_t crypted_length) {
	if (crypted_length < 4)
		return std::nullopt;
	std::string decrypted(crypted_length - 4, '\0');
	if (!decrypt(source, (uint8_t*) decrypted.data(), crypted_length))
		return std::nullopt;
	return decrypted;
}

bool CryptState::encrypt(const uint8_t *source, uint8_t *dst, size_t plain_length) {
	uint8_t tag[AES_BLOCK_SIZE];

	// First, increase our IV.
	for (int i = 0; i < AES_BLOCK_SIZE; i++)
		if (++encrypt_iv[i])
			break;

	if (!ocbEncrypt(source, dst + 4, plain_length, encrypt_iv, tag, true)) {
		return false;
	}

	dst[0] = encrypt_iv[0];
	dst[1] = tag[0];
	dst[2] = tag[1];
	dst[3] = tag[2];
	return true;
}

bool CryptState::decrypt(const uint8_t *source, uint8_t *dst, size_t crypted_length) {
	if (crypted_length < 4)
		return false;

	size_t plain_length = crypted_length - 4;

	uint8_t saveiv[AES_BLOCK_SIZE];
	uint8_t ivbyte = source[0];
	bool restore         = false;
	uint8_t tag[AES_BLOCK_SIZE];

	int lost = 0;
	int late = 0;

	memcpy(saveiv, decrypt_iv, AES_BLOCK_SIZE);

	if (((decrypt_iv[0] + 1) & 0xFF) == ivbyte) {
		// In order as expected.
		if (ivbyte > decrypt_iv[0]) {
			decrypt_iv[0] = ivbyte;
		} else if (ivbyte < decrypt_iv[0]) {
			decrypt_iv[0] = ivbyte;
			for (int i = 1; i < AES_BLOCK_SIZE; i++)
				if (++decrypt_iv[i])
					break;
		} else {
			return false;
		}
	} else {
		// This is either out of order or a repeat.

		int diff = ivbyte - decrypt_iv[0];
		if (diff > 128)
			diff = diff - 256;
		else if (diff < -128)
			diff = diff + 256;

		if ((ivbyte < decrypt_iv[0]) && (diff > -30) && (diff < 0)) {
			// Late packet, but no wraparound.
			late          = 1;
			lost          = -1;
			decrypt_iv[0] = ivbyte;
			restore       = true;
		} else if ((ivbyte > decrypt_iv[0]) && (diff > -30) && (diff < 0)) {
			// Last was 0x02, here comes 0xff from last round
			late          = 1;
			lost          = -1;
			decrypt_iv[0] = ivbyte;
			for (int i = 1; i < AES_BLOCK_SIZE; i++)
				if (decrypt_iv[i]--)
					break;
			restore = true;
		} else if ((ivbyte > decrypt_iv[0]) && (diff > 0)) {
			// Lost a few packets, but beyond that we're good.
			lost          = ivbyte - decrypt_iv[0] - 1;
			decrypt_iv[0] = ivbyte;
		} else if ((ivbyte < decrypt_iv[0]) && (diff > 0)) {
			// Lost a few packets, and wrapped around
			lost          = 256 - decrypt_iv[0] + ivbyte - 1;
			decrypt_iv[0] = ivbyte;
			for (int i = 1; i < AES_BLOCK_SIZE; i++)
				if (++decrypt_iv[i])
					break;
		} else {
			return false;
		}

		if (decrypt_history[decrypt_iv[0]] == decrypt_iv[1]) {
			memcpy(decrypt_iv, saveiv, AES_BLOCK_SIZE);
			return false;
		}
	}

	bool ocb_success = ocbDecrypt(source + 4, dst, plain_length, decrypt_iv, tag);

	if (!ocb_success || memcmp(tag, source + 1, 3) != 0) {
		memcpy(decrypt_iv, saveiv, AES_BLOCK_SIZE);
		return false;
	}
	decrypt_history[decrypt_iv[0]] = decrypt_iv[1];

	if (restore)
		memcpy(decrypt_iv, saveiv, AES_BLOCK_SIZE);

	uiGood++;
	// uiLate += late, but we have to make sure we don't cause wrap-arounds on the unsigned lhs
	if (late > 0) {
		uiLate += late;
	} else if (uiLate > abs(late)) {
		uiLate -= abs(late);
	}
	// uiLost += lost, but we have to make sure we don't cause wrap-arounds on the unsigned lhs
	if (lost > 0) {
		uiLost += lost;
	} else if (uiLost > abs(lost)) {
		uiLost -= abs(lost);
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

#define AESencrypt(src, dst, key) AESencrypt_ctx(src, dst, key, enc_ctx_ocb_enc)
#define AESdecrypt(src, dst, key) AESdecrypt_ctx(src, dst, key, dec_ctx_ocb_enc)

bool CryptState::ocbEncrypt(const uint8_t *plain, uint8_t *encrypted, size_t len, const uint8_t *nonce, uint8_t *tag, bool modifyPlainOnXEXStarAttack) {
	keyblock checksum, delta, tmp, pad;
	bool success = true;

	// Initialize
	AESencrypt(nonce, delta, raw_key);
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
		AESencrypt(tmp, tmp, raw_key);
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
	AESencrypt(tmp, pad, raw_key);
	memcpy(tmp, plain, len);
	memcpy((uint8_t *)tmp + len, (const uint8_t *)pad + len,
	       AES_BLOCK_SIZE - len);
	XOR(checksum, checksum, tmp);
	XOR(tmp, pad, tmp);
	memcpy(encrypted, tmp, len);

	S3(delta);
	XOR(tmp, delta, checksum);
	AESencrypt(tmp, tag, raw_key);

	return success;
}

#undef AESencrypt
#undef AESdecrypt

#define AESencrypt(src, dst, key) AESencrypt_ctx(src, dst, key, enc_ctx_ocb_dec)
#define AESdecrypt(src, dst, key) AESdecrypt_ctx(src, dst, key, dec_ctx_ocb_dec)

bool CryptState::ocbDecrypt(const uint8_t *encrypted, uint8_t *plain, size_t len, const uint8_t *nonce, uint8_t *tag) {
	keyblock checksum, delta, tmp, pad;
	bool success = true;

	// Initialize
	AESencrypt(nonce, delta, raw_key);
	ZERO(&checksum);

	while (len > AES_BLOCK_SIZE) {
		S2(delta);
		XOR(tmp, delta, (const subblock*)encrypted);
		AESdecrypt(tmp, tmp, raw_key);
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
	AESencrypt(tmp, pad, raw_key);
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
	AESencrypt(tmp, tag, raw_key);

	return success;
}

#undef AESencrypt
#undef AESdecrypt
#undef BLOCKSIZE
#undef SHIFTBITS
#undef SWAPPED
#undef HIGHBIT
