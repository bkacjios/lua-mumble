// Runs every test registered by the files in tests/core

#include "test.h"

std::vector<TestCase>& tests() {
	static std::vector<TestCase> list;
	return list;
}

int failures = 0;

int main() {
	setvbuf(stdout, NULL, _IOLBF, 0);
	int failed_tests = 0;
	for (const TestCase &test : tests()) {
		int before = failures;
		test.run();
		bool ok = failures == before;
		if (!ok) failed_tests++;
		printf("%s %s\n", ok ? "PASS" : "FAIL", test.name);
	}
	printf("%zu tests, %d failed\n", tests().size(), failed_tests);
	return failed_tests == 0 ? 0 : 1;
}
