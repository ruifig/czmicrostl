/**
Minimal std::vector implementation.

It's not meant to be a full replacement, so depending on needs, some things my be missing.
*/

#pragma once

#include <assert.h>
#include <type_traits>
#include <cstddef>
#include <algorithm>
#include <utility>
#include <new.h>

#define CZ_VECTOR_ASSERT(x) assert(x)
#define CZ_VECTOR_ASSERT_SLOW(x) assert(x)

#define CZ_VECTOR_CHECK_ITERATOR(iter) \
	CZ_VECTOR_ASSERT_SLOW(iter >= _ptrAt(0) && iter <= _ptrAt(m_size));
#define CZ_VECTOR_CHECK_ITERATOR_DEREFERANCEABLE(iter) \
	CZ_VECTOR_ASSERT_SLOW(iter >= _ptrAt(0) && iter < _ptrAt(m_size));
#define CZ_VECTOR_CHECK_ITERATOR_RANGE(first, last) \
	CZ_VECTOR_ASSERT_SLOW(first <= last && first>=_ptrAt(0) && last <= _ptrAt(m_size))
#define CZ_VECTOR_CHECK_ITERATOR_EXTERNAL_RANGE(first, last) \
	CZ_VECTOR_ASSERT_SLOW(first <= last && (last <_ptrAt(0) || first>=_ptrAt(m_capacity)))

#define CZ_DEBUG 1

namespace cz
{

/**
 * VectorAllocator provides a simple wrapper around malloc/free, so I can have checks for correct memory allocation/deallocation
 * during unit tests (if CZ_VECTOR_UNITTEST_ALLOCATOR is 1)
 */
namespace detail
{
#if !defined(CZ_VECTOR_UNITTEST_ALLOCATOR) || CZ_VECTOR_UNITTEST_ALLOCATOR==0
	struct VectorAllocator
	{
		static inline void* _alloc(size_t bytes)
		{
			void* ptr = malloc(bytes);
			CZ_VECTOR_ASSERT(ptr);
			return ptr;
		}

		static void _free(void* ptr)
		{
			free(ptr);
		}
	};
#endif
} // namespace detail


namespace detail
{
	template<typename T>
	class base_vector
	{
	public:
		using size_type = std::size_t;

	protected:

		template<typename A, class B = A>
		static A _exchange(A& val, B&& newVal)
		{
			A oldVal = static_cast<A&&>(val);
			val = static_cast<B&&>(newVal);
			return oldVal;
		}

		template<typename Type>
		static Type _min(Type a, Type b)
		{
			return a <= b ? a : b;
		}

		//
		// Construct a single element at the given position.
		// Depending on Args, it can do default construction, copy construction or move construction
		template<typename... Args>
		static void _constructSingle(void* at, Args&&... args)
		{
			new(at) T(std::forward<Args>(args)...);
		}

		// Constructs N elements start at the given location
		// Depending on Args, it can do default construction or copy construction
		// Move construction doesn't make sense, because we can't be moving something to multiple elements
		template<typename... Args>
		static void _constructN(void* at, size_type count, Args&&... args)
		{
			while (count--)
			{
				new(at) T(std::forward<Args>(args)...);
				at = reinterpret_cast<T*>(at) + 1;
			}
		}

		//
		// copy construct [first, last) to new memory [dest, ...)
		static void _copyConstructRange(const T* first, const T* last, T* dest)
		{
			if constexpr (std::is_trivially_copy_constructible_v<T>)
			{
				const size_type size = static_cast<size_type>(last - first) * sizeof(T);
				memmove(dest, first, size);
			}
			else
			{
				while (first != last)
				{
					new(dest) T(*first);
					++dest;
					++first;
				}
			}
		}

		static void _destroySingle(T* pos)
		{
			if constexpr (!std::is_trivially_destructible_v<T>)
			{
				pos->~T();
			}

#if CZ_DEBUG
			memset(reinterpret_cast<void*>(pos), 0xDD, sizeof(T));
#endif
		}

