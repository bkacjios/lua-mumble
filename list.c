#include "mumble.h"

Node* node_new() {
	Node* node = malloc(sizeof(Node));
	node->key = 0;
	node->value = NULL;
	node->next = NULL;
	node->prev = NULL;
	return node;
}

void node_insert(Node* current, uint32_t key, void *value) {
	while(current->next != NULL) {
		current = current->next;
	}
	current->next = node_new();
	(current->next)->prev = current;
	current = current->next;
	current->key = key;
	current->value = value;
	current->next = NULL;
}

void* node_get_value(Node* current, uint32_t key) {
	while (current->next != NULL) {
		if (current->key == key) {
			printf("FOUND %d\n", key);
			return current->value;
		}
		current = current->next;
	}
	return NULL;
}

void* node_remove(Node* current, uint32_t key) {
	while (current->next != NULL && (current->next)->key != key) {
		current = current->next;
	}

	if(current->next == NULL) {
		printf("\nElement %d is not present in the list\n", key);
		return NULL;
	}

	Node *tmp = current->next;
	if(tmp->next == NULL) {
		current->next = NULL;
	} else {
		current->next = tmp->next;
		(current->next)->prev = tmp->prev;
	}
	tmp->prev = current;
	void* ret = tmp->value;
	free(tmp);
	return ret;
}