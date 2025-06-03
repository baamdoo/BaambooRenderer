#pragma once
#include <queue>
#include <mutex>

namespace baamboo
{

template< typename T >
class ThreadQueue
{
public:
    void push(T&& value)
	{
        std::lock_guard< std::mutex > lock(m_mutex);
        m_queue.push(std::move(value));
        m_cv.notify_one();
    }

    // pop & push
    void replace(T&& value)
    {
        assert(!empty());

        std::unique_lock< std::mutex > lock(m_mutex);
        m_queue.pop();
        m_queue.push(std::move(value));
        m_cv.notify_one();
    }

    void pop(T& value)
	{
        std::unique_lock< std::mutex > lock(m_mutex);
        m_cv.wait(lock, [this] { return !m_queue.empty() || m_closed; });

        if (!m_closed)
        {
            value = m_queue.front();
            m_queue.pop();
        }
    }

    std::optional< T > try_pop()
	{
        std::lock_guard< std::mutex > lock(m_mutex);
        if (m_queue.empty())
            return std::nullopt;

        T value = m_queue.front();
        m_queue.pop();
        return value;
    }

    void clear()
    {
        std::lock_guard< std::mutex > lock(m_mutex);

        m_queue = std::queue< T >();
    }

    bool empty() const
	{
        std::lock_guard< std::mutex > lock(m_mutex);
        return m_queue.empty();
    }

    size_t size() const
	{
        std::lock_guard< std::mutex > lock(m_mutex);
        return m_queue.size();
    }

private:
    std::queue< T >         m_queue;
    mutable std::mutex      m_mutex;
    std::condition_variable m_cv;

    std::atomic_bool m_closed = false;
};

} // namespace baamboo