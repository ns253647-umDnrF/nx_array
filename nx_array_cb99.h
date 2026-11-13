#ifndef NX_ARRAY_CB99_H_INCLUDED
#define NX_ARRAY_CB99_H_INCLUDED

#if defined(__BORLANDC__) && __BORLANDC__ < 0x0570
// BCC 5.6 can omit destruction during exception propagation in inlined STL
// templates. Compile the ENTIRE translation unit with -vi- -x -xd, including
// precompiled headers. This local setting alone cannot fix earlier includes.
// Put the supplied bcc6_stl_fix directory FIRST in the project include path,
// and define _STLP_EXPOSE_GLOBALS_IMPLEMENTATION before all STL/PCH headers.
// Its _alloc.c patch preserves the stock allocator ABI.
// Do NOT enable _STLP_USE_NEWALLOC: prebuilt RTL code can use the stock pool.
#if defined(_RTLDLL) || defined(_STLP_USE_DYNAMIC_LIB)
#error NxArray BCC6 pool fix requires static STL/RTL; imported pool functions cannot be replaced here
#endif
#if defined(_STLP_USE_NEWALLOC) || defined(_STLP_USE_MALLOC) || defined(_STLP_USE_PERTHREAD_ALLOC) || defined(_STLP_DEBUG_ALLOC) || defined(_STLP_DEBUG)
#error NxArray BCC6 requires the stock non-debug STL pool ABI
#endif
#pragma option push -vi-
#endif

// nx_array for Borland C++Builder 6 (BCC32 5.6), C++98/03.
// Port of nx_array_cb.h, final version dated 2026-11-11.
// Do not include another nx_array implementation in the same translation unit.
// Elements live in list nodes; only the auxiliary pointer index reallocates.
// Iterators identify positions, not nodes. Structural changes invalidate them.
//
// C++98 ownership contract:
//  * Copies of a nonempty owning pointer array are rejected, even from lvalues.
//    C++98 cannot distinguish a const lvalue from a const temporary safely.
//  * Use transfer_from / spliceArray / mergeArray with named source objects.
//  * Never return an owning array by value: copy elision is not guaranteed.
//  * Raw pointers/views carry no provenance. Do not create competing owners.
//  * clear/erase/assignment do NOT delete pointees (same as the modern version).
//    Use the explicit *_with_delete operations to dispose of owned objects.
//  * Pointee destructors, element destructors and allocator deallocation must
//    not throw. Ownership is for single, complete objects, not new[] storage.
//  * Modern QString/QChar helper headers are not dependencies of this file.
//  * BCB6's STL uses overloaded operator& internally: element types that
//    overload unary operator& are NOT supported by that library. Use a wrapper
//    without this overload, or a pointer element with explicit ownership.
//  * clear_with_delete is allocation-free and quadratic in the worst case.

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <deque>
#include <functional>
#include <iterator>
#include <limits>
#include <list>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace nx_array_cb99_detail {
template<class A, class B> struct same_type { enum { value = 0 }; };
template<class A> struct same_type<A, A> { enum { value = 1 }; };
#if defined(__BORLANDC__) && __BORLANDC__ < 0x0570
# if !defined(NX_CB99_BCC6_POOL_FIX) || NX_CB99_BCC6_POOL_FIX != 20261112
#  error NxArray BCC6 requires bcc6_stl_fix include path and _STLP_EXPOSE_GLOBALS_IMPLEMENTATION before STL/PCH
# endif
# if defined(_STLP_USE_NEWALLOC) || defined(_STLP_USE_MALLOC) || defined(_STLP_USE_PERTHREAD_ALLOC) || defined(_STLP_DEBUG_ALLOC) || defined(_STLP_DEBUG)
#  error NxArray BCC6 requires the stock pool backend; alternative backends can conflict with the prebuilt RTL
# endif
// Verify the actual selected backend, not just configuration macro names.
typedef std::_Node_alloc required_stl_backend;
typedef char bcc6_stl_backend_must_match_stock_pool[
    same_type<std::__sgi_alloc, required_stl_backend>::value ? 1 : -1];
#endif
struct yes {};
struct no {};
struct iterator_tag {};
template<class U> struct input_reference { typedef const U& type; };
// Check mutable pointer argument types. BCC6 decays literals during template
// deduction; the private array overloads below provide the additional guard.
template<class Argument, class Char> struct writable_text_argument {};
template<class Char> struct writable_text_argument<Char*, Char> {
    static Char* get(Char* value) { return value; }
};
template<class Char> class writable_text_pointer {
    Char* value_;
public:
    template<class Argument> writable_text_pointer(const Argument& value)
        : value_(writable_text_argument<Argument, Char>::get(value)) {}
    operator Char*() const { return value_; }
};
typedef writable_text_pointer<char> writable_char_pointer;
template<> struct input_reference<char*> { typedef writable_text_pointer<char> type; };
template<> struct input_reference<wchar_t*> { typedef writable_text_pointer<wchar_t> type; };
template<class U> struct constructor_guard {
    template<class A> static void validate(const A&) {}
};
template<> struct constructor_guard<char*> {
    template<class A> static void validate(const A& a) { writable_text_pointer<char> checked(a); }
};
template<> struct constructor_guard<wchar_t*> {
    template<class A> static void validate(const A& a) { writable_text_pointer<wchar_t> checked(a); }
};
// Private array overloads are enabled only for pointer destinations that
// cannot safely retain literal storage. BCC6 checks access before instantiating
// these declarations, avoiding its array-reference template mangler bug.
struct disabled_literal_guard {};
struct disabled_wide_literal_guard {};
template<class U> struct literal_guard {
    typedef disabled_literal_guard narrow;
    typedef disabled_wide_literal_guard wide;
};
template<class U> struct literal_guard<U*> {
    typedef char narrow;
    typedef wchar_t wide;
};
#define NX_CB99_CONST_TEXT(U) \
template<> struct literal_guard<U> { \
    typedef disabled_literal_guard narrow; \
    typedef disabled_wide_literal_guard wide; \
};
NX_CB99_CONST_TEXT(const char*)
NX_CB99_CONST_TEXT(const volatile char*)
NX_CB99_CONST_TEXT(const wchar_t*)
NX_CB99_CONST_TEXT(const volatile wchar_t*)
NX_CB99_CONST_TEXT(const void*)
NX_CB99_CONST_TEXT(const volatile void*)
#undef NX_CB99_CONST_TEXT
inline yes source_kind(const iterator_tag*) { return yes(); }
inline no source_kind(...) { return no(); }
// BCC6 rejects cv-only partial specialization, but accepts abstract object
// types in this unevaluated array test. Its extension also allows function
// pointers to convert to void*, so that conversion cannot be the test.
#if defined(__BORLANDC__) && __BORLANDC__ < 0x0570
template<class U> yes object_kind_impl(U (*)[1]) { return yes(); }
template<class U> no object_kind_impl(...) { return no(); }
#else
// Adding const qualifies object types, including abstract classes, but not
// function types. Standard compilers reject arrays of abstract objects.
template<class U> struct const_kind { typedef no type; };
template<class U> struct const_kind<const U> { typedef yes type; };
template<class U> typename const_kind<const U>::type object_kind_impl(int) {
    return typename const_kind<const U>::type();
}
#endif
inline bool enabled(yes) { return true; }
inline bool enabled(no) { return false; }
template<class U> void destroy_pointer(U* p, yes) { delete p; }
template<class U> void destroy_pointer(U*, no) {}

