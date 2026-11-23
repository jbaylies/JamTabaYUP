/*
    JamTabaYUP - a NINJAM client
    Copyright (C) 2026 JamTabaYUP contributors

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version. See LICENSE.
*/

#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace jamtaba::audio
{

/** Single producer, single consumer queue. push() and pop() never lock or
    allocate; the storage is allocated once, by the constructor or resize(),
    while neither side is using the queue. */
template <typename T>
class SpscQueue
{
public:
    explicit SpscQueue (std::size_t capacity = 0) { resize (capacity); }

    /** Not thread safe: only while nobody pushes or pops. */
    void resize (std::size_t capacity)
    {
        items.assign (capacity + 1, T {});
        head.store (0);
        tail.store (0);
    }

    bool push (const T& item) noexcept
    {
        const auto t = tail.load (std::memory_order_relaxed);
        const auto next = increment (t);
        if (next == head.load (std::memory_order_acquire))
            return false;

        items[t] = item;
        tail.store (next, std::memory_order_release);
        return true;
    }

    bool pop (T& item) noexcept
    {
        const auto h = head.load (std::memory_order_relaxed);
        if (h == tail.load (std::memory_order_acquire))
            return false;

        item = items[h];
        head.store (increment (h), std::memory_order_release);
        return true;
    }

    bool isEmpty() const noexcept
    {
        return head.load (std::memory_order_acquire) == tail.load (std::memory_order_acquire);
    }

private:
    std::size_t increment (std::size_t index) const noexcept
    {
        return index + 1 == items.size() ? 0 : index + 1;
    }

    std::vector<T> items;
    std::atomic<std::size_t> head { 0 };
    std::atomic<std::size_t> tail { 0 };
};

} // namespace jamtaba::audio
