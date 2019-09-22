#ifndef CIRCULARBUFFER_H
#define CIRCULARBUFFER_H

#include <iostream>
#include <vector>
#include <set>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <functional>

template<typename T>
class CircularBuffer
{
public:
    static_assert(std::is_copy_constructible<T>::value, "CircularBuffer requires copying. Maybe consider using a pointer.");
    class CircularItem;
    class Holder;

    CircularBuffer() = default;
    CircularBuffer(std::size_t n) {
        resize(n);
    }
    virtual ~CircularBuffer()
    {
        clear();
    }

    /// Not implemented yet.
    CircularBuffer(const CircularBuffer &) = delete;
    CircularBuffer &operator=(const CircularBuffer &) = delete;

    /// Prepare n default items.
    void resize(std::size_t n)
    {
        clear();
        for (unsigned i = 0; i < n; ++i) {
            items.push_back(new CircularItem(this, new T{}));
        }

        // Connect items to circle
        for (unsigned i = 0; i < items.size() - 1; ++i) {
            items[i]->setNext(items[i + 1]);
            items[i + 1]->setLast(items[i]);
        }

        // Current item, next is always final
        current = items.back();
        items.back()->setNext(items.front());

        // Final item, last is always current
        final = items.front();
        items.front()->setLast(items.back());
    }

    void clear()
    {
        for (CircularItem *item: items) {
            delete item;
        }
        items.clear();
    }

    /// Push pointer to data to buffer. It takes ownership of the data.
    void push(T *data)
    {
        items.push_back(new CircularItem(data));

        // Connect items to circle
        if (items.size() >= 2) {
            CircularItem *&last = items.at(items.size()-1);
            CircularItem *&nextToLast = items.at(items.size()-2);

            nextToLast->setNext(last);
            last->setLast(nextToLast);
        }

        // Current item, next is always final
        current = items.back();
        items.back()->setNext(items.front());

        // Final item, last is always current
        final = items.front();
        items.front()->setLast(items.back());
    }

public:
    /// Wait for updated data.
    void waitForNew()
    {
        std::unique_lock<std::mutex> lck(mutex);
        CircularItem *test = current;

        new_item_ready.wait(lck, [&]{ return test != current; });
    }

    /// Returns holder with current data.
    Holder getCurrent() { return prepareHolder(getCurrentItem()); }

    /// Returns n holders with the most current data, sorted from current to oldest.
    std::vector<Holder> getCurrent(unsigned n) { return prepareHolder(getCurrentNItems(n)); }

    /// Returns nth holder with data.
    Holder getNth(unsigned nth) { return prepareHolder(getNthItem(nth)); }

    /// Returns final (oldest) holder with data.
    Holder getFinal() { return prepareHolder(getFinalItem()); }

    /// Returns n holders with the oldest data, sorted from oldest to current.
    std::vector<Holder> getFinalN(unsigned n) { return prepareHolder(getFinalNItems(n)); }

    /// Returns the holder with new data once ready.
    Holder getNextWait() { return prepareHolder(getNextWaitItem()); }

    //Holder getNext(Holder &holder) { return prepareHolder(getNextItem(holder.getItem())); }
    //Holder getNextWait(Holder &holder) { return prepareHolder(getNextWaitItem(holder.getItem())); }
    //Holder getLast(Holder &holder) { return prepareHolder(getLastItem(holder.getItem())); }

public:
    /// Get the oldest unused item for updating with new data.
    CircularItem *getNewCurrent()
    {
        std::lock_guard<std::mutex> lock(mutex);

        CircularItem *item = nullptr;
        // Try if skipped item are available
        for (CircularItem *i: skipped) {
            if (i->isValid() && !i->isUsed()) {
                skipped.erase(i);
                item = i;
                break;
            }
        }

        // If not, find next available item
        if (item == nullptr) {
            item = current;
            while (true) {
                item = item->getNext();

                if (item == current) {
                    return nullptr;
                }

                if (item->isValid() && !item->isUsed()) {
                    break;
                } else {
                    skipped.insert(item); // Save the skipped ones
                }
            }

            // Update final. If available item is skipped one, do not move final.
            final = item->getNext();
        }

        current->setNext(item);
        item->setLast(current);

        // Set new current item
        current = item;

        // Keep the circle
        current->setNext(final);
        final->setLast(current);

        item->setValid(false);
        return item;
    }

