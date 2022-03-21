#pragma once

#include <utility>

namespace std
{

/*
Minimal std::unique_ptr implementation, close enough for my personal needs
*/
template<typename T>
class unique_ptr
{
private:
	T* m_ptr = nullptr;
public:
	~unique_ptr()
	{
		delete m_ptr;
	}

	constexpr unique_ptr() noexcept { }
	explicit unique_ptr(T* p) noexcept
		: m_ptr(p)
	{
	}

	unique_ptr(unique_ptr&& other) noexcept
	{
		this->swap(other);
	}

	// Constructor/Assignment for use with types derived from T
	template<typename U>
	unique_ptr(unique_ptr<U>&& moving)
	{
		unique_ptr<T> tmp(moving.release());
		tmp.swap(*this);
	}

	template<typename U>
	unique_ptr& operator=(unique_ptr<U>&& moving)
	{
		unique_ptr<T> tmp(moving.release());
		tmp.swap(*this);
		return *this;
	}
		
	// Remove compiler generated copy semantics
	unique_ptr(const unique_ptr&) = delete;
	unique_ptr& operator=(const unique_ptr&) = delete;

	void swap(unique_ptr& other) noexcept
	{
		T* tmp = m_ptr;
		m_ptr = other.m_ptr;
		other.m_ptr = tmp;
	}

	T* operator->() const { return m_ptr; }
	T& operator*() const { return m_ptr; }

	unique_ptr& operator=(unique_ptr&& other) noexcept
	{
		other.swap(*this);
		return* this;
	}

	T* get() const { return m_ptr; }
	explicit operator bool() const { return m_ptr; }

	T* release() noexcept
	{
		T* result = m_ptr;
		m_ptr = nullptr;
		return result;
	}

	void reset()
	{
		T* tmp = release();
		delete tmp;
	}

};

template<typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args)
{
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}
	
} // namespace std

