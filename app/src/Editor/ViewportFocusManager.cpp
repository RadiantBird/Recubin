#include <Editor/ViewportFocusManager.hpp>
#include <iostream>

void ViewportFocusManager::debugPrint() const {
    std::lock_guard<std::mutex> lock(focusMutex);
    
    std::cout << "[ViewportFocusManager] ";
    if (currentFocusedViewport) {
        std::cout << "Focused: " << currentFocusedViewport->title 
                  << " (Generation: " << focusGeneration.load() << ")";
    } else {
        std::cout << "No focused viewport (Generation: " << focusGeneration.load() << ")";
    }
    std::cout << std::endl;
}