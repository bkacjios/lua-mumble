#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <list>
#include <optional>
#include <string>

std::string bin_to_strhex(const char *bin, size_t binsz);
std::string bin_to_strhex(const std::string &bin);

/*
 * An index keyed list that keeps the most recently added entry at the front.
 * Iteration order matches the order the Lua API has always exposed.
 */
template <typename T>
class LinkList {
public:
	struct Node {
		uint32_t index;
		T* data;
	};

	using iterator = typename std::list<Node>::iterator;

	void add(uint32_t index, T* data) {
		nodes.push_front(Node{index, data});
	}

	// Removes the first entry with the given index
	void remove(uint32_t index) {
		for (auto it = nodes.begin(); it != nodes.end(); ++it) {
			if (it->index == index) {
				nodes.erase(it);
				return;
			}
		}
	}

	// Removes the first entry pointing to the given data
	void removeData(const T* data) {
		for (auto it = nodes.begin(); it != nodes.end(); ++it) {
			if (it->data == data) {
				nodes.erase(it);
				return;
			}
		}
	}

	T* get(uint32_t index) const {
		for (const Node &node : nodes) {
			if (node.index == index) {
				return node.data;
			}
		}
		return nullptr;
	}

	bool contains(uint32_t index) const {
		for (const Node &node : nodes) {
			if (node.index == index) {
				return true;
			}
		}
		return false;
	}

	// Returns the position of the entry pointing to data, or end()
	iterator find(const T* data) {
		for (auto it = nodes.begin(); it != nodes.end(); ++it) {
			if (it->data == data) {
				return it;
			}
		}
		return nodes.end();
	}

	void clear() {
		nodes.clear();
	}

	size_t count() const {
		return nodes.size();
	}

	bool empty() const {
		return nodes.empty();
	}

	Node& front() {
		return nodes.front();
	}

	iterator begin() {
		return nodes.begin();
	}

	iterator end() {
		return nodes.end();
	}

private:
	std::list<Node> nodes;
};

// A FIFO of binary messages, guarded by its owner's mutex
class MessageQueue {
public:
	void push(std::string message) {
		messages.push_back(std::move(message));
	}

	bool empty() const {
		return messages.empty();
	}

	std::string pop() {
		std::string message = std::move(messages.front());
		messages.pop_front();
		return message;
	}

	void clear() {
		messages.clear();
	}

private:
	std::deque<std::string> messages;
};

// The C string of an optional string, or NULL when it was never set
static inline const char* optcstr(const std::optional<std::string> &str) {
	return str ? str->c_str() : nullptr;
}
