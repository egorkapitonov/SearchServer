#pragma once

#include <iostream>
#include <vector>
#include <string>
#include "document.h"

template <typename IteratorRanges>
class IteratorRange {
public:
    explicit IteratorRange(IteratorRanges begin, IteratorRanges end)
        : begin_(begin)
        , end_(end)
        , size_(distance(begin, end)) {
        }

    IteratorRanges begin() const {
        return begin_;
    }
    IteratorRanges end() const {
        return end_;
    }
    size_t size() const {
        return size_;
    }
private:
    IteratorRanges begin_;
    IteratorRanges end_;
    size_t size_;
};

template<typename It>
std::ostream& operator<<(std::ostream& out, const IteratorRange<It>& d) {
    for (auto i = d.begin(); i != d.end(); ++i) {
        out << *i;
    }
    return out;
}

template <typename Pag>
class Paginator {
public:
    Paginator(const Pag& result_begin, const Pag& result_end, size_t size_of_sheet) {
        auto full_size = distance(result_begin, result_end);
        Pag helper = result_begin;
        for (auto i = 0; i < full_size / size_of_sheet; ++i) {
            sheets.push_back(IteratorRange<Pag>(helper, helper + size_of_sheet));
            helper = helper + size_of_sheet;
        }
        if (helper != result_end) {
            sheets.push_back(IteratorRange<Pag>(helper, result_end));
        }
    }
    auto begin() const {
        return sheets.begin();
    }
    auto end() const {
        return sheets.end();
    }
    size_t size() {
        return sheets.size();
    }
private:
    std::vector<IteratorRange<Pag>> sheets;
};

template <typename Container>
auto Paginate(const Container& c, size_t page_size) {
    return Paginator(begin(c), end(c), page_size);
}