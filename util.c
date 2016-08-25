#include "mumble.h"

double gettime()
{
	struct timeval time;
	gettimeofday(&time, (struct timezone *) NULL);
	return time.tv_sec + time.tv_usec/1.0e6;
}

void bin_to_strhex(uint8_t *bin, size_t binsz, char **result)
{
	char hex_str[]= "0123456789abcdef";
	size_t i;

	*result = (char *)malloc(binsz * 2 + 1);
	(*result)[binsz * 2] = 0;

	if (!binsz)
		return;

	for (i = 0; i < binsz; i++)
	{
		(*result)[i * 2 + 0] = hex_str[(bin[i] >> 4) & 0x0F];
		(*result)[i * 2 + 1] = hex_str[(bin[i]     ) & 0x0F];
	}
}

void debugstack(lua_State *l, const char* text)
{
	for (int i=1; i<=lua_gettop(l); i++)
	{
		if (lua_isstring(l, i))
			printf("%s [%d] = %s (%s)\n", text, i, eztype(l, i), lua_tostring(l, i));
		else
			printf("%s [%d] = %s\n", text, i, eztype(l, i));
	}
}

int luaL_checkboolean(lua_State *L, int i){
	if(!lua_isboolean(L,i))
		luaL_typerror(L,i,"boolean");
	return lua_toboolean(L,i);
}

int luaL_optboolean(lua_State *L, int i, int d){
	if(lua_type(L, i) < 1)
		return d;
	else
		return luaL_checkboolean(L, i);
}

const char* eztype(lua_State *L, int i)
{
	return lua_typename(L, lua_type(L, i));
}