		// Destroyed [first, last)
		static void _destroyRange(T* first, T* last)
		{
#if CZ_DEBUG
			T* tmp = first;
#endif

			if constexpr (!std::is_trivially_destructible_v<T>)
			{
				while (first != last)
				{
					first->~T();
					++first;
				}
			}

#if CZ_DEBUG
			memset(reinterpret_cast<void*>(tmp), 0xDD, (last - tmp) * sizeof(T));
#endif
		}
		
		// Move constructs [first, last) to new memory [dest,...)
		static void _moveConstructRange(T* first, T* last, T* dest)
		{
			if constexpr (std::is_trivially_move_constructible_v<T>)
			{
				const size_type size = static_cast<size_type>(last - first) * sizeof(T);
				memmove(reinterpret_cast<void*>(dest), first, size);
			}
			else
			{
				while (first != last)
				{
					new(dest) T(std::move(*first));
					++dest;
					++first;
				}
			}
		}
		
		// Copy assigns [first, last) to [dest,...)
		static T* _copyAssignRange(const T* first, const T* last, T* dest)
		{
			if constexpr(std::is_trivially_copy_assignable_v<T>)
			{
				const size_type count = static_cast<size_type>(last - first);
				memmove(reinterpret_cast<void*>(dest), first, count * sizeof(T));
				return dest + count;
			}

			while(first != last)
			{
				*dest = *first;
				++dest;
				++first;
			}
			return dest;
		}

		// Moves [first, last) to [dest,...)
		// Returns a pointer after the last pos we moved to. As-in: "dest + count"
		static T* _moveAssignRange(T* first, T* last, T* dest)
		{
			if constexpr (std::is_trivially_move_assignable_v<T>)
			{
				const size_type count = static_cast<size_type>(last - first);
				memmove(reinterpret_cast<void*>(dest), first, count * sizeof(T));
				return dest + count;
			}

			while (first != last)
			{
				*dest = std::move(*first);
				++dest;
				++first;
			}
			return dest;
		}

		//
		// move [first, last) to [..., dest)
		T* _moveAssignBackwardRange(T* first, T* last, T* dest)
		{
			if constexpr (std::is_trivially_move_assignable_v<T>)
			{
				const size_type count = static_cast<size_type>(last - first);
				memmove(reinterpret_cast<void*>(dest - count), first, count * sizeof(T));
			}
			else
			{
				while (first != last)
				{
					--dest;
					--last;
					*dest = std::move(*last);
				}
			}

			return dest;
		}

		T* _allocate(size_type capacity)
		{
			if (capacity == 0)
			{
				return nullptr;
			}
			else
			{
				T* ptr = reinterpret_cast<T*>(VectorAllocator::_alloc(capacity * sizeof(T)));
#if CZ_DEBUG
				memset(reinterpret_cast<void*>(ptr), 0xCD, capacity * sizeof(T));
#endif
				return ptr;
			}
		}

		void _free(void* ptr)
		{
			VectorAllocator::_free(ptr);
		}

	};

} // namespace detail

template<typename T>
class vector : public detail::base_vector<T>
{
private:
	using util = detail::base_vector<T>;
	using value_type = T;
	using size_type = std::size_t;
	using reference = value_type&;
	using const_reference = const value_type&;
public:
	
	constexpr vector() noexcept {}

	explicit vector(size_type count) noexcept
		: m_data(util::_allocate(count))
		, m_capacity(count)
		, m_size(count)
	{
		util::_constructN(m_data, count);
	}

	explicit vector(size_type count, const T& value) noexcept
		: m_data(util::_allocate(count))
		, m_capacity(count)
		, m_size(count)
	{
		util::_constructN(m_data, count, value);
	}

