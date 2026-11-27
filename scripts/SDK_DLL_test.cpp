#include <iostream>
#include <app\include\recubin.h>

/*
cl /LD /MD /std:c++23 /EHsc SDK_DLL_test.cpp /Fe:SDK_DLL_test.dll /I.. /I..\app\include
*/
using namespace recubin;
void start() {
    // std::optional<Instance> cube = GetSelfProcess()->GetParent();
    std::cout << "This is from C++ DLL Process!\n";
}