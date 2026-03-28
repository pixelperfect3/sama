#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace engine::memory
{

template <typename T, size_t N>
class InlinedVector
{
    static_assert(N > 0, "Inline capacity must be greater than 0");

public:
    using value_type = T;
    using size_type = size_t;
    using difference_type = std::ptrdiff_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;
    using iterator = T*;
    using const_iterator = const T*;

    // -----------------------------------------------------------------------
    // Construction / destruction
    // -----------------------------------------------------------------------

    InlinedVector() noexcept = default;

    ~InlinedVector()
    {
        clear();
        freeHeap();
    }

    InlinedVector(const InlinedVector& other)
    {
        reserve(other.size_);
        for (size_t i = 0; i < other.size_; ++i)
        {
            ::new (data_ + i) T(other.data_[i]);
        }
        size_ = other.size_;
    }

    InlinedVector(InlinedVector&& other) noexcept(
        std::is_nothrow_move_constructible_v<T>)
    {
        if (other.heapAllocated_)
        {
            // Steal the heap buffer directly.
            data_ = other.data_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            heapAllocated_ = true;

            other.data_ = reinterpret_cast<T*>(other.inlineStorage_);
            other.size_ = 0;
            other.capacity_ = N;
            other.heapAllocated_ = false;
        }
        else
        {
            // Move elements from inline storage.
            for (size_t i = 0; i < other.size_; ++i)
            {
                ::new (data_ + i) T(std::move(other.data_[i]));
            }
            size_ = other.size_;
            // Destroy moved-from elements.
            for (size_t i = 0; i < other.size_; ++i)
            {
                other.data_[i].~T();
            }
            other.size_ = 0;
        }
    }

    InlinedVector& operator=(const InlinedVector& other)
    {
        if (this == &other)
        {
            return *this;
        }
        clear();
        reserve(other.size_);
        for (size_t i = 0; i < other.size_; ++i)
        {
            ::new (data_ + i) T(other.data_[i]);
        }
        size_ = other.size_;
        return *this;
    }

    InlinedVector& operator=(InlinedVector&& other) noexcept(
        std::is_nothrow_move_constructible_v<T>)
    {
        if (this == &other)
        {
            return *this;
        }
        clear();
        freeHeap();

        if (other.heapAllocated_)
        {
            data_ = other.data_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            heapAllocated_ = true;

            other.data_ = reinterpret_cast<T*>(other.inlineStorage_);
            other.size_ = 0;
            other.capacity_ = N;
            other.heapAllocated_ = false;
        }
        else
        {
            data_ = reinterpret_cast<T*>(inlineStorage_);
            for (size_t i = 0; i < other.size_; ++i)
            {
                ::new (data_ + i) T(std::move(other.data_[i]));
            }
            size_ = other.size_;
            capacity_ = N;
            heapAllocated_ = false;

            for (size_t i = 0; i < other.size_; ++i)
            {
                other.data_[i].~T();
            }
            other.size_ = 0;
        }
        return *this;
    }

    // -----------------------------------------------------------------------
    // Element access
    // -----------------------------------------------------------------------

    reference operator[](size_t i) noexcept
    {
        assert(i < size_);
        return data_[i];
    }

    const_reference operator[](size_t i) const noexcept
    {
        assert(i < size_);
        return data_[i];
    }

    reference at(size_t i)
    {
        if (i >= size_)
        {
            throw std::out_of_range("InlinedVector::at");
        }
        return data_[i];
    }

    const_reference at(size_t i) const
    {
        if (i >= size_)
        {
            throw std::out_of_range("InlinedVector::at");
        }
        return data_[i];
    }

    reference front() noexcept
    {
        assert(size_ > 0);
        return data_[0];
    }

    const_reference front() const noexcept
    {
        assert(size_ > 0);
        return data_[0];
    }

    reference back() noexcept
    {
        assert(size_ > 0);
        return data_[size_ - 1];
    }

    const_reference back() const noexcept
    {
        assert(size_ > 0);
        return data_[size_ - 1];
    }

    pointer data() noexcept
    {
        return data_;
    }

    const_pointer data() const noexcept
    {
        return data_;
    }

    // -----------------------------------------------------------------------
    // Iterators
    // -----------------------------------------------------------------------

    iterator begin() noexcept { return data_; }
    iterator end() noexcept { return data_ + size_; }
    const_iterator begin() const noexcept { return data_; }
    const_iterator end() const noexcept { return data_ + size_; }
    const_iterator cbegin() const noexcept { return data_; }
    const_iterator cend() const noexcept { return data_ + size_; }

    // -----------------------------------------------------------------------
    // Capacity
    // -----------------------------------------------------------------------

    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    size_t size() const noexcept { return size_; }
    size_t capacity() const noexcept { return capacity_; }

    void reserve(size_t newCap)
    {
        if (newCap <= capacity_)
        {
            return;
        }
        growTo(newCap);
    }

    // -----------------------------------------------------------------------
    // Modifiers
    // -----------------------------------------------------------------------

    void push_back(const T& value)
    {
        if (size_ == capacity_)
        {
            growTo(capacity_ * 2);
        }
        ::new (data_ + size_) T(value);
        ++size_;
    }

    void push_back(T&& value)
    {
        if (size_ == capacity_)
        {
            growTo(capacity_ * 2);
        }
        ::new (data_ + size_) T(std::move(value));
        ++size_;
    }

    template <typename... Args>
    reference emplace_back(Args&&... args)
    {
        if (size_ == capacity_)
        {
            growTo(capacity_ * 2);
        }
        T* p = ::new (data_ + size_) T(std::forward<Args>(args)...);
        ++size_;
        return *p;
    }

    void pop_back() noexcept
    {
        assert(size_ > 0);
        --size_;
        data_[size_].~T();
    }

    iterator erase(iterator pos)
    {
        assert(pos >= begin() && pos < end());
        pos->~T();
        // Shift elements left.
        for (iterator it = pos; it + 1 < end(); ++it)
        {
            ::new (static_cast<void*>(it)) T(std::move(*(it + 1)));
            (it + 1)->~T();
        }
        --size_;
        return pos;
    }

    void clear() noexcept
    {
        for (size_t i = 0; i < size_; ++i)
        {
            data_[i].~T();
        }
        size_ = 0;
    }

    void resize(size_t newSize)
    {
        if (newSize > capacity_)
        {
            growTo(newSize);
        }
        if (newSize > size_)
        {
            for (size_t i = size_; i < newSize; ++i)
            {
                ::new (data_ + i) T();
            }
        }
        else
        {
            for (size_t i = newSize; i < size_; ++i)
            {
                data_[i].~T();
            }
        }
        size_ = newSize;
    }

    void resize(size_t newSize, const T& value)
    {
        if (newSize > capacity_)
        {
            growTo(newSize);
        }
        if (newSize > size_)
        {
            for (size_t i = size_; i < newSize; ++i)
            {
                ::new (data_ + i) T(value);
            }
        }
        else
        {
            for (size_t i = newSize; i < size_; ++i)
            {
                data_[i].~T();
            }
        }
        size_ = newSize;
    }

private:
    void growTo(size_t newCap)
    {
        assert(newCap > capacity_);
        T* newData = static_cast<T*>(::operator new(sizeof(T) * newCap));
        for (size_t i = 0; i < size_; ++i)
        {
            ::new (newData + i) T(std::move(data_[i]));
            data_[i].~T();
        }
        freeHeap();
        data_ = newData;
        capacity_ = newCap;
        heapAllocated_ = true;
    }

    void freeHeap() noexcept
    {
        if (heapAllocated_)
        {
            ::operator delete(data_);
            data_ = reinterpret_cast<T*>(inlineStorage_);
            capacity_ = N;
            heapAllocated_ = false;
        }
    }

    alignas(T) std::byte inlineStorage_[sizeof(T) * N];
    T* data_ = reinterpret_cast<T*>(inlineStorage_);
    size_t size_ = 0;
    size_t capacity_ = N;
    bool heapAllocated_ = false;
};

}  // namespace engine::memory