	vector(const vector& other) noexcept
		: m_data(util::_allocate(other.m_size))
		, m_capacity(other.m_capacity)
		, m_size(other.m_size)
	{
		util::_copyConstructRange(other._ptrAt(0), other._ptrAt(other.m_size), _ptrAt(0));
	}

	vector(vector&& other) noexcept
		: m_data(util::_exchange(other.m_data, nullptr))
		, m_capacity(util::_exchange(other.m_capacity, 0))
		, m_size(util::_exchange(other.m_size, 0))
	{
	}

	vector& operator=(const vector& other) noexcept
	{
		if (this != &other)
		{
			assign(other.begin(), other.end());
		}
		return *this;
	}

	vector& operator=(vector&& other) noexcept
	{
		if (this != &other)
		{
			_tidy();
			m_data = util::_exchange(other.m_data, nullptr);
			m_size = util::_exchange(other.m_size, 0);
			m_capacity = util::_exchange(other.m_capacity, 0);
		}
		return *this;
	}

	~vector() noexcept
	{
		_tidy();
	}

	//
	// Element access
	//
	T* data()
	{
		return static_cast<T*>(m_data);
	}
	const T* data() const
	{
		return static_cast<const T*>(m_data);
	}

	// "at" is not implemented since it requires throwing a std::out_of_range exception
	/*
	T& at(size_type pos);
	const T& at(size_type pos);
	*/

	T& operator[](size_type pos)
	{
		CZ_VECTOR_ASSERT_SLOW(pos < m_size);
		return _refAt(pos);
	}
	
	const T& operator[](size_type pos) const
	{
		CZ_VECTOR_ASSERT_SLOW(pos < m_size);
		return _refAt(pos);
	}

	T& front()
	{
		CZ_VECTOR_ASSERT_SLOW(m_size);
		return _refAt(0);
	}
	
	const T& front() const
	{
		CZ_VECTOR_ASSERT_SLOW(m_size);
		return _refAt(0);
	}

	T& back()
	{
		CZ_VECTOR_ASSERT_SLOW(m_size);
		return _refAt(m_size-1);
	}
	
	const T& back() const
	{
		CZ_VECTOR_ASSERT_SLOW(m_size);
		return _refAt(m_size-1);
	}

	//
	// Iterators
	//
	T* begin() noexcept
	{
		return _ptrAt(0);
	}

	const T* begin() const noexcept
	{
		return _ptrAt(0);
	}

	T* end() noexcept
	{
		return _ptrAt(m_size);
	}

	const T* end() const noexcept
	{
		return _ptrAt(m_size);
	}


	//
	// Capacity related methods
	//
	bool empty() const noexcept
	{
		return m_size==0 ? true : false;
	}

	size_type size() const noexcept
	{
		return m_size;
	}

	void reserve(size_type newCapacity)
	{
		if (newCapacity > m_capacity)
		{
			_setCapacity(newCapacity);
		}
	}

	size_type capacity() const
	{
		return m_capacity;
	}

	void shrink_to_fit()
	{
		_setCapacity(m_size);
	}

	//
	// Modifiers API
	//
	void clear() noexcept
	{
		_clear();
	}

	template<typename... Args>
	T& emplace_back(Args&&... args)
	{
		if (m_size == m_capacity)
		{
			_setCapacity(m_capacity+1);
		}
		return _emplace_back_with_unused_capacity(std::forward<Args>(args)...);
	}

	template<typename... Args>
	T* emplace(T* pos, Args&&... args)
	{
		CZ_VECTOR_CHECK_ITERATOR(pos);

		if (m_size < m_capacity) // check if no reallocation needed
		{
			if (pos == _ptrAt(m_size)) // insert at the back
			{
				return &_emplace_back_with_unused_capacity(std::forward<Args>(args)...);
			}
			else
			{
				// handle aliasing (passing a reference to an element in this vector)
				T value(std::forward<Args>(args)...);
				// The last element move constructed into the one after (the new vector back)
				util::_constructSingle(_ptrAt(m_size), std::move(_refAt(m_size-1)));
				util::_moveAssignBackwardRange(pos, _ptrAt(m_size-1), _ptrAt(m_size));
				m_size++;
				*pos = std::move(value);
			}
			return pos;
		}
		else
		{
			return _emplace_reallocate(pos, std::forward<Args>(args)...);
		}
	}

