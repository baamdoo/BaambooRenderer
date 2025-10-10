#pragma once
#include <memory>
#include <atomic>
#include <utility>
#include <cassert>
#include <type_traits>

#include "Defines.h"

template< typename T >
using Box = std::unique_ptr< T >;

template< typename T, typename... TArgs >
Box< T > MakeBox(TArgs&&... args)
{
    return std::make_unique< T >(std::forward< TArgs >(args)...);
}

namespace ptr_util
{

BAAMBOO_API bool IsLive(void* ptr);
BAAMBOO_API void AddToLiveReferences(void* ptr);
BAAMBOO_API void RemoveFromLiveReferences(void* ptr);

}

DLLEXPORT_TEMPLATE template struct BAAMBOO_API std::atomic< uint32_t >;
class BAAMBOO_API ArcBase
{
public:
    ArcBase() : m_refCount(0) {}
    virtual ~ArcBase() = default;

    ArcBase(const ArcBase&) = delete;
    ArcBase& operator=(const ArcBase&) = delete;
    ArcBase(ArcBase&&) = delete;
    ArcBase& operator=(ArcBase&&) = delete;

    void AddRef() const 
    {
        m_refCount.fetch_add(1, std::memory_order_relaxed);
    }

    void Release() const 
    {
        m_refCount.fetch_sub(1, std::memory_order_acq_rel);
    }

    uint32_t RefCount() const 
    {
        return m_refCount.load(std::memory_order_relaxed);
    }

private:
    mutable std::atomic< uint32_t > m_refCount;
};

template< typename T >
class Arc 
{

public:
    Arc() noexcept : m_ptr(nullptr) {}
    Arc(std::nullptr_t) noexcept : m_ptr(nullptr) {}

    explicit Arc(T* ptr) noexcept 
        : m_ptr(ptr) 
    {
        static_assert(std::is_base_of_v< ArcBase, T >, "T must derive from ArcBase");

        addRef();
    }

    Arc(const Arc& other) noexcept 
        : m_ptr(other.m_ptr) 
    {
        addRef();
    }

    Arc(Arc&& other) noexcept 
        : m_ptr(other.m_ptr) 
    {
        other.m_ptr = nullptr;
    }

    template< typename U, typename = std::enable_if_t< std::is_convertible_v< U*, T* > > >
    Arc(const Arc< U >& other) noexcept 
        : m_ptr(other.get()) 
    {
        addRef();
    }

    ~Arc() 
    {
        release();
    }

    Arc& operator=(const Arc& other) noexcept 
    {
        Arc(other).swap(*this);
        return *this;
    }

    Arc& operator=(Arc&& other) noexcept 
    {
        Arc(std::move(other)).swap(*this);
        return *this;
    }

    Arc& operator=(std::nullptr_t) noexcept 
    {
        reset();
        return *this;
    }

    void reset(T* ptr = nullptr) noexcept 
    {
        Arc(ptr).swap(*this);
    }

    void swap(Arc& other) noexcept 
    {
        std::swap(m_ptr, other.m_ptr);
    }

    T* get() const noexcept { return m_ptr; }
    T& operator*() const noexcept { assert(m_ptr); return *m_ptr; }
    T* operator->() const noexcept { assert(m_ptr); return m_ptr; }

    explicit operator bool() const noexcept { return m_ptr != nullptr; }

    bool operator==(const Arc& other) const noexcept { return m_ptr == other.m_ptr; }
    bool operator!=(const Arc& other) const noexcept { return m_ptr != other.m_ptr; }
    bool operator<(const Arc& other) const noexcept { return m_ptr < other.m_ptr; }

private:
    void addRef()
    {
        if (m_ptr)
        {
            m_ptr->AddRef();
            ptr_util::AddToLiveReferences((void*)m_ptr);
        }
    }

    void release()
    {
        if (m_ptr)
        {
            m_ptr->Release();
            if (m_ptr->RefCount() == 0)
            {
                delete m_ptr;
                ptr_util::RemoveFromLiveReferences((void*)m_ptr);
                m_ptr = nullptr;
            }
        }
    }

    T* m_ptr;
};

template< typename T, typename... TArgs >
Arc< T > MakeArc(TArgs&&... args) 
{
    return Arc< T >(new T(std::forward< TArgs >(args)...));
}

template< typename T, typename U >
Arc< T > StaticCast(const Arc< U >& ptr) 
{
    return Arc< T >(static_cast<T*>(ptr.get()));
}

template< typename T, typename U >
Arc< T > DynamicCast(const Arc< U >& ptr) 
{
    return Arc< T >(dynamic_cast<T*>(ptr.get()));
}

template< typename T >
class Weak
{
public:
    Weak() = default;
    Weak(Arc< T > arc)
    {
        m_ptr = arc.get();
    }
    Weak(T* ptr)
    {
        m_ptr = ptr;
    }

    T* operator->() { return m_ptr; }
    const T* operator->() const { return m_ptr; }

    T& operator*() { return *m_ptr; }
    const T& operator*() const { return *m_ptr; }

    Arc< T > lock() const noexcept 
    {
        if (ptr_util::IsLive(m_ptr))
        {
            return Arc< T >(m_ptr);
        }

        return nullptr;
    }

    bool valid() const { return m_ptr ? ptr_util::IsLive(m_ptr) : false; }
    operator bool() const { return valid(); }

private:
    T* m_ptr = nullptr;
};
