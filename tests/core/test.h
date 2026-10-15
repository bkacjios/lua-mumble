#pragma once

// A minimal test runner, so the tests need no extra dependencies

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

struct TestCase {
	const char *name;
	std::function<void()> run;
};

std::vector<TestCase>& tests();
extern int failures;

struct TestRegistrar {
	TestRegistrar(const char *name, std::function<void()> run) {
		tests().push_back({name, run});
	}
};
#define TEST(name) \
	static void name(); \
	static TestRegistrar registrar_##name(#name, name); \
	static void name()

#define CHECK(cond) do { \
	if (!(cond)) { \
		failures++; \
		fprintf(stderr, "  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
	} \
} while (0)

#define CHECK_EQ(a, b) do { \
	auto _a = (a); \
	auto _b = (b); \
	if (!(_a == _b)) { \
		failures++; \
		fprintf(stderr, "  %s:%d: CHECK_EQ(%s, %s) failed: %s != %s\n", __FILE__, __LINE__, #a, #b, \
		        std::to_string(_a).c_str(), std::to_string(_b).c_str()); \
	} \
} while (0)

#define CHECK_NEAR(a, b, eps) do { \
	double _a = (a); \
	double _b = (b); \
	if (std::fabs(_a - _b) > (eps)) { \
		failures++; \
		fprintf(stderr, "  %s:%d: CHECK_NEAR(%s, %s) failed: %f != %f\n", __FILE__, __LINE__, #a, #b, _a, _b); \
	} \
} while (0)