    /// Once the item is updated, set as ready.
    void setNewReady(CircularItem *item)
    {
        std::lock_guard<std::mutex> lock(mutex);
        item->setValid(true);
        new_item_ready.notify_all();
    }

protected:
    CircularItem *getCurrentItem()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return getLastValid(current);
    }

    CircularItem *getNthItem(unsigned nth)
    {
        std::lock_guard<std::mutex> lock(mutex);

        CircularItem *frame = current;
        for (unsigned i = 0; i < nth; ++i) {
            frame = frame->getLast();
        }

        return getLastValid(frame);
    }

    std::vector<CircularItem *> getCurrentNItems(unsigned n)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (n >= items.size()) {
            n = items.size() - 1;
        }

        std::vector<CircularItem *> taken_items;
        CircularItem *item = getLastValid(current);
        if (!item) {
            return taken_items;
        }
        taken_items.push_back(item);

        for (unsigned i = 1; i < n; ++i) {
            CircularItem *item = getLastValid(taken_items.back()->getLast());
            if (!item) {
                return taken_items;
            }
            taken_items.push_back(item);
        }

        return taken_items;
    }

    CircularItem *getFinalItem()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return getNextValid(final);
    }

    std::vector<CircularItem *> getFinalNItems(unsigned n)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (n >= items.size()) {
            n = items.size() - 1;
        }

        std::vector<CircularItem *> taken_items;
        CircularItem *item = getNextValid(final);
        if (!item) {
            return taken_items;
        }
        taken_items.push_back(item);

        for (unsigned i = 1; i < n; ++i) {
            CircularItem *item = getNextValid(taken_items.back()->getNext());
            if (!item) {
                return taken_items;
            }
            taken_items.push_back(item);
        }

        return taken_items;
    }

    CircularItem *getNextItem(CircularItem *item)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return getNextValid(item->getNext());
    }
    CircularItem *getNextWaitItem(CircularItem *item)
    {
        CircularItem *itemNext;

        while (true) {
            itemNext = getNextItem(item);
            if (itemNext) {
                break;
            } else {
                std::unique_lock<std::mutex> lck(mutex);
                new_item_ready.wait(lck);
            }
        }

        return itemNext;
    }

    CircularItem *getNextWaitItem()
    {
        return getNextWaitItem(current);
    }

    std::vector<CircularItem *> getNextWaitNItems(unsigned n)
    {
        if (n >= items.size()) {
            n = items.size() - 1;
        }

        std::vector<CircularItem *> taken_items;
        CircularItem *item = getCurrentItem();
        if (!item) {
            return taken_items;
        }
        taken_items.push_back(item);

        for (unsigned i = 1; i < n; ++i) {
            taken_items.push_back(getNextWaitItem(taken_items.back()));
        }

        return taken_items;
    }

    CircularItem *getLastItem(CircularItem *item)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return getLastValid(item->getLast());
    }

    void holdItem(CircularItem *item)
    {
        //std::cout << __PRETTY_FUNCTION__ << std::endl;
        std::lock_guard<std::mutex> lock(mutex);
        item->hold();
    }

    void releaseItem(CircularItem *item)
    {
        //std::cout << __PRETTY_FUNCTION__ << std::endl;
        std::lock_guard<std::mutex> lock(mutex);
        item->release();
    }

protected:
    CircularItem *getNextValid(CircularItem *item)
    {
        if (current == item) {
            return nullptr;
        }

        CircularItem *next = item;
        while (!next->isValid()) {
            next = next->getNext();
            if (next == item) {
                return nullptr;
            }
        }

        next->hold();
        return next;
    }

    CircularItem *getLastValid(CircularItem *item)
    {
        if (final == item) {
            return nullptr;
        }

        CircularItem *last = item;
        while (!last->isValid()) {
            last = last->getLast();
            if (last == item) {
                return nullptr;
            }
        }

        last->hold();
        return last;
    }

protected:
    Holder prepareHolder(CircularItem *item)
    {
        return Holder(this, item);
    }

    std::vector<Holder> prepareHolder(const std::vector<CircularItem *> &items)
    {
        std::vector<Holder> items_vector;
        items_vector.reserve(items.size());
        for (CircularItem *item: items) {
            items_vector.push_back(Holder(this, item));
        }

        return items_vector;
    }

private:
    std::vector<CircularItem *> items;
    CircularItem *current = nullptr;
    CircularItem *final = nullptr;

    std::mutex mutex;
    std::condition_variable new_item_ready;
    std::set<CircularItem *> skipped;
};

/**
 * Class represents the item in the buffer. It holds the pointer to data, it also deletes the data.
 *
 * It implements the basic functions for the buffer - pointer data, pointer to next and previous items.
 * It also implements the synchronization in form of semaphor. This class is not thread safe.
 */
template<typename T>
class CircularBuffer<T>::CircularItem
{
    friend CircularBuffer;

private:
    CircularItem(T *data): data(data) {}
    ~CircularItem() { delete data; }
    CircularItem(const CircularItem &) = delete;
    CircularItem &operator=(const CircularItem &) = delete;

    T *get() { return data; }

    CircularItem *getNext() const { return next; }
    CircularItem *getLast() const { return last; }
    void setNext(CircularItem *item) { next = item; }
    void setLast(CircularItem *item) { last = item; }

    bool isValid() const { return valid; }
    void setValid(bool valid) { this->valid = valid; }

    void hold() { semaphore++; }
    void release() { if (semaphore > 0) semaphore--; }
    bool isUsed() const { return (semaphore != 0); }

private:
    T *data;

    bool valid = true;
    unsigned semaphore = 0;

    CircularItem *next = nullptr;
    CircularItem *last = nullptr;
};

/**
 * Class for passing the data to the user. It releases the semaphor atomatically.
 *
 * For safety reasons, the object can not be copied.
 */
template<typename T>
class CircularBuffer<T>::Holder
{
    CircularBuffer<T> *buffer;
    typename CircularBuffer<T>::CircularItem *item;

public:
    Holder(CircularBuffer<T> *buffer, typename CircularBuffer<T>::CircularItem *item): buffer(buffer), item(item) {}
    Holder(const Holder &other) {
        buffer = other.buffer;
        item = other.item;
        buffer->holdItem(item);
    }
    Holder &operator=(const Holder &other) {
        if(this == &other) {
            return *this;
        }
        buffer = other.buffer;
        item = other.item;
        buffer->holdItem(item);
        return this;
    }
    Holder(Holder&& other): buffer(other.buffer), item(other.item) {
        other.buffer = nullptr;
    }
    Holder& operator=(Holder&& other) {
        if (&other == this) {
            return *this;
        }

        buffer->releaseItem(item);
        buffer = other.buffer;
        item = other.item;
        other.buffer = nullptr;

        return *this;
    }
    ~Holder() {
        if (buffer) {
            buffer->releaseItem(item);
        }
    }

    const T *get() const { return item->get(); }
    const T *operator*() const { return item->get(); }
    const T *operator->() const { return item->get(); }
};

#endif // CIRCULARBUFFER_H
