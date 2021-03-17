#include "stdafx.h"
#include <iostream>
#include "SyncStream.h"

SyncStream sync_cout(std::cout);
SyncStream sync_cerr(std::cerr);

// https://stackoverflow.com/questions/10015897/cannot-have-typeofstdendl-as-template-parameter
template<class e, class t> //stream version
auto get_endl(const std::basic_ostream<e, t>&) -> decltype(&std::endl<e, t>)
{
    return std::endl<e, t>;
}


decltype(&std::endl<char, std::char_traits<char>>) sync_endl = get_endl(std::cout);