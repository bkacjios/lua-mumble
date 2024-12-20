#include "mumble.h"
#include <ctype.h>

double gettime(clockid_t mode)
{
	struct timespec time;
	clock_gettime(mode, &time);
	return time.tv_sec + time.tv_nsec / 1.0e9;
}

void bin_to_strhex(char *bin, size_t binsz, char **result)
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

static void mumble_print(int level, const char* fmt, va_list va)
{
	char* lcolor;
	char* llevel;
	FILE *out = stdout;

	// Determine log level and associated color
	switch (level) {
		case LOG_INFO:
			llevel = " INFO";
			lcolor = "\x1b[32;1m";
			break;
		case LOG_WARN:
			llevel = " WARN";
			lcolor = "\x1b[33;1m";
			break;
		case LOG_ERROR:
			llevel = "ERROR";
			lcolor = "\x1b[31;1m";
			out = stderr;
			break;
		case LOG_DEBUG:
			llevel = "DEBUG";
			lcolor = "\x1b[35;1m";
			break;
		case LOG_TRACE:
			llevel = "TRACE";
			lcolor = "\x1b[36;1m";
			break;
		default:
			llevel = "UNKWN";
			lcolor = "\x1b[0m";
			break;
	}
	// Print the log type and color information
	fprintf(out, "[\x1b[34;1mMUMBLE\x1b[0m - %s%s\x1b[0m] ", lcolor, llevel);
	// Print our formatted message
	vfprintf(out, fmt, va);
}

void mumble_log(int level, const char* fmt, ...)
{
	if (level > LOG_LEVEL) return;
	va_list va;
	va_start(va,fmt);
	mumble_print(level, fmt, va);
	va_end(va);
}

static void iterate_and_print(lua_State *L, int index)
{
	// Push another reference to the table on top of the stack (so we know
	// where it is, and this function can work for negative, positive and
	// pseudo indices
	lua_pushvalue(L, index);
	// stack now contains: -1 => table
	lua_pushnil(L);
	// stack now contains: -1 => nil; -2 => table
	while (lua_next(L, -2))
	{
		// stack now contains: -1 => value; -2 => key; -3 => table
		// copy the key so that lua_tostring does not modify the original
		lua_pushvalue(L, -2);
		// stack now contains: -1 => key; -2 => value; -3 => key; -4 => table
		const char *key = lua_tostring(L, -1);
		const char *value = lua_tostring(L, -2);
		printf("%s => %s\n", key, value);
		// pop value + copy of key, leaving original key
		lua_pop(L, 2);
		// stack now contains: -1 => key; -2 => table
	}
	// stack now contains: -1 => table (when lua_next returns 0 it pops the key
	// but does not push anything.)
	// Pop table
	lua_pop(L, 1);
	// Stack is now the same as it was on entry to this function
}

void print_unescaped(const char* ptr, int len) {
	if (!ptr) return;
	for (int i = 0; i < len; i++, ptr++) {
		switch (*ptr) {
			case '\0': printf("\\0");  break;
			case '\a': printf("\\a");  break;
			case '\b': printf("\\b");  break;
			case '\f': printf("\\f");  break;
			case '\n': printf("\\n");  break;
			case '\r': printf("\\r");  break;
			case '\t': printf("\\t");  break;
			case '\v': printf("\\v");  break;
			case '\\': printf("\\\\"); break;
			case '\?': printf("\\\?"); break;
			case '\'': printf("\\\'"); break;
			case '\"': printf("\\\""); break;
			default:
				if (isprint(*ptr)) printf("%c",     *ptr);
				else               printf("\\%03o", *ptr);
		}
	}
}

#if (defined(LUA_VERSION_NUM) && LUA_VERSION_NUM < 502) && !defined(LUAJIT)

// luaL_testudata and luaL_traceback were introduced in Lua 5.2
// LuaJit also has their own implementations

void* luaL_testudata(lua_State* L, int index, const char* tname) {
	if (!lua_isuserdata(L, index)) {
		return NULL; // Not userdata
	}

	if (!lua_getmetatable(L, index)) {
		return NULL; // No metatable
	}

	luaL_getmetatable(L, tname); // Push the expected metatable

	if (!lua_rawequal(L, -1, -2)) {
		lua_pop(L, 2); // Pop both metatables
		return NULL;   // Metatables do not match
	}

	lua_pop(L, 2); // Pop both metatables
	return lua_touserdata(L, index); // Return the userdata
}

