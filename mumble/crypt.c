#include "crypt.h"
#include "ocb.h"
#include "log.h"

#include <errno.h>
#include <string.h>

int mumble_crypt_new(lua_State* l) {
	mumble_crypt *crypt = lua_newuserdata(l, sizeof(mumble_crypt));

	if (crypt == NULL) {
		return luaL_error(l, "unable to create %s: %s", METATABLE_CRYPT, strerror(errno));
	}

	crypt_init(crypt);
	mumble_log(LOG_DEBUG, "%s: %p initalized", METATABLE_CRYPT, crypt);
	luaL_getmetatable(l, METATABLE_CRYPT);
	lua_setmetatable(l, -2);
	return 1;
}

static int ocb_aes128_isValid(lua_State* l) {
	mumble_crypt *crypt = luaL_checkudata(l, 1, METATABLE_CRYPT);
	lua_pushboolean(l, crypt_isValid(crypt));
	return 1;
}

static int ocb_aes128_genKey(lua_State* l) {
	mumble_crypt *crypt = luaL_checkudata(l, 1, METATABLE_CRYPT);
	crypt_genKey(crypt);
	return 0;
}

static int ocb_aes128_setKey(lua_State* l) {
	mumble_crypt *crypt = luaL_checkudata(l, 1, METATABLE_CRYPT);

	size_t rkey_len, eiv_len, div_len;
	const uint8_t* rkey = (const uint8_t*) luaL_checklstring(l, 2, &rkey_len);
	const uint8_t* eiv = (const uint8_t*) luaL_checklstring(l, 3, &eiv_len);
	const uint8_t* div = (const uint8_t*) luaL_checklstring(l, 4, &div_len);

	lua_pushboolean(l, crypt_setKey(crypt, rkey, rkey_len, eiv, eiv_len, div, div_len));
	return 1;
}

static int ocb_aes128_setRawKey(lua_State* l) {
	mumble_crypt *crypt = luaL_checkudata(l, 1, METATABLE_CRYPT);

	size_t raw_key_len;
	const uint8_t* raw_key = (const uint8_t*) luaL_checklstring(l, 2, &raw_key_len);

	lua_pushboolean(l, crypt_setRawKey(crypt, raw_key, raw_key_len));
	return 1;
}

static int ocb_aes128_setEncryptIV(lua_State* l) {
	mumble_crypt *crypt = luaL_checkudata(l, 1, METATABLE_CRYPT);

	size_t iv_len;
	const uint8_t* iv_key = (const uint8_t*) luaL_checklstring(l, 2, &iv_len);

	lua_pushboolean(l, crypt_setEncryptIV(crypt, iv_key, iv_len));
	return 1;
}

static int ocb_aes128_setDecryptIV(lua_State* l) {
	mumble_crypt *crypt = luaL_checkudata(l, 1, METATABLE_CRYPT);

	size_t iv_len;
	const uint8_t* iv_key = (const uint8_t*) luaL_checklstring(l, 2, &iv_len);

	lua_pushboolean(l, crypt_setDecryptIV(crypt, iv_key, iv_len));
	return 1;
}

static int ocb_aes128_getRawKey(lua_State* l) {
	mumble_crypt *crypt = luaL_checkudata(l, 1, METATABLE_CRYPT);
	lua_pushlstring(l, (const char*) crypt_getRawKey(crypt), AES_KEY_SIZE_BYTES);
	return 1;
}

static int ocb_aes128_getEncryptIV(lua_State* l) {
	mumble_crypt *crypt = luaL_checkudata(l, 1, METATABLE_CRYPT);
	lua_pushlstring(l, (const char*) crypt_getEncryptIV(crypt), AES_BLOCK_SIZE);
	return 1;
}

static int ocb_aes128_getDecryptIV(lua_State* l) {
	mumble_crypt *crypt = luaL_checkudata(l, 1, METATABLE_CRYPT);
	lua_pushlstring(l, (const char*) crypt_getDecryptIV(crypt), AES_BLOCK_SIZE);
	return 1;
}

static int ocb_aes128_getGood(lua_State* l) {
	mumble_crypt *crypt = luaL_checkudata(l, 1, METATABLE_CRYPT);
	lua_pushinteger(l, crypt_getGood(crypt));
	return 1;
}

static int ocb_aes128_getLate(lua_State* l) {
	mumble_crypt *crypt = luaL_checkudata(l, 1, METATABLE_CRYPT);
	lua_pushinteger(l, crypt_getLate(crypt));
	return 1;
}

static int ocb_aes128_getLost(lua_State* l) {
	mumble_crypt *crypt = luaL_checkudata(l, 1, METATABLE_CRYPT);
	lua_pushinteger(l, crypt_getLost(crypt));
	return 1;
}

static int ocb_aes128_encrypt(lua_State* l) {
	mumble_crypt *crypt = luaL_checkudata(l, 1, METATABLE_CRYPT);

	size_t decrypted_len;
	const uint8_t* decrypted = (const uint8_t*) luaL_checklstring(l, 2, &decrypted_len);

	size_t encrypted_len = decrypted_len + 4;
	uint8_t* encrypted = malloc(encrypted_len);

	if (crypt_encrypt(crypt, decrypted, encrypted, decrypted_len)) {
		lua_pushlstring(l, (const char*) encrypted, encrypted_len);
	} else {
		lua_pushnil(l);
	}

	free(encrypted);
	return 1;
}

static int ocb_aes128_decrypt(lua_State* l) {
	mumble_crypt *crypt = luaL_checkudata(l, 1, METATABLE_CRYPT);

	size_t encrypted_len;
	const uint8_t* encrypted = (const uint8_t*) luaL_checklstring(l, 2, &encrypted_len);

	size_t decrypted_len = encrypted_len - 4;
	uint8_t* decrypted = malloc(encrypted_len);

	if (crypt_decrypt(crypt, encrypted, decrypted, encrypted_len)) {
		lua_pushlstring(l, (const char*) decrypted, decrypted_len);
	} else {
		lua_pushnil(l);
	}

	free(decrypted);
	return 1;
}

static int ocb_aes128_tostring(lua_State *l) {
	lua_pushfstring(l, "%s: %p", METATABLE_CRYPT, lua_topointer(l, 1));
	return 1;
}

static int ocb_aes128_gc(lua_State* l) {
	mumble_crypt *crypt = luaL_checkudata(l, 1, METATABLE_CRYPT);
	mumble_log(LOG_DEBUG, "%s: %p garbage collected", METATABLE_CRYPT, crypt);
	crypt_uninitialize(crypt);
	return 0;
}

const luaL_Reg mumble_ocb_aes128[] = {
	{"isValid", ocb_aes128_isValid},
	{"genKey", ocb_aes128_genKey},
	{"setKey", ocb_aes128_setKey},
	{"setRawKey", ocb_aes128_setRawKey},
	{"setEncryptIV", ocb_aes128_setEncryptIV},
	{"setDecryptIV", ocb_aes128_setDecryptIV},
	{"getRawKey", ocb_aes128_getRawKey},
	{"getEncryptIV", ocb_aes128_getEncryptIV},
	{"getDecryptIV", ocb_aes128_getDecryptIV},
	{"getGood", ocb_aes128_getGood},
	{"getLate", ocb_aes128_getLate},
	{"getLost", ocb_aes128_getLost},
	{"encrypt", ocb_aes128_encrypt},
	{"decrypt", ocb_aes128_decrypt},
	{"__tostring", ocb_aes128_tostring},
	{"__gc", ocb_aes128_gc},
	{NULL, NULL}
};