template<class U> U* address(U& value) {
    return reinterpret_cast<U*>(&const_cast<char&>(
        reinterpret_cast<const volatile char&>(value)));
}
template<bool Integral> struct integral_tag { typedef no type; };
template<> struct integral_tag<true> { typedef yes type; };
// size_t/ptrdiff_t may use extended integer types even in C++98. Detect the
// aliases without specializing them twice when they name int/long on 32 bits.
template<class U> struct integral {
    enum { standard_count = same_type<U, std::size_t>::value ||
                            same_type<U, std::ptrdiff_t>::value ||
                            same_type<U, const std::size_t>::value ||
                            same_type<U, const std::ptrdiff_t>::value ||
                            same_type<U, volatile std::size_t>::value ||
                            same_type<U, volatile std::ptrdiff_t>::value ||
                            same_type<U, const volatile std::size_t>::value ||
                            same_type<U, const volatile std::ptrdiff_t>::value };
    typedef typename integral_tag<standard_count>::type type;
};
// BCC6 can retain top-level cv when deducing a value parameter inside another
// template. Explicit cv specializations also avoid its cv-only partial-
// specialization limitation.
#define NX_CB99_INTEGRAL(U) \
template<> struct integral<U> { typedef yes type; }; \
template<> struct integral<const U> { typedef yes type; }; \
template<> struct integral<volatile U> { typedef yes type; }; \
template<> struct integral<const volatile U> { typedef yes type; };
NX_CB99_INTEGRAL(bool)
NX_CB99_INTEGRAL(char)
NX_CB99_INTEGRAL(signed char)
NX_CB99_INTEGRAL(unsigned char)
NX_CB99_INTEGRAL(short)
NX_CB99_INTEGRAL(unsigned short)
NX_CB99_INTEGRAL(int)
NX_CB99_INTEGRAL(unsigned int)
NX_CB99_INTEGRAL(long)
NX_CB99_INTEGRAL(unsigned long)
NX_CB99_INTEGRAL(wchar_t)
#undef NX_CB99_INTEGRAL

template<class U> struct pointer_ops {
    static bool deletable() { return false; }
    static void dispose(U&, std::vector<U*>&) {}
    static void replace(U& value, const U& next, std::vector<U*>&) { value = next; }
};
template<class U> struct pointer_ops<U*> {
    static bool deletable() { return enabled(object_kind_impl<U>(0)); }
    static void dispose(U*& value, std::vector<U**>& index) {
        U* old = value;
        if (!old || !deletable()) return;
        // Null all aliases before invoking the pointee destructor.
        for (std::size_t i = 0; i < index.size(); ++i)
            if (*index[i] == old) *index[i] = 0;
        destroy_pointer(old, object_kind_impl<U>(0));
    }
    static void replace(U*& value, U* const& next, std::vector<U**>& index) {
        U* snapshot = next;
        if (value == snapshot) return;
        dispose(value, index);
        value = snapshot;
    }
};
#define NX_CB99_VOID_POINTER(U) \
template<> struct pointer_ops<U> { \
    static bool deletable() { return false; } \
    static void dispose(U&, std::vector<U*>&) {} \
    static void replace(U& value, U const& next, std::vector<U*>&) { value = next; } \
};
NX_CB99_VOID_POINTER(void*)
NX_CB99_VOID_POINTER(const void*)
NX_CB99_VOID_POINTER(volatile void*)
NX_CB99_VOID_POINTER(const volatile void*)
#undef NX_CB99_VOID_POINTER

template<class It, class Compare> struct node_compare {
    Compare* compare;
    explicit node_compare(Compare& c) : compare(nx_array_cb99_detail::address(c)) {}
    bool operator()(const It& a, const It& b) const { return (*compare)(*a, *b); }
};
template<class U> struct equal_value {
    const U* value;
    explicit equal_value(const U& v) : value(nx_array_cb99_detail::address(v)) {}
    bool operator()(const U& v) const { return v == *value; }
};
}

template<class T, class Allocator = std::allocator<T> >
class nx_array {
public:
    typedef T value_type;
    typedef Allocator allocator_type;
    typedef std::size_t size_type;
    typedef std::ptrdiff_t difference_type;
    typedef T* pointer;
    typedef const T* const_pointer;
    typedef T& reference;
    typedef const T& const_reference;
    typedef typename nx_array_cb99_detail::input_reference<T>::type input_reference;
    typedef std::random_access_iterator_tag iterator_category;
    typedef std::list<T, Allocator> list_type;
    // BCC 5.6 loses the initializer of an ODR-used in-class static constant.
    // Definitions with initializers are provided after the class instead.
    static const int START_POS;
    static const int END_POS;

    class random_access_iterator : public nx_array_cb99_detail::iterator_tag {
        nx_array* owner_;
        size_type index_;
    public:
        typedef std::random_access_iterator_tag iterator_category;
        typedef T value_type;
        typedef std::ptrdiff_t difference_type;
        typedef T* pointer;
        typedef T& reference;
        random_access_iterator(nx_array* owner = 0, size_type i = 0) : owner_(owner), index_(i) {}
        nx_array* container() const { return owner_; }
        size_type index() const { return index_; }
        reference operator*() const { return owner_->at(index_); }
        pointer operator->() const { return nx_array_cb99_detail::address(operator*()); }
        reference operator[](difference_type n) const { return *(*this + n); }
        random_access_iterator& operator++() { ++index_; return *this; }
        random_access_iterator operator++(int) { random_access_iterator r(*this); ++*this; return r; }
        random_access_iterator& operator--() { --index_; return *this; }
        random_access_iterator operator--(int) { random_access_iterator r(*this); --*this; return r; }
        random_access_iterator& operator+=(difference_type n) { index_ += n; return *this; }
        random_access_iterator& operator-=(difference_type n) { index_ -= n; return *this; }
        random_access_iterator operator+(difference_type n) const { random_access_iterator r(*this); return r += n; }
        random_access_iterator operator-(difference_type n) const { random_access_iterator r(*this); return r -= n; }
        difference_type operator-(const random_access_iterator& r) const {
            if (owner_ != r.owner_) throw std::invalid_argument("NxArray: unrelated iterators");
            return static_cast<difference_type>(index_) - static_cast<difference_type>(r.index_);
        }
        bool operator==(const random_access_iterator& r) const { return owner_ == r.owner_ && index_ == r.index_; }
        bool operator!=(const random_access_iterator& r) const { return !(*this == r); }
        bool operator<(const random_access_iterator& r) const { return (*this - r) < 0; }
        bool operator>(const random_access_iterator& r) const { return r < *this; }
        bool operator<=(const random_access_iterator& r) const { return !(r < *this); }
        bool operator>=(const random_access_iterator& r) const { return !(*this < r); }
        friend random_access_iterator operator+(difference_type n, random_access_iterator i) { return i += n; }
    };

