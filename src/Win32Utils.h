#pragma once

#include <memory>

#ifdef _WIN32

#include <windows.h>

std::shared_ptr<ACL> BuildDacl777();
std::shared_ptr<SECURITY_ATTRIBUTES> BuildSecurityAttributes777();

#endif