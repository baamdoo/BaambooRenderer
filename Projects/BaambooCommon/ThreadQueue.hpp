#pragma once
#include <queue>
#include <mutex>
#include <optional>

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

    // a combined function to block data race by pushing and replacing simultaneously from different threads
    void push_or_replace(T&& value, uint32_t capacity)
    {
        std::unique_lock< std::mutex > lock(m_mutex);

        assert(m_queue.size() <= capacity);
        if (m_queue.size() == capacity)
            m_queue.pop();

        m_queue.push(std::move(value));

        m_cv.notify_one();
    }

    std::optional< T > try_pop()
    {
        std::lock_guard< std::mutex > lock(m_mutex);
        if (m_queue.empty())
            return std::nullopt;

        T value = std::move(m_queue.front());
        m_queue.pop();
        return value;
    }

    std::optional< T > wait_pop()
    {
        std::unique_lock< std::mutex > lock(m_mutex);
        m_cv.wait(lock, [this] { return !m_queue.empty() || m_closed; });

        if (!m_closed)
        {
            T value = std::move(m_queue.front());
            m_queue.pop();
            return value;
        }

        return std::nullopt;
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

    void open()
    {
        std::lock_guard lock(m_mutex);
        m_closed = false;
    }

    void close()
    {
        {
            std::lock_guard lock(m_mutex);
            m_closed = true;
        }
        m_cv.notify_all();
    }

private:
    std::queue< T >         m_queue;
    mutable std::mutex      m_mutex;
    std::condition_variable m_cv;

    bool m_closed = false;
};

} // namespace baamboo