    class const_random_access_iterator : public nx_array_cb99_detail::iterator_tag {
        const nx_array* owner_;
        size_type index_;
    public:
        typedef std::random_access_iterator_tag iterator_category;
        typedef T value_type;
        typedef std::ptrdiff_t difference_type;
        typedef const T* pointer;
        typedef const T& reference;
        const_random_access_iterator(const nx_array* owner = 0, size_type i = 0) : owner_(owner), index_(i) {}
        const_random_access_iterator(const random_access_iterator& i) : owner_(i.container()), index_(i.index()) {}
        const nx_array* container() const { return owner_; }
        size_type index() const { return index_; }
        reference operator*() const { return owner_->at(index_); }
        pointer operator->() const { return nx_array_cb99_detail::address(operator*()); }
        reference operator[](difference_type n) const { return *(*this + n); }
        const_random_access_iterator& operator++() { ++index_; return *this; }
        const_random_access_iterator operator++(int) { const_random_access_iterator r(*this); ++*this; return r; }
        const_random_access_iterator& operator--() { --index_; return *this; }
        const_random_access_iterator operator--(int) { const_random_access_iterator r(*this); --*this; return r; }
        const_random_access_iterator& operator+=(difference_type n) { index_ += n; return *this; }
        const_random_access_iterator& operator-=(difference_type n) { index_ -= n; return *this; }
        const_random_access_iterator operator+(difference_type n) const { const_random_access_iterator r(*this); return r += n; }
        const_random_access_iterator operator-(difference_type n) const { const_random_access_iterator r(*this); return r -= n; }
        difference_type operator-(const const_random_access_iterator& r) const {
            if (owner_ != r.owner_) throw std::invalid_argument("NxArray: unrelated iterators");
            return static_cast<difference_type>(index_) - static_cast<difference_type>(r.index_);
        }
        bool operator==(const const_random_access_iterator& r) const { return owner_ == r.owner_ && index_ == r.index_; }
        bool operator!=(const const_random_access_iterator& r) const { return !(*this == r); }
        bool operator<(const const_random_access_iterator& r) const { return (*this - r) < 0; }
        bool operator>(const const_random_access_iterator& r) const { return r < *this; }
        bool operator<=(const const_random_access_iterator& r) const { return !(r < *this); }
        bool operator>=(const const_random_access_iterator& r) const { return !(*this < r); }
        friend const_random_access_iterator operator+(difference_type n, const_random_access_iterator i) { return i += n; }
        friend bool operator==(const random_access_iterator& l, const const_random_access_iterator& r) { return r == l; }
        friend bool operator!=(const random_access_iterator& l, const const_random_access_iterator& r) { return !(r == l); }
        friend bool operator<(const random_access_iterator& l, const const_random_access_iterator& r) { return const_random_access_iterator(l) < r; }
        friend bool operator>(const random_access_iterator& l, const const_random_access_iterator& r) { return r < l; }
        friend bool operator<=(const random_access_iterator& l, const const_random_access_iterator& r) { return !(r < l); }
        friend bool operator>=(const random_access_iterator& l, const const_random_access_iterator& r) { return !(l < r); }
        friend difference_type operator-(const random_access_iterator& l, const const_random_access_iterator& r) { return const_random_access_iterator(l) - r; }
    };