	T* insert(T* pos, const T& value)
	{
		return emplace(pos, value);
	}

	T* insert(T* pos, T&& value)
	{
		return emplace(pos, std::move(value));
	}

	T* erase(const T* _pos)
	{
		T* pos = const_cast<T*>(_pos);
		CZ_VECTOR_CHECK_ITERATOR_DEREFERANCEABLE(pos);
		util::_moveAssignRange(pos + 1, _ptrAt(m_size), pos);
		util::_destroySingle(_ptrAt(m_size-1));
		m_size--;
		return pos;
	}

	// erases [first, last)
	T* erase(const T* _first, const T* _last)
	{
		CZ_VECTOR_CHECK_ITERATOR_RANGE(_first, _last);
		T* first = const_cast<T*>(_first);
		T* last = const_cast<T*>(_last);

		if (first != last) // Standard says erasing an empty range is a no-op
		{
			CZ_VECTOR_CHECK_ITERATOR_DEREFERANCEABLE(first);
			CZ_VECTOR_CHECK_ITERATOR(last);

			// We do two steps
			// 1. move assign [last, end) over to the elements we want to erase (starts at "first")
			T* newLast = util::_moveAssignRange(last, _ptrAt(m_size), first); 
			util::_destroyRange(newLast, _ptrAt(m_size));
			m_size -= last - first;
		}

		return first;
	}

	void assign(const T* first, const T* last)
	{
		CZ_VECTOR_CHECK_ITERATOR_EXTERNAL_RANGE(first, last);
		_assign_range(first, last);
	}

	void push_back(const T& value)
	{
		emplace_back(value);
	}

	void push_back(T&& value)
	{
		emplace_back(std::move(value));
	}

	void pop_back()
	{
		CZ_VECTOR_ASSERT_SLOW(m_size>0);
		util::_destroyRange(_ptrAt(m_size-1), _ptrAt(m_size));
		m_size--;
	}

	//
	// operators
	//
	friend bool operator==(const vector<T>& a, const vector<T>& b)
	{
		if (a.m_size != b.m_size)
		{
			return false;
		}

		return std::equal(a._ptrAt(0), a._ptrAt(a.m_size), b._ptrAt(0));
	}

	friend bool operator!=(const vector<T>& a, const vector<T>& b)
	{
		return !(operator==(a,b));
	}

	

private:

	// Assigns a range
	void _assign_range(const T* first, const T* last)
	{
         const size_t newSize = last - first;
		
		if constexpr (std::is_trivially_copyable_v<T>)
		{
			if (newSize > m_capacity)
			{
				_clearAndSetCapacity(newSize);
			}

			memmove(_ptrAt(0), first, newSize * sizeof(T));
			m_size = newSize;
		}
		else
		{

			if (newSize > m_size)
			{
				if (newSize > m_capacity)
				{
					_clearAndSetCapacity(newSize);
				}
				
				// We need to do two blocks. copy assign the first part, and copy construct the second
				// Note that if newSize is bigger than current capacity, the vector was cleared above, and thus m_size is 0 and no copy assignments will happen
				util::_copyAssignRange(first, first + m_size, _ptrAt(0));
				util::_copyConstructRange(first + m_size, last, _ptrAt(m_size));
			}
			else
			{
				util::_copyAssignRange(first, last, _ptrAt(0));
				util::_destroyRange(_ptrAt(newSize), _ptrAt(m_size));
			}

			m_size = newSize;
		}
	}

