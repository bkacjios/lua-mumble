#include "mumble.h"

double gettime()
{
	struct timeval time;
	gettimeofday(&time, (struct timezone *) NULL);
	return time.tv_sec + time.tv_usec/1.0e6;
}

int getNetworkBandwidth(int bitrate, int frames)
{
	int overhead = 20 + 8 + 4 + 1 + 2 + 12 + frames;
	overhead *= (800 / frames);
	return overhead + bitrate;
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

/* Function to add a node at the beginning of Linked List. 
	This function expects a pointer to the data to be added 
	and size of the data type */

void list_add(LinkNode** head_ref, uint32_t value) 
{
	LinkNode* new_node = malloc(sizeof(LinkNode));
	new_node->data = value;
	new_node->next = (*head_ref);
	(*head_ref) = new_node;
}

void list_remove(LinkNode **head_ref, uint32_t value)
{
	LinkNode* temp = *head_ref, *prev;

	// If head node itself holds the key to be deleted
	if (temp != NULL && temp->data == value)
	{
		*head_ref = temp->next;   // Changed head
		free(temp);               // free old head
		return;
	}

	// Search for the key to be deleted, keep track of the
	// previous node as we need to change 'prev->next'
	while (temp != NULL && temp->data != value)
	{
		prev = temp;
		temp = temp->next;
	}

	// If key was not present in linked list 
	if (temp == NULL) return;

	// Unlink the node from linked list 
	prev->next = temp->next;

	free(temp); // Free memory
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
