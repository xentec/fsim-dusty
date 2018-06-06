#pragma once

#include "global.hpp"
#include "type/itr.hpp"

#include <asio/buffer.hpp>
#include <asio/buffers_iterator.hpp>
#include <asio/streambuf.hpp>

#include <vector>
#include <utility>

struct Buffer : asio::streambuf
{
	using const_iterator = asio::buffers_iterator<const_buffers_type, u8>;
	using iterator = asio::buffers_iterator<mutable_buffers_type, u8>;

	using const_range = Range<const_iterator>;
	using range = Range<iterator>;

	inline
	auto data_range() const -> const_range
	{
		const auto bufs = data();
		return const_range(const_iterator::begin(bufs), const_iterator::end(bufs));
	}

	inline
	auto prepare_range(usz n) -> range
	{
		const auto bufs = prepare(n);
		return range(iterator::begin(bufs), iterator::end(bufs));
	}
};

// unformated writing to std::ostream

template<class T, class CharT, class Traits, typename = std::enable_if_t<std::is_pod<T>::value && sizeof(CharT) == 1>>
std::basic_ostream<CharT,Traits>& operator|(std::basic_ostream<CharT,Traits>& os, T value)
{
	os.write(reinterpret_cast<const CharT*>(&value), sizeof(T));
	return os;
}

template < class T, class... Types>
constexpr std::array<T, sizeof...(Types)> arr(Types&&... t) { return {{ std::forward<T>(t)... }}; }
