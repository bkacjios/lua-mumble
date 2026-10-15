#include "util/util.h"


#include <arpa/inet.h>
#include <cctype>
#include <cstdio>

std::string bin_to_strhex(const char *bin, size_t binsz) {
	static const char hex_str[] = "0123456789abcdef";

	std::string result(binsz * 2, '\0');

	for (size_t i = 0; i < binsz; i++) {
		result[i * 2 + 0] = hex_str[(bin[i] >> 4) & 0x0F];
		result[i * 2 + 1] = hex_str[(bin[i]     ) & 0x0F];
	}

	return result;
}

std::string bin_to_strhex(const std::string &bin) {
	return bin_to_strhex(bin.data(), bin.size());
}
