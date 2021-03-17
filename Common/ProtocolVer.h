#pragma once

#define PROTOCOL_VERSION 1.0
#ifdef WIN32
#define PLATFORM "win32"
#else 
#define PLATFORM "linux"
#endif

#define STRINGIZE(n) #n
#define STR(s) STRINGIZE(s)
#define MAKE_VERSION(name, msg) name "/" STR(PROTOCOL_VERSION) "/" PLATFORM ": " msg
