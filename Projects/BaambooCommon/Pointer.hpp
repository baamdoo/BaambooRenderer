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

namespace ptr_detail
{

struct ControlBlock;

struct StrongRefTag
{
    explicit StrongRefTag() = default;
};

BAAMBOO_API bool TryAddStrong(ControlBlock* pControlBlock) noexcept;
BAAMBOO_API void AddWeak(ControlBlock* pControlBlock) noexcept;
BAAMBOO_API void ReleaseWeak(ControlBlock* pControlBlock) noexcept;
BAAMBOO_API bool IsValid(ControlBlock* pControlBlock) noexcept;

}


class BAAMBOO_API ArcBase
{
public:
    ArcBase();
    virtual ~ArcBase();

    ArcBase(const ArcBase&) = delete;
    ArcBase& operator=(const ArcBase&) = delete;
    ArcBase(ArcBase&&) = delete;
    ArcBase& operator=(ArcBase&&) = delete;

    uint32_t RefCount() const noexcept;

private:
    template< typename T >
    friend class Arc;

    template< typename T >
    friend class Weak;

    void AddRef() const noexcept;
    bool ReleaseRef() const noexcept;

    ptr_detail::ControlBlock* GetControlBlock() const noexcept
    {
        return m_pControlBlock;
    }

    ptr_detail::ControlBlock* m_pControlBlock = nullptr;
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
    template< typename U >
    friend class Weak;

    Arc(T* ptr, ptr_detail::StrongRefTag) noexcept
        : m_ptr(ptr)
    {
        // no add reference
    }

    void addRef()
    {
        if (m_ptr)
        {
            m_ptr->AddRef();
        }
    }

    void release()
    {
        T* ptr = std::exchange(m_ptr, nullptr);

        if (ptr && ptr->ReleaseRef())
        {
            delete ptr;
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
    Weak() noexcept = default;
    Weak(std::nullptr_t) noexcept {}
    Weak(const Arc< T >& arc) noexcept
        : m_ptr(arc.get())
        , m_pControlBlock(m_ptr ? m_ptr->GetControlBlock() : nullptr)
    {
        addWeak();
    }
    Weak(T* ptr) noexcept
        : m_ptr(ptr)
        , m_pControlBlock(ptr ? ptr->GetControlBlock() : nullptr)
    {
        addWeak();
    }
    Weak(const Weak& other) noexcept
        : m_ptr(other.m_ptr)
        , m_pControlBlock(other.m_pControlBlock)
    {
        addWeak();
    }
    Weak(Weak&& other) noexcept
        : m_ptr(std::exchange(other.m_ptr, nullptr))
        , m_pControlBlock(std::exchange(other.m_pControlBlock, nullptr))
    {
    }
    ~Weak() { releaseWeak(); }

    Weak& operator=(Weak other) noexcept
    {
        swap(other);
        return *this;
    }

    Weak& operator=(const Arc< T >& arc) noexcept
    {
        Weak(arc).swap(*this);
        return *this;
    }

    void reset() noexcept
    {
        Weak().swap(*this);
    }

    void swap(Weak& other) noexcept
    {
        std::swap(m_ptr, other.m_ptr);
        std::swap(m_pControlBlock, other.m_pControlBlock);
    }

    Arc< T > lock() const noexcept 
    {
        if (!m_pControlBlock)
            return nullptr;

        if (!ptr_detail::TryAddStrong(m_pControlBlock))
            return nullptr;

        return Arc< T >(m_ptr, ptr_detail::StrongRefTag{}); // ref count is already added by ptr_detail::TryAddStrong
    }

    bool valid() const { return m_ptr && ptr_detail::IsValid(m_pControlBlock); }
    operator bool() const { return valid(); }

private:
    void addWeak() noexcept
    {
        if (m_pControlBlock)
            ptr_detail::AddWeak(m_pControlBlock);
    }

    void releaseWeak() noexcept
    {
        m_ptr = nullptr;

        auto* pControlBlock = std::exchange(m_pControlBlock, nullptr);
        if (pControlBlock)
            ptr_detail::ReleaseWeak(pControlBlock);
    }

    T* m_ptr = nullptr;
    ptr_detail::ControlBlock* m_pControlBlock = nullptr;
};
