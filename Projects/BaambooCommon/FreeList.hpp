#pragma once

namespace baamboo
{

template< typename TIndex = unsigned >
class FreeList
{
public:
    /**
     * @brief Allocate an index.
     *
     * @return IndexType The allocated index.
     */
    TIndex allocate() {
        if (m_freeIndices.empty()) 
        {
            return m_nextIndex++;
        }
        else 
        {
            TIndex index = m_freeIndices.back();
            m_freeIndices.pop_back();
            return index;
        }
    }

    /**
     * @brief Release an index.
     *
     * @param index The index to release.
     * @throws std::out_of_range if the index is not valid.
     */
    void release(TIndex index)
	{
        if (index >= m_nextIndex)
            throw std::out_of_range("Invalid index");

        m_freeIndices.push_back(index);
    }

    /**
     * @brief Clear the free list and reset the next index.
     */
    void clear()
	{
        m_freeIndices.clear();
        m_nextIndex = 0;
    }

    /**
     * @brief Reserve capacity for free indices.
     *
     * @param capacity The capacity to reserve.
     */
    void reserve(size_t capacity)
	{
        m_freeIndices.reserve(capacity);
    }

    [[nodiscard]]
    inline TIndex size() const { return m_nextIndex; }
    [[nodiscard]]
    inline size_t freeCount() const { return m_freeIndices.size(); }

private:
    std::vector< TIndex > m_freeIndices;
    TIndex                m_nextIndex = 0;
};

} // namespace baamboo