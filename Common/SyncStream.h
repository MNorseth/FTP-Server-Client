#pragma once
#include <mutex>
#include <iostream>
#include <ostream>
#include "Message.h"

class SyncStream {
    std::mutex mtx_;
    std::ostream& dest_;

    void print_internal() {}

    template <class ...Args>
    void print_internal(std::ostream& (*f)(std::ostream&), const Args&... args) {
        f(dest_);
        print_internal(args...);
    }

    template <class NextArg, class ...Args>
    void print_internal(const NextArg& next, const Args&... args) {
        dest_ << next;
        print_internal(args...);
    }



    public:
        SyncStream(std::ostream& dest) : dest_(dest) {}


        template <class ...Args>
        void print(const Args&... args) {
            std::lock_guard<std::mutex> lock(mtx_);
            print_internal(args...);
        }
};

extern SyncStream sync_cout;
extern SyncStream sync_cerr;
extern decltype(&std::endl<char, std::char_traits<char>>) sync_endl;