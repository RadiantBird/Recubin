#pragma once
#include <string>

namespace RecubinUUID {
// Generates a RFC-4122-shaped random identifier without exposing platform APIs.
std::string generate();
bool isValid(const std::string& value);
}