void luaL_traceback(lua_State* L, lua_State* L1, const char* msg, int level) {
	lua_Debug ar;
	luaL_Buffer b;
	luaL_buffinit(L, &b);

	if (msg) {
		luaL_addstring(&b, msg);
		luaL_addstring(&b, "\n");
	}

	luaL_addstring(&b, "stack traceback:\n");

	// Start traversing the call stack from the specified level
	while (lua_getstack(L1, level++, &ar)) {
		if (lua_getinfo(L1, "Slnf", &ar)) {
			luaL_addstring(&b, "\t"); // Indent for stack trace entries

			// Add source and line number
			if (ar.currentline > 0) {
				luaL_addstring(&b, ar.short_src);
				luaL_addstring(&b, ":");
				luaL_addstring(&b, lua_pushfstring(L, "%d", ar.currentline));
				lua_pop(L, 1); // Pop the formatted string
			} else {
				luaL_addstring(&b, "[C]");
			}

			// Add function information
			if (*ar.namewhat != '\0') {
				luaL_addstring(&b, ": in function '");
				luaL_addstring(&b, (ar.name ? ar.name : "?"));
				luaL_addstring(&b, "'");
			} else if (*ar.what == 'm') {
				luaL_addstring(&b, ": in main chunk");
			} else if (*ar.what == 'C') {
				const void* func_ptr = lua_topointer(L1, -1); // Function is already on the stack
				luaL_addstring(&b, ": at ");
				luaL_addstring(&b, lua_pushfstring(L, "%p", func_ptr));
				lua_pop(L, 1); // Pop the formatted string
			} else {
				luaL_addstring(&b, ": in function <");
				luaL_addstring(&b, ar.short_src);
				luaL_addstring(&b, ":");
				luaL_addstring(&b, lua_pushfstring(L, "%d", ar.linedefined));
				lua_pop(L, 1); // Pop the formatted string
				luaL_addstring(&b, ">");
			}

			luaL_addstring(&b, "\n");
			lua_pop(L1, 1); // Pop the function pushed by "f" from the stack
		}
	}

	luaL_pushresult(&b); // Push the accumulated buffer as a string
}
#endif

void luaL_debugstack(lua_State *l, const char* text)
{
	mumble_log(LOG_DEBUG, "%s stack dump\n", text);
	for (int i=1; i<=lua_gettop(l); i++)
	{
		int t = lua_type(l, i);
		size_t len;
		const char* tname = lua_typename(l, t);
		switch (t) {
			case LUA_TSTRING:  /* strings */
			{
				const char* str = lua_tolstring(l, i, &len);
				printf("\t%d - %s[\"", i, tname);
				print_unescaped(str, len);
				printf("\"]\n", i, tname, lua_tostring(l, i));
				break;
			}
			case LUA_TBOOLEAN:  /* booleans */
				printf("\t%d - %s[%s]\n", i, tname, lua_toboolean(l, i) ? "true" : "false");
				break;

			case LUA_TNUMBER:  /* numbers */
				printf("\t%d - %s[%g]\n", i, tname, lua_tonumber(l, i));
				break;

			default:  /* other values */
				printf("\t%d - %s[%p]\n", i, tname, lua_topointer(l, i));
				break;
		}
	}
}

int luaL_typerror(lua_State *L, int narg, const char *tname) {
	const char *msg = lua_pushfstring(L, "%s expected, got %s", tname, luaL_typename(L, narg));
	return luaL_argerror(L, narg, msg);
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

int luaL_isudata(lua_State *L, int ud, const char *tname) {
	if (lua_isuserdata(L, ud)) { // value is a userdata?
		if (lua_getmetatable(L, ud)) { // does it have a metatable?
			int equal;
			lua_getfield(L, LUA_REGISTRYINDEX, tname); // get correct metatable
			equal = lua_rawequal(L, -1, -2); // does it have the correct mt?
			lua_pop(L, 2); // remove both metatables
			return equal;
		}
	}
	return 0; // else false
}

/* Function to add a node at the beginning of Linked List. 
	This function expects a pointer to the data to be added 
	and size of the data type */

void list_add(LinkNode** head_ref, uint32_t index, void *data)
{
	LinkNode* new_node = malloc(sizeof(LinkNode));

	new_node->index = index;
	new_node->data = data;
	new_node->next = (*head_ref);

	(*head_ref) = new_node;
}

void list_remove(LinkNode **head_ref, uint32_t index)
{
	LinkNode* temp = *head_ref, *prev;

	// If head node itself holds the key to be deleted
	if (temp != NULL && temp->index == index)
	{
		*head_ref = temp->next;   // Changed head
		free(temp);               // free old head
		return;
	}

	// Search for the key to be deleted, keep track of the
	// previous node as we need to change 'prev->next'
	while (temp != NULL && temp->index != index)
	{
		prev = temp;
		temp = temp->next;
	}

	// If key was not present in linked list 
	if (temp == NULL) return;

	// Unlink the node from linked list 
	prev->next = temp->next;

	free(temp);               // free old head
}

void list_clear(LinkNode** head_ref)
{
	/* deref head_ref to get the real head */
	LinkNode* current = *head_ref;
	LinkNode* next;

	while (current != NULL)
	{
		next = current->next;
		free(current);
		current = next;
	}

	/* deref head_ref to affect the real head back
	in the caller. */
	*head_ref = NULL;
}

size_t list_count(LinkNode** head_ref)
{
	size_t count = 0;

	LinkNode* current = *head_ref;

	while (current != NULL)
	{
		count++;
		current = current->next;
	}

	return count;
}

void* list_get(LinkNode* current, uint32_t index) {
	while (current != NULL) {
		if (current->index == index) {
			return current->data;
		}
		current = current->next;
	}
	return NULL;
}