    typedef random_access_iterator iterator;
    typedef const_random_access_iterator const_iterator;
    typedef std::reverse_iterator<iterator> reverse_iterator;
    typedef std::reverse_iterator<const_iterator> const_reverse_iterator;

private:
    typedef typename list_type::iterator node_iterator;
    typedef typename list_type::const_iterator const_node_iterator;
    typedef list_type storage_type;
    storage_type nodes_;
    std::vector<T*> index_;
    void check_index(size_type i) const {
        if (i >= size()) throw std::out_of_range("NxArray: index out of range");
    }
    void check_size(size_type n) const {
        if (n > max_size()) throw std::length_error("NxArray: max_size exceeded");
    }
    size_type position_index(int pos, bool useExc = true) const {
        if (pos == END_POS) return size();
        if (pos < 0 || static_cast<size_type>(pos) > size()) {
            if (useExc) throw std::out_of_range("NxArray: invalid position");
            return size();
        }
        return static_cast<size_type>(pos);
    }
    node_iterator node_at(size_type i) {
        node_iterator p;
        if (i <= size() / 2) {
            p = nodes_.begin();
            std::advance(p, static_cast<difference_type>(i));
        } else {
            p = nodes_.end();
            std::advance(p, -static_cast<difference_type>(size() - i));
        }
        return p;
    }
    void ensure_capacity(size_type required) {
        check_size(required);
        if (required <= capacity()) return;
        const size_type limit = max_size();
        const size_type extra = capacity() / 2 + 1;
        const size_type grown = capacity() > limit - extra ? limit : capacity() + extra;
        reserve((std::max)(required, grown));
    }
    void rebuild() {
        // All callers reserve enough space BEFORE mutating the node list.
        index_.clear();
        for (node_iterator p = nodes_.begin(); p != nodes_.end(); ++p)
            index_.push_back(nx_array_cb99_detail::address(*p));
    }
    void insert_pending(size_type pos, list_type& pending) {
        const size_type n = pending.size();
        if (n > max_size() - size()) throw std::length_error("NxArray: max_size exceeded");
        ensure_capacity(size() + n);
        nodes_.splice(node_at(pos), pending);
        rebuild();
    }
    void replace_pending(list_type& pending) {
        std::vector<T*> next;
        next.reserve((std::max)(capacity(), static_cast<size_type>(pending.size())));
        for (node_iterator p = pending.begin(); p != pending.end(); ++p)
            next.push_back(nx_array_cb99_detail::address(*p));
        nodes_.swap(pending);
        index_.swap(next);
    }
    template<class Container> void replace_and_drain(Container& source) {
        storage_type pending(nodes_.get_allocator());
        stage(pending, source.begin(), source.end(), max_size());
        replace_pending(pending); // pending now retains the old elements
        source.clear();          // finish accessing source before pending dies
    }
    template<class It> static void check_range(It, It, std::input_iterator_tag) {}
    template<class It> static void check_range(It first, It last, std::random_access_iterator_tag) {
        if (first != last && last - first < 0) throw std::invalid_argument("NxArray: reversed range");
    }
    template<class It> static void check_source(It, It, nx_array_cb99_detail::no) {}
    template<class It> static void check_source(It first, It last, nx_array_cb99_detail::yes) {
        if (!first.container() || first.container() != last.container())
            throw std::invalid_argument("NxArray: invalid source range");
        first.container()->checkIterator(first);
        first.container()->checkIterator(last);
        if (first != last) first.container()->check_copy_source();
    }
    template<class It> static void check_source_range(It first, It last) {
        check_source(first, last, nx_array_cb99_detail::source_kind(nx_array_cb99_detail::address(first)));
    }
    template<class It> static void check_source_range(std::reverse_iterator<It> first, std::reverse_iterator<It> last) {
        check_source_range(last.base(), first.base());
    }
    template<class It> void stage(list_type& pending, It first, It last, size_type limit) {
        check_source_range(first, last);
        typedef typename std::iterator_traits<It>::iterator_category category;
        check_range(first, last, category());
        size_type n = 0;
        for (; first != last; ++first) {
            if (n == limit) throw std::length_error("NxArray: range too large");
            pending.push_back(*first);
            ++n;
        }
    }
    void stage_fill(list_type& pending, size_type n, const T& value) {
        // Populate a fully constructed list so cleanup has a stable owner.
        for (size_type i = 0; i < n; ++i) pending.push_back(value);
    }
    template<class Integer> static size_type nonnegative(Integer n) {
        if (n < 0) throw std::length_error("NxArray: negative count");
        const size_type count = static_cast<size_type>(n);
        if (static_cast<Integer>(count) != n) throw std::length_error("NxArray: count too large");
        return count;
    }
    void init_count(size_type count, size_type reserved) {
        check_size(count);
        reserve((std::max)(count, reserved));
        for (size_type i = 0; i < count; ++i) nodes_.push_back(T());
        rebuild();
    }
    template<class It> void init_pair(It first, It last, size_type reserved, nx_array_cb99_detail::no) {
        reserve(reserved);
        storage_type pending(nodes_.get_allocator());
        stage(pending, first, last, max_size());
        replace_pending(pending);
    }
    template<class Integer> void init_pair(Integer count, Integer reserved, size_type extra, nx_array_cb99_detail::yes) {
        if (extra) throw std::invalid_argument("NxArray: third argument requires an iterator range");
        init_count(nonnegative(count), nonnegative(reserved));
    }
    template<class It> void assign_pair(It first, It last, nx_array_cb99_detail::no) {
        storage_type pending(nodes_.get_allocator());
        stage(pending, first, last, max_size());
        replace_pending(pending);
    }
    template<class Integer> void assign_pair(Integer n, Integer value, nx_array_cb99_detail::yes) { T converted = value; assign(nonnegative(n), input_reference(converted)); }
    template<class It> iterator insert_pair(const_iterator pos, It first, It last, nx_array_cb99_detail::no) {
        checkIterator(pos);
        storage_type pending(nodes_.get_allocator());
        stage(pending, first, last, max_size() - size());
        insert_pending(pos.index(), pending);
        return iterator(this, pos.index());
    }
    template<class Integer> iterator insert_pair(const_iterator pos, Integer n, Integer value, nx_array_cb99_detail::yes) {
        T converted = value;
        return insert(pos, nonnegative(n), input_reference(converted));
    }
    void require_equal_allocator(const list_type& other) const {
        if (!(nodes_.get_allocator() == other.get_allocator()))
            throw std::invalid_argument("NxArray: unequal allocators cannot transfer nodes");
    }
    void check_transfer(const nx_array& other) const {
        require_equal_allocator(other.nodes_);
        if (nx_array_cb99_detail::pointer_ops<T>::deletable() &&
            isDeleteItemsOnDestroy != other.isDeleteItemsOnDestroy)
            throw std::invalid_argument("NxArray: ownership modes differ");
    }
    size_type range_finish(size_type start, size_type finish) const {
        check_index(start);
        if (finish == static_cast<size_type>(END_POS) || finish >= size()) finish = size() - 1;
        if (start > finish) throw std::invalid_argument("NxArray: reversed range");
        return finish;
    }

public:
    bool isDeleteItemsOnDestroy;

    explicit nx_array(size_type initial_count = 0, size_type reserve_count = 0)
        : nodes_(), index_(), isDeleteItemsOnDestroy(false) { init_count(initial_count, reserve_count); }
    explicit nx_array(const allocator_type& allocator)
        : nodes_(allocator), index_(), isDeleteItemsOnDestroy(false) {}
    nx_array(const nx_array& other)
        : nodes_(other.get_allocator()), index_(), isDeleteItemsOnDestroy(false) {
        other.check_copy_source();
        reserve(other.capacity());
        nodes_.insert(nodes_.end(), other.nodes_.begin(), other.nodes_.end());
        rebuild();
    }
    nx_array(const nx_array& other, size_type reserved)
        : nodes_(other.get_allocator()), index_(), isDeleteItemsOnDestroy(false) {
        other.check_copy_source();
        reserve((std::max)(reserved, other.size()));
        nodes_.insert(nodes_.end(), other.nodes_.begin(), other.nodes_.end());
        rebuild();
    }
    template<class It> nx_array(It first, It last, size_type reserved = 0)
        : nodes_(), index_(), isDeleteItemsOnDestroy(false) {
        typedef typename nx_array_cb99_detail::integral<It>::type tag;
        init_pair(first, last, reserved, tag());
    }
    nx_array(size_type count, const T* values, size_type reserved = 0)
        : nodes_(), index_(), isDeleteItemsOnDestroy(false) {
        if (count && !values) throw std::invalid_argument("NxArray: null input");
        check_size(count);
        reserve((std::max)(count, reserved));
        if (count) nodes_.insert(nodes_.end(), values, values + count);
        rebuild();
    }
    explicit nx_array(const std::vector<T>& values, size_type reserved = 0)
        : nodes_(), index_(), isDeleteItemsOnDestroy(false) { init_pair(values.begin(), values.end(), reserved, nx_array_cb99_detail::no()); }
    explicit nx_array(const std::deque<T>& values, size_type reserved = 0)
        : nodes_(), index_(), isDeleteItemsOnDestroy(false) { init_pair(values.begin(), values.end(), reserved, nx_array_cb99_detail::no()); }
    explicit nx_array(const list_type& values, size_type reserved = 0)
        : nodes_(values.get_allocator()), index_(), isDeleteItemsOnDestroy(false) { init_pair(values.begin(), values.end(), reserved, nx_array_cb99_detail::no()); }
    ~nx_array() { if (isDeleteItemsOnDestroy) clear_with_delete(); }

