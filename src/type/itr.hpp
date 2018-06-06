#pragma once

#include <iterator>

template<class Iter>
struct Range
{
	using iterator  = Iter;
	using traits    = std::iterator_traits<Iter>;
	using iterator_category = typename traits::iterator_category;
	using value_type        = typename traits::value_type;
	using difference_type   = typename traits::difference_type;
	using pointer           = typename traits::pointer;
	using reference         = typename traits::reference;

	Range(): Range(Iter{}) {}
	Range(Iter b, Iter e = Iter{}): b(b), e(e) {}

	auto begin() const -> const Iter& { return b; }
	auto end() const -> const Iter&   { return e; }
	auto begin() -> Iter&  { return b; }
	auto end() -> Iter&    { return e; }

	auto data() -> pointer { return &*b; }
	auto data() const -> const pointer { return &*b; }
	auto operator[](size_t n) -> reference { return *std::next(b, n); }
	auto operator[](size_t n) const -> const reference { return *std::next(b, n); }

	auto front() const -> const reference { return *b; }
	auto back() const -> const reference { return *e; }

	auto size() const -> size_t { return std::distance(b,e); }
	auto empty() const -> bool { return b == e; }

	auto advance(size_t n = 1) -> void { std::advance(b, n); }

	operator Range<pointer>() { return range(data(), size()); }

protected:
	Iter b, e;
};

template<class I>
static auto range(I begin, I end) -> Range<I>
{
	return {begin, end};
}

template<class Container, class I = typename Container::const_iterator>
static auto range(const Container& c) -> const Range<I>
{
	return {c.begin(), c.end()};
}

template<class Container, class I = typename Container::iterator>
static auto range(Container& c) -> Range<I>
{
	return {c.begin(), c.end()};
}

template<class T>
static auto range(T* ptr, size_t size) -> Range<T*>
{
	return {ptr, ptr+size};
}

template<class T>
static auto range(const T* ptr, size_t size) -> Range<const T*>
{
	return {ptr, ptr+size};
}