	template<typename... Args>
	T* _emplace_reallocate(const T* pos, Args&&... args)
	{
		CZ_VECTOR_ASSERT_SLOW(m_size == m_capacity);
		
		const size_type newSize = m_size +1;
		const size_type newCapacity = newSize;
		const size_type posIndex = _ptrToIndex(pos);

		T* newVec = util::_allocate(newCapacity);

		// create the new element at the desired position
		util::_constructSingle(newVec + posIndex, std::forward<Args>(args)...);

		// If we are inserting at the last position, we can just move [ begin(), end() )  to the new vector
		if (posIndex == m_size)
		{
			// Move all elements to the new memory
			util::_moveConstructRange(_ptrAt(0), _ptrAt(m_size), newVec);
		}
		else
		{
			// If we are inserting between members, we need to split the move into two blocks
			util::_moveConstructRange(_ptrAt(0), _ptrAt(posIndex), newVec);
			util::_moveConstructRange(_ptrAt(posIndex), _ptrAt(m_size), newVec + posIndex + 1); 
		}

		_changeArray(newVec, newSize, newCapacity);
		return _ptrAt(posIndex);
	}

	void _tidy()
	{
		if (m_data)
		{
			util::_destroyRange(_ptrAt(0), _ptrAt(m_size));
			util::_free(m_data);
			m_data = nullptr;
			m_size = 0;
			m_capacity = 0;
		}
	}

	//
	// Replaces all internals with a set of fully constructed data
	void _changeArray(T* newVec, size_type newSize, size_type newCapacity)
	{
		if (m_data)
		{
			util::_destroyRange(_ptrAt(0), _ptrAt(m_size));
			util::_free(m_data);
		}

		m_data = newVec;
		m_size = newSize;
		m_capacity = newCapacity;
	}

	inline const T& _refAt(size_type index) const
	{
		return *(reinterpret_cast<const T*>(&(((char*)m_data)[sizeof(T) * index])));
	}

	inline T& _refAt(size_type index)
	{
		return *(reinterpret_cast<T*>(&(((char*)m_data)[sizeof(T) * index])));
	}

	inline const T* _ptrAt(size_type index) const
	{
		return reinterpret_cast<const T*>(&(((char*)m_data)[sizeof(T) * index]));
	}

	inline T* _ptrAt(size_type index)
	{
		return reinterpret_cast<T*>(&(((char*)m_data)[sizeof(T) * index]));
	}

	inline size_type _ptrToIndex(const T* pos)
	{
		return pos - (T*)m_data;
	}

	template<typename... Args>
	T& _emplace_back_with_unused_capacity(Args&&... args)
	{
		T* ptr = _ptrAt(m_size);
		util::_constructSingle(ptr, std::forward<Args>(args)...);
		++m_size;
		return *ptr;
	}

	// Can be used to grow or shrink to fit.
	// If newCapacity is 0, it will deallocate the buffer
	void _setCapacity(size_type newCapacity)
	{
		CZ_VECTOR_ASSERT_SLOW(newCapacity >= m_size);

		if (newCapacity == m_capacity)
		{
			return;
		}
		else if (newCapacity == 0)
		{
			util::_free(m_data);
			m_data = nullptr;
			m_capacity = 0;
		}
		else
		{
			T* newVec = util::_allocate(newCapacity);

			if (m_size)
			{
				T* oldFirst = _ptrAt(0);
				T* oldLast = _ptrAt(m_size);
				util::_moveConstructRange(oldFirst, oldLast, newVec);
				util::_destroyRange(oldFirst, oldLast);
			}
			
			util::_free(m_data);
			m_data = newVec;
			m_capacity = newCapacity;
		}
	}

	void _clear()
	{
		if (m_size)
		{
			util::_destroyRange(_ptrAt(0), _ptrAt(m_size));
			m_size = 0;
		}
	}

	void _clearAndSetCapacity(size_t newCapacity)
	{
		_clear();
		_setCapacity(newCapacity);
	}
	

	void* m_data = nullptr;
	size_type m_capacity = 0;
	size_type m_size = 0;
};

}