    bool owns_pointers() const { return isDeleteItemsOnDestroy && nx_array_cb99_detail::pointer_ops<T>::deletable(); }
    void check_copy_source() const {
        if (owns_pointers() && !empty())
            throw std::invalid_argument("NxArray: copying an owning pointer array is unsafe in C++98; use transfer_from/spliceArray");
    }
    void checkIterator(const_iterator i) const {
        if (i.container() != this) throw std::invalid_argument("NxArray: foreign iterator");
        if (i.index() > size()) throw std::out_of_range("NxArray: iterator out of range");
    }
    iterator begin() { return iterator(this, 0); }
    iterator end() { return iterator(this, size()); }
    const_iterator begin() const { return const_iterator(this, 0); }
    const_iterator end() const { return const_iterator(this, size()); }
    const_iterator cbegin() const { return begin(); }
    const_iterator cend() const { return end(); }
    reverse_iterator rbegin() { return reverse_iterator(end()); }
    reverse_iterator rend() { return reverse_iterator(begin()); }
    const_reverse_iterator rbegin() const { return const_reverse_iterator(end()); }
    const_reverse_iterator rend() const { return const_reverse_iterator(begin()); }
    const_reverse_iterator crbegin() const { return rbegin(); }
    const_reverse_iterator crend() const { return rend(); }

    size_type size() const { return index_.size(); }
    bool empty() const { return index_.empty(); }
    size_type capacity() const { return index_.capacity(); }
    size_type max_size() const {
        size_type list_limit = (std::min)(static_cast<size_type>(nodes_.max_size()), static_cast<size_type>(get_allocator().max_size()));
        return (std::min)((std::min)(list_limit, index_.max_size()),
            static_cast<size_type>((std::numeric_limits<difference_type>::max)()));
    }
    allocator_type get_allocator() const { return nodes_.get_allocator(); }
    void reserve(size_type n) { check_size(n); index_.reserve(n); }
    void shrink_to_fit() { std::vector<T*>(index_).swap(index_); }
    reference operator[](size_type i) { check_index(i); return *index_[i]; }
    const_reference operator[](size_type i) const { check_index(i); return *index_[i]; }
    reference at(size_type i) { return (*this)[i]; }
    const_reference at(size_type i) const { return (*this)[i]; }
    reference front() { return at(0); }
    const_reference front() const { return at(0); }
    reference back() { return at(size() - 1); }
    const_reference back() const { return at(size() - 1); }
    pointer getItemPtr(size_type i) { check_index(i); return index_[i]; }
    const_pointer getItemPtr(size_type i) const { check_index(i); return index_[i]; }
    const T* const* data() const { return index_.empty() ? 0 : &index_[0]; }
    difference_type getItemIndex(const T* p) const {
        for (size_type i = 0; i < size(); ++i) if (index_[i] == p) return static_cast<difference_type>(i);
        return -1;
    }
    bool isPointerValid(const T* p) const { return p && getItemIndex(p) >= 0; }
    bool validate_pointers() const {
        size_type i = 0;
        for (const_node_iterator p = nodes_.begin(); p != nodes_.end(); ++p, ++i)
            if (i >= size() || index_[i] != nx_array_cb99_detail::address(*p)) return false;
        return i == size();
    }
    void clear() { nodes_.clear(); index_.clear(); }
    void assign(size_type n, input_reference value) {
        check_size(n);
        storage_type pending(nodes_.get_allocator());
        stage_fill(pending, n, value);
        replace_pending(pending);
    }
    template<class It> void assign(It first, It last) {
        typedef typename nx_array_cb99_detail::integral<It>::type tag;
        assign_pair(first, last, tag());
    }
    void assign(const nx_array& other) { operator=(other); }
    template<class Container> void assign(const Container& c) { assign(c.begin(), c.end()); }
    nx_array& operator=(const nx_array& other) {
        if (this != &other) { other.check_copy_source(); assign(other.begin(), other.end()); }
        return *this;
    }
    nx_array& operator=(const std::vector<T>& c) { assign(c.begin(), c.end()); return *this; }
    nx_array& operator=(const std::deque<T>& c) { assign(c.begin(), c.end()); return *this; }
    nx_array& operator=(const list_type& c) { assign(c.begin(), c.end()); return *this; }
    nx_array& operator=(const std::pair<const T*, size_type>& c) {
        if (c.second && !c.first) throw std::invalid_argument("NxArray: null input");
        check_size(c.second);
        if (c.second) assign(c.first, c.first + c.second); else clear();
        return *this;
    }
    // Explicit move-assignment replacement. Old destination pointees are NOT deleted.
    void transfer_from(nx_array& source) {
        if (this == &source) return;
        require_equal_allocator(source.nodes_);
        storage_type retired(nodes_.get_allocator());
        reserve(source.size());
        // The source may be a subobject owned by a destination element.
        // Keep old elements alive until ALL source accesses are complete.
        retired.splice(retired.end(), nodes_);
        index_.clear();
        nodes_.splice(nodes_.end(), source.nodes_);
        isDeleteItemsOnDestroy = source.isDeleteItemsOnDestroy;
        rebuild();
        source.index_.clear();
        source.isDeleteItemsOnDestroy = false;
    }
    nx_array& operator<<=(nx_array& c) { transfer_from(c); return *this; }

