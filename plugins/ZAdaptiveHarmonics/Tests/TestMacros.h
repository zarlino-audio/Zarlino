#pragma once
#include <iostream>

extern int gPassed;
extern int gFailed;

#define TEST(name) \
    std::cerr << "  " << name << " ... " << std::flush; \
    _testOk = true;

#define CHECK(cond) \
    do { if (!(cond)) { _testOk = false; std::cerr << "\n    FAIL: " << #cond << " (line " << __LINE__ << ")" << std::endl; } } while(0)

#define END_TEST() \
    if (_testOk) { std::cerr << "PASS" << std::endl; gPassed++; } \
    else { std::cerr << "FAIL" << std::endl; gFailed++; }
