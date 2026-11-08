//
// Created by Bradley Pearce on 19/01/2026.
//

extern "C" {
    #include "../src/vm.h"
    #include "../src/utils.h"
}
#include <iostream>
#include <string>
#include "test_include.h"
#include "test_generate_list.h"


void error_handler(const char *error) {
    std::cout << error << std::endl;
    // We will use C++ exceptions to gracefully unwind the stack, which we will catch
    throw std::runtime_error(error);
}

int main()
{
    // tests
    test_generate_list();
    MyloConfig.error_callback = &error_handler;
    // test the tests
    int passed = 0;
    int total = 0;
    for (auto &test : tests)
    {
        std::cout << "Running..." << test.first << std::endl;
        try {
            if (test.second().result) {
                ++passed;
                setTerminalColor(MyloFgGreen, MyloBgColorDefault);
                std::cout << "[PASS] ";
                setTerminalColor(MyloFgDefault, MyloBgColorDefault);
                std::cout << test.first << std::endl;
            }
            else {
                setTerminalColor(MyloFgRed, MyloBgColorDefault);
                std::cout << "[FAILED] ";
                setTerminalColor(MyloFgDefault, MyloBgColorDefault);
                std::cout << test.first << std::endl;
                setTerminalColor(MyloFgMagenta, MyloBgColorDefault);
                std::cout << " --> " << test.second().result_string << std::endl;
                setTerminalColor(MyloFgDefault, MyloBgColorDefault);
            }
            ++total;
        } catch (std::exception &e) {
            setTerminalColor(MyloFgRed, MyloBgColorDefault);
            std::cout << "[FAILED -- EXCEPTION] ";
            std::cout << std::endl << test.first << " --> " << "EXCEPTION" << e.what() << std::endl;
            setTerminalColor(MyloFgDefault, MyloBgColorDefault);
            ++total;
        }
        setTerminalColor(MyloFgBlue, MyloBgColorDefault);
        std::cout << "========================================================" << std::endl;
        setTerminalColor(MyloFgDefault, MyloBgColorDefault);

    }
    std::cout << std::endl;

    std::cout << "Tests Passed : " << passed << " out of " << total << std::endl;

    return passed == total ? 0 : 1;
}