    void addItem(input_reference value, int position = END_POS, bool useExc = false) {
        const size_type pos = position_index(position, useExc);
        insert(const_iterator(this, pos), value);
    }
    void push_back(input_reference value) { insert(end(), value); }
    void push_front(input_reference value) { insert(begin(), value); }
    iterator insert(const_iterator pos, input_reference value) {
        checkIterator(pos);
        if (size() == max_size()) throw std::length_error("NxArray: max_size exceeded");
        ensure_capacity(size() + 1);
        node_iterator p = nodes_.insert(node_at(pos.index()), value);
        index_.insert(index_.begin() + pos.index(), nx_array_cb99_detail::address(*p));
        return iterator(this, pos.index());
    }
    iterator insert(const_iterator pos, size_type n, input_reference value) {
        checkIterator(pos);
        if (n > max_size() - size()) throw std::length_error("NxArray: max_size exceeded");
        storage_type pending(nodes_.get_allocator());
        stage_fill(pending, n, value);
        insert_pending(pos.index(), pending);
        return iterator(this, pos.index());
    }
    template<class It> iterator insert(const_iterator pos, It first, It last) {
        typedef typename nx_array_cb99_detail::integral<It>::type tag;
        return insert_pair(pos, first, last, tag());
    }
    // Fixed-arity compatibility helpers: construct a temporary T, then COPY it.
    T& emplaceItem(int pos = END_POS) { size_type i=position_index(pos); T value = T(); return *insert(const_iterator(this,i), value); }
    template<class A1> T& emplaceItem(int pos, const A1& a1) { size_type i=position_index(pos); nx_array_cb99_detail::constructor_guard<T>::validate(a1); T value(a1); return *insert(const_iterator(this,i), value); }
    template<class A1, class A2> T& emplaceItem(int pos, const A1& a1, const A2& a2) { size_type i=position_index(pos); T value(a1,a2); return *insert(const_iterator(this,i), value); }
    template<class A1, class A2, class A3> T& emplaceItem(int pos, const A1& a1, const A2& a2, const A3& a3) { size_type i=position_index(pos); T value(a1,a2,a3); return *insert(const_iterator(this,i), value); }
    iterator emplace(const_iterator pos) { checkIterator(pos); T value = T(); return insert(pos, value); }
    template<class A1> iterator emplace(const_iterator pos, const A1& a1) { checkIterator(pos); nx_array_cb99_detail::constructor_guard<T>::validate(a1); T value(a1); return insert(pos, value); }
    template<class A1, class A2> iterator emplace(const_iterator pos, const A1& a1, const A2& a2) { checkIterator(pos); T value(a1,a2); return insert(pos, value); }
    template<class A1, class A2, class A3> iterator emplace(const_iterator pos, const A1& a1, const A2& a2, const A3& a3) { checkIterator(pos); T value(a1,a2,a3); return insert(pos, value); }
    void resize(size_type n) {
        check_size(n);
        if (n < size()) erase(const_iterator(this, n), end());
        else if (n > size()) {
            storage_type pending(nodes_.get_allocator());
            const size_type count = n - size();
            for (size_type i = 0; i < count; ++i) pending.push_back(T());
            insert_pending(size(), pending);
        }
    }
    void resize(size_type n, input_reference value) {
        check_size(n);
        if (n < size()) erase(const_iterator(this, n), end());
        else if (n > size()) insert(end(), n - size(), value);
    }
    void addArray(const nx_array& other, int position = END_POS) {
        other.check_copy_source();
        insert(const_iterator(this, position_index(position, false)), other.begin(), other.end());
    }
    void addArray(const std::vector<T>& c, int position = END_POS) {
        insert(const_iterator(this, position_index(position, false)), c.begin(), c.end());
    }
    void addArray(const std::deque<T>& c, int position = END_POS) {
        insert(const_iterator(this, position_index(position, false)), c.begin(), c.end());
    }
    void addArray(const list_type& c, int position = END_POS) {
        insert(const_iterator(this, position_index(position, false)), c.begin(), c.end());
    }
    void addArray(const T* values, size_type n, int position = END_POS, bool useExc = false) {
        const size_type pos = position_index(position, useExc);
        if (n && !values) throw std::invalid_argument("NxArray: null input");
        if (n > max_size() - size()) throw std::length_error("NxArray: max_size exceeded");
        if (n) insert(const_iterator(this, pos), values, values + n);
    }
    iterator addArray(const nx_array& c, const_iterator pos) { c.check_copy_source(); return insert(pos, c.begin(), c.end()); }
    template<class Container> iterator addArray(const Container& c, const_iterator pos) { return insert(pos, c.begin(), c.end()); }
    void spliceArray(nx_array& source, int position = END_POS) {
        const size_type pos = position_index(position);
        if (&source == this) return;
        check_transfer(source);
        insert_pending(pos, source.nodes_);
        source.index_.clear();
    }
    void mergeArray(nx_array& source, int position = END_POS) { spliceArray(source, position); }
    void spliceArray(list_type& source, int position = END_POS) {
        const size_type pos = position_index(position);
        require_equal_allocator(source);
        insert_pending(pos, source);
    }
    void mergeArray(list_type& source, int position = END_POS) { spliceArray(source, position); }
    void mergeArray(std::vector<T>& source, int position = END_POS) { addArray(source, position); source.clear(); }
    void mergeArray(std::deque<T>& source, int position = END_POS) { addArray(source, position); source.clear(); }
    void transfer_from(list_type& source) {
        require_equal_allocator(source);
        check_size(source.size());
        storage_type retired(nodes_.get_allocator());
        reserve(source.size());
        retired.splice(retired.end(), nodes_);
        index_.clear();
        nodes_.splice(nodes_.end(), source);
        rebuild();
    }
    nx_array& operator<<=(list_type& c) { transfer_from(c); return *this; }
    nx_array& operator<<=(std::vector<T>& c) { replace_and_drain(c); return *this; }
    nx_array& operator<<=(std::deque<T>& c) { replace_and_drain(c); return *this; }

    nx_array& operator+=(input_reference value) { addItem(value); return *this; }
    nx_array& operator+=(const nx_array& c) { addArray(c); return *this; }
    nx_array& operator+=(const std::vector<T>& c) { addArray(c); return *this; }
    nx_array& operator+=(const std::deque<T>& c) { addArray(c); return *this; }
    nx_array& operator+=(const list_type& c) { addArray(c); return *this; }
    nx_array& operator<<(input_reference value) { addItem(value); return *this; }
    nx_array& operator<<(nx_array& c) { spliceArray(c); return *this; }
    nx_array& operator<<(list_type& c) { spliceArray(c); return *this; }
    nx_array& operator<<(std::vector<T>& c) { mergeArray(c); return *this; }
    nx_array& operator<<(std::deque<T>& c) { mergeArray(c); return *this; }
    nx_array operator+(input_reference value) const { nx_array r(*this); r.addItem(value); return r; }
    nx_array operator+(const nx_array& c) const { nx_array r(*this); r.addArray(c); return r; }
    nx_array operator+(const std::vector<T>& c) const { nx_array r(*this); r.addArray(c); return r; }
    nx_array operator+(const std::deque<T>& c) const { nx_array r(*this); r.addArray(c); return r; }
    nx_array operator+(const list_type& c) const { nx_array r(*this); r.addArray(c); return r; }
    nx_array operator+(const std::pair<const T*, size_type>& c) const { nx_array r(*this); r.addArray(c.first, c.second); return r; }

private:
    typedef typename nx_array_cb99_detail::literal_guard<T>::narrow narrow_literal_guard;
    typedef typename nx_array_cb99_detail::literal_guard<T>::wide wide_literal_guard;
    // Deliberately declaration-only: reject unsafe literals with a normal
    // access/type error. Text classes use the public value-taking overloads.
    template<size_type N> void addItem(const narrow_literal_guard (&s)[N], int pos=END_POS, bool useExc=false);
    template<size_type N> void push_back(const narrow_literal_guard (&s)[N]);
    template<size_type N> void push_front(const narrow_literal_guard (&s)[N]);
    template<size_type N> T& emplaceItem(int pos, const narrow_literal_guard (&s)[N]);
    template<size_type N> iterator emplace(const_iterator pos, const narrow_literal_guard (&s)[N]);
    template<size_type N> iterator insert(const_iterator pos, const narrow_literal_guard (&s)[N]);
    template<size_type N> iterator insert(const_iterator pos, size_type n, const narrow_literal_guard (&s)[N]);
    template<size_type N> void assign(size_type n, const narrow_literal_guard (&s)[N]);
    template<size_type N> void resize(size_type n, const narrow_literal_guard (&s)[N]);
    template<size_type N> void replace(size_type i, const narrow_literal_guard (&s)[N]);
    template<size_type N> void replace_with_delete(size_type i, const narrow_literal_guard (&s)[N]);
    template<size_type N> void setValue(const narrow_literal_guard (&s)[N], size_type first, size_type last=static_cast<size_type>(END_POS));
    template<size_type N> void setValue(const narrow_literal_guard (&s)[N], const_iterator first);
    template<size_type N> void setValue(const narrow_literal_guard (&s)[N], const_iterator first, const_iterator last);
    template<size_type N> void setValue(const narrow_literal_guard (&s)[N], iterator first, iterator last);
    template<size_type N> nx_array& operator+=(const narrow_literal_guard (&s)[N]);
    template<size_type N> nx_array& operator<<(const narrow_literal_guard (&s)[N]);
    template<size_type N> nx_array operator+(const narrow_literal_guard (&s)[N]) const;
    template<size_type N> void addItem(const wide_literal_guard (&s)[N], int pos=END_POS, bool useExc=false);
    template<size_type N> void push_back(const wide_literal_guard (&s)[N]);
    template<size_type N> void push_front(const wide_literal_guard (&s)[N]);
    template<size_type N> T& emplaceItem(int pos, const wide_literal_guard (&s)[N]);
    template<size_type N> iterator emplace(const_iterator pos, const wide_literal_guard (&s)[N]);
    template<size_type N> iterator insert(const_iterator pos, const wide_literal_guard (&s)[N]);
    template<size_type N> iterator insert(const_iterator pos, size_type n, const wide_literal_guard (&s)[N]);
    template<size_type N> void assign(size_type n, const wide_literal_guard (&s)[N]);
    template<size_type N> void resize(size_type n, const wide_literal_guard (&s)[N]);
    template<size_type N> void replace(size_type i, const wide_literal_guard (&s)[N]);
    template<size_type N> void replace_with_delete(size_type i, const wide_literal_guard (&s)[N]);
    template<size_type N> void setValue(const wide_literal_guard (&s)[N], size_type first, size_type last=static_cast<size_type>(END_POS));
    template<size_type N> void setValue(const wide_literal_guard (&s)[N], const_iterator first);
    template<size_type N> void setValue(const wide_literal_guard (&s)[N], const_iterator first, const_iterator last);
    template<size_type N> void setValue(const wide_literal_guard (&s)[N], iterator first, iterator last);
    template<size_type N> nx_array& operator+=(const wide_literal_guard (&s)[N]);
    template<size_type N> nx_array& operator<<(const wide_literal_guard (&s)[N]);
    template<size_type N> nx_array operator+(const wide_literal_guard (&s)[N]) const;
public:

    void removeItem(size_type i) { check_index(i); erase(const_iterator(this, i)); }
    void removeRange(size_type start, size_type finish = static_cast<size_type>(END_POS)) {
        if (empty()) return;
        finish = range_finish(start, finish);
        erase(const_iterator(this, start), const_iterator(this, finish + 1));
    }
    iterator erase(const_iterator pos) {
        checkIterator(pos);
        check_index(pos.index());
        nodes_.erase(node_at(pos.index()));
        index_.erase(index_.begin() + pos.index());
        return iterator(this, pos.index());
    }
    iterator erase(const_iterator first, const_iterator last) {
        checkIterator(first); checkIterator(last);
        if (first.index() > last.index()) throw std::invalid_argument("NxArray: reversed range");
        node_iterator a = node_at(first.index()), b = a;
        std::advance(b, static_cast<difference_type>(last.index() - first.index()));
        nodes_.erase(a, b);
        index_.erase(index_.begin() + first.index(), index_.begin() + last.index());
        return iterator(this, first.index());
    }
    iterator erase(const T& value) {
        for (size_type i = 0; i < size(); ++i) if (at(i) == value) return erase(const_iterator(this, i));
        return end();
    }
    template<class Compare> iterator erase(const T& value, Compare compare) {
        for (size_type i = 0; i < size(); ++i) if (compare(at(i), value)) return erase(const_iterator(this, i));
        return end();
    }
    template<class Predicate> iterator erase_if(Predicate predicate) {
        std::vector<size_type> positions;
        for (size_type i = 0; i < size(); ++i) if (predicate(at(i))) positions.push_back(i);
        if (positions.empty()) return end();
        node_iterator p = nodes_.begin();
        size_type i = 0, k = 0;
        while (p != nodes_.end() && k < positions.size()) {
            node_iterator current = p++;
            if (i == positions[k]) { nodes_.erase(current); ++k; }
            ++i;
        }
        rebuild();
        return iterator(this, positions[0]);
    }
    size_type erase_all(const T& value) {
        const size_type old = size();
        erase_if(nx_array_cb99_detail::equal_value<T>(value));
        return old - size();
    }
    size_type erase_range(const_iterator first, const_iterator last) {
        checkIterator(first); checkIterator(last);
        if (first.index() > last.index()) throw std::invalid_argument("NxArray: reversed range");
        size_type n = last.index() - first.index(); erase(first, last); return n;
    }
    // C++98 may throw while copying the return value AFTER removal. Basic
    // guarantee only. extract_to() avoids that second copy; output must be external.
    T extract(size_type i) { check_index(i); T result(at(i)); removeItem(i); return result; }
    T extract(const_iterator i) { checkIterator(i); return extract(i.index()); }
    T pop_back() { if (empty()) throw std::out_of_range("NxArray: empty"); return extract(size() - 1); }
    void extract_to(size_type i, T& output) {
        check_index(i);
        if (isPointerValid(nx_array_cb99_detail::address(output))) throw std::invalid_argument("NxArray: output aliases an element");
        output = at(i);
        removeItem(i);
    }
    void moveItem(size_type from, size_type to) {
        check_index(from); check_index(to);
        if (from == to) return;
        node_iterator source = node_at(from), dest = node_at(to);
        if (from < to) ++dest;
        nodes_.splice(dest, nodes_, source);
        if (from < to) std::rotate(index_.begin() + from, index_.begin() + from + 1, index_.begin() + to + 1);
        else std::rotate(index_.begin() + to, index_.begin() + from, index_.begin() + from + 1);
    }
    void swapItems(size_type i, size_type j) {
        check_index(i); check_index(j);
        if (i == j) return;
        if (j < i) std::swap(i, j);
        node_iterator a = node_at(i), b = node_at(j), after_b = b; ++after_b;
        nodes_.splice(a, nodes_, b);
        nodes_.splice(after_b, nodes_, a);
        std::swap(index_[i], index_[j]);
    }
    bool sort() { return sort(std::less<T>()); }
    template<class Compare> bool sort(Compare compare) {
        if (size() < 2) return true;
        try {
            std::vector<node_iterator> order;
            order.reserve(size());
            for (node_iterator p = nodes_.begin(); p != nodes_.end(); ++p) order.push_back(p);
            std::stable_sort(order.begin(), order.end(), nx_array_cb99_detail::node_compare<node_iterator, Compare>(compare));
            for (size_type i = 0; i < order.size(); ++i) nodes_.splice(nodes_.end(), nodes_, order[i]);
            rebuild();
            return true;
        } catch (...) { return false; }
    }
    void reverse() { nodes_.reverse(); std::reverse(index_.begin(), index_.end()); }
    difference_type find(const T& value, bool* ok = 0) const {
        if (ok) *ok = true;
        try { for (size_type i = 0; i < size(); ++i) if (at(i) == value) return static_cast<difference_type>(i); }
        catch (...) { if (ok) *ok = false; }
        return -1;
    }
    bool contains(const T& value, bool* ok = 0) const { return find(value, ok) >= 0; }
    iterator find_itr(const T& value, bool* ok = 0) { difference_type i = find(value, ok); return i < 0 ? end() : iterator(this, static_cast<size_type>(i)); }
    const_iterator find_itr(const T& value, bool* ok = 0) const { difference_type i = find(value, ok); return i < 0 ? end() : const_iterator(this, static_cast<size_type>(i)); }
    size_type count(const T& value) const { return static_cast<size_type>(std::count(begin(), end(), value)); }
    void setValue(input_reference value, size_type start, size_type finish = static_cast<size_type>(END_POS)) {
        if (empty()) return;
        finish = range_finish(start, finish);
        for (size_type i = start; i <= finish; ++i) at(i) = value;
    }
    void setValue(input_reference value, const_iterator first, const_iterator last) {
        checkIterator(first); checkIterator(last);
        if (first.index() > last.index()) throw std::invalid_argument("NxArray: reversed range");
        for (size_type i = first.index(); i < last.index(); ++i) at(i) = value;
    }
    void setValue(input_reference value, const_iterator first) { setValue(value, first, cend()); }
    template<class Function> void apply(Function f) { for (iterator p = begin(); p != end(); ++p) f(*p); }
    template<class Function> void apply(Function f) const { for (const_iterator p = begin(); p != end(); ++p) f(*p); }
    void replace(size_type i, input_reference value) { at(i) = value; }
    void replace_with_delete(size_type i, input_reference value) {
        check_index(i);
        nx_array_cb99_detail::pointer_ops<T>::replace(at(i), value, index_);
    }
    void clear_with_delete() {
        for (size_type i = 0; i < size(); ++i) nx_array_cb99_detail::pointer_ops<T>::dispose(at(i), index_);
        clear();
    }
    void removeItem_with_delete(size_type i) {
        check_index(i);
        nx_array_cb99_detail::pointer_ops<T>::dispose(at(i), index_);
        removeItem(i);
    }
    void removeItem_with_delete(const_iterator i) { checkIterator(i); removeItem_with_delete(i.index()); }
    void removeRange_with_delete(size_type start, size_type finish = static_cast<size_type>(END_POS)) {
        if (empty()) return;
        finish = range_finish(start, finish);
        for (size_type i = start; i <= finish; ++i) nx_array_cb99_detail::pointer_ops<T>::dispose(at(i), index_);
        removeRange(start, finish);
    }
    list_type toStdList() const { check_copy_source(); return list_type(nodes_); }
    std::vector<T> toStdVector(size_type reserved = 0) const {
        check_copy_source(); std::vector<T> r; r.reserve((std::max)(reserved, size()));
        r.insert(r.end(), nodes_.begin(), nodes_.end()); return r;
    }
    std::deque<T> toStdDeque() const { check_copy_source(); return std::deque<T>(nodes_.begin(), nodes_.end()); }
    list_type ejectStdList() {
        check_copy_source(); list_type r(nodes_.get_allocator());
        r.splice(r.end(), nodes_); index_.clear(); return r;
    }
    std::vector<T> ejectStdVector(size_type reserved = 0) { std::vector<T> r = toStdVector(reserved); clear(); return r; }
    std::deque<T> ejectStdDeque() { std::deque<T> r = toStdDeque(); clear(); return r; }
    // Explicit ownership handoff. Destination must be empty, with equal allocator.
    // Raw-pointer pointees become the caller's responsibility.
    void eject_to(list_type& output) {
        if (!output.empty()) throw std::invalid_argument("NxArray: output list must be empty");
        require_equal_allocator(output);
        output.splice(output.end(), nodes_); index_.clear();
    }
    void swap(nx_array& other) {
        if (this == &other) return;
        require_equal_allocator(other.nodes_);
        nodes_.swap(other.nodes_); index_.swap(other.index_);
        std::swap(isDeleteItemsOnDestroy, other.isDeleteItemsOnDestroy);
    }
    friend void swap(nx_array& a, nx_array& b) { a.swap(b); }
    friend bool operator==(const nx_array& a, const nx_array& b) { return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin()); }
    friend bool operator!=(const nx_array& a, const nx_array& b) { return !(a == b); }
    friend bool operator<(const nx_array& a, const nx_array& b) { return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end()); }
    friend bool operator>(const nx_array& a, const nx_array& b) { return b < a; }
    friend bool operator<=(const nx_array& a, const nx_array& b) { return !(b < a); }
    friend bool operator>=(const nx_array& a, const nx_array& b) { return !(a < b); }
    bool operator==(const std::vector<T>& c) const { return size() == c.size() && std::equal(begin(), end(), c.begin()); }
    bool operator!=(const std::vector<T>& c) const { return !(*this == c); }
    bool operator==(const std::deque<T>& c) const { return size() == c.size() && std::equal(begin(), end(), c.begin()); }
    bool operator!=(const std::deque<T>& c) const { return !(*this == c); }
};

template<class T, class A> const int nx_array<T, A>::START_POS = 0;
template<class T, class A> const int nx_array<T, A>::END_POS = -1;

#if defined(__BORLANDC__) && __BORLANDC__ < 0x0570
#pragma option pop
#endif

#endif
