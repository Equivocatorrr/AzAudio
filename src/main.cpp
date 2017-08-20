/*
    File: main.cpp
    Author: singularity
    Simple test program for our library
*/

#include <iostream>

#include "log.hpp"

#ifdef __unix
#include <csignal>
#include <cstdlib>
#include <execinfo.h>
#include <unistd.h>

void handler(int sig) {
    void *array[50];
    size_t size = backtrace(array, 50);
    char **strings;
    strings = backtrace_symbols(array, size);
    sys::cout <<  "Error: signal " << sig << std::endl;
    for (uint32_t i = 0; i < size; i++) {
        sys::cout << strings[i] << std::endl;
    }
    free(strings);
    exit(1);
}
#endif

int main(int argumentCount, char** argumentValues) {
    #ifdef __unix
    signal(SIGSEGV, handler);
    #endif
    try {
        sys::cout << "Hello world!" << std::endl;
    } catch (std::runtime_error& e) {
        sys::cout << "Runtime Error: " << e.what() << std::endl;
    }
    return 0;
}
