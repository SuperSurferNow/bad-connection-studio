#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>

namespace bcs {

// Small fixed-capacity queue for live media. When full, push() discards the
// OLDEST item: for a live feed, a fresh frame is always worth more than a
// stale one, and this keeps end-to-end latency bounded.
template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity) : m_capacity(capacity ? capacity : 1) {}

    // Returns the number of items dropped to make room (0 normally).
    std::size_t push(T item)
    {
        std::deque<T> dropped; // destroyed outside the lock
        {
            std::lock_guard lk(m_mutex);
            if (m_closed)
                return 1;
            while (m_items.size() >= m_capacity) {
                dropped.push_back(std::move(m_items.front()));
                m_items.pop_front();
            }
            m_items.push_back(std::move(item));
        }
        m_cv.notify_one();
        return dropped.size();
    }

    std::optional<T> pop(std::chrono::milliseconds timeout)
    {
        std::unique_lock lk(m_mutex);
        m_cv.wait_for(lk, timeout, [&] { return m_closed || !m_items.empty(); });
        if (m_items.empty())
            return std::nullopt;
        T v = std::move(m_items.front());
        m_items.pop_front();
        return v;
    }

    void close()
    {
        {
            std::lock_guard lk(m_mutex);
            m_closed = true;
        }
        m_cv.notify_all();
    }

    void reset()
    {
        std::deque<T> old;
        std::lock_guard lk(m_mutex);
        old.swap(m_items);
        m_closed = false;
    }

    std::size_t size() const
    {
        std::lock_guard lk(m_mutex);
        return m_items.size();
    }

    std::size_t capacity() const { return m_capacity; }

private:
    const std::size_t m_capacity;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<T> m_items;
    bool m_closed = false;
};

} // namespace bcs
