#ifndef CIRCULARBUFFER_H
#define CIRCULARBUFFER_H

#include <iostream>
#include <vector>
#include <set>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <cassert>

/**
 * Template Class Container used for multithreaded shared data with model One Producer Multiple Consumers.
 */
template<typename T>
class CircularBuffer
{
public:
    static_assert(std::is_default_constructible<T>::value, "CircularBuffer requires default constructible object.");
    using value_type = T;
    class CircularItem;

    /// Create buffer with default values.
    CircularBuffer(std::size_t size);
    /// Create buffer using the given values. Takes ownership of the data, so use with std::move.
    CircularBuffer(const std::vector<T *> &&data);
    /// Not implemented yet - would require copy contructible items.
    CircularBuffer(const CircularBuffer &) = delete;
    /// Not implemented yet - would require copy contructible items.
    CircularBuffer &operator=(const CircularBuffer &) = delete;
    /// Releases all the allocated memory.
    ~CircularBuffer();

public:
    /// Blocks until new data are ready.
    void waitForNew();
    /// Always release the item! Or use the make_holder() function.
    /// Returns item with current data.
    CircularItem *getCurrent();
    /// Returns nth item with data.
    CircularItem *getNth(std::size_t nth);
    /// Returns n items with the most current data, sorted from current to oldest.
    std::vector<CircularItem *> getCurrent(std::size_t n);
    /// Returns final (oldest) item with data.
    CircularItem *getFinal();
    /// Returns n items with the oldest data, sorted from oldest to current.
    std::vector<CircularItem *> getFinal(std::size_t n);
    /// Returns an item with new data once ready.
    CircularItem *getNextWait();
    /// Returns n items with new data once ready.
    std::vector<CircularItem *> getNextWait(std::size_t n);
    /// Returns an item after the item once ready.
    CircularItem *getNextWait(CircularItem *item);

public:
    /// Increases semaphore lock for given item
    void holdItem(CircularItem *item);
    /// Releases semaphore lock for given item
    void releaseItem(CircularItem *item);

public:
    /// Gets item count
    std::size_t size() const { return items.size(); }
    /// Get the oldest unused item for updating with new data.
    CircularItem *getNewCurrent();
    /// Once the item is updated, set as ready.
    void setNewReady(CircularItem *item);
    /// Print the current state, for debugging.
    void print();

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
public:
    friend CircularBuffer;
    T *get() { return data; }

private:
    CircularItem(T *data, std::size_t id): data(data), id(id) {}
    ~CircularItem() { delete data; }
    CircularItem(const CircularItem &) = delete;
    CircularItem &operator=(const CircularItem &) = delete;

    CircularItem *getNext() const { return next; }
    CircularItem *getLast() const { return last; }
    void setNext(CircularItem *item) { next = item; }
    void setLast(CircularItem *item) { last = item; }

    bool isValid() const { return valid; }
    void setValid(bool valid) { this->valid = valid; }

    void hold() { semaphore++; }
    void release() { assert(semaphore != 0); semaphore--; }
    bool isUsed() const { return (semaphore != 0); }

    std::size_t getId() const { return id; }

private:
    T *data;

    const std::size_t id = 0;
    bool valid = true;
    std::size_t semaphore = 0;

    CircularItem *next = nullptr;
    CircularItem *last = nullptr;
};

/**
 * Helper function for creating pointer to CircularBuffer data.
 * The shared pointer automatically releases the data once destroyed.
 */
template<typename T, typename U = T>
std::shared_ptr<U> make_shared_circular(CircularBuffer<T> *buffer, typename CircularBuffer<T>::CircularItem *item, U *data = nullptr)
{
    if (!data) {
        data = item->get();
    }

    std::shared_ptr<U> pointer(data, [buffer, item](U *){
        buffer->releaseItem(item);
    });

    return pointer;
}

/**
 * Helper function for creating vector of pointers to CircularBuffer data.
 * The shared pointer automatically releases the data once destroyed.
 */
template<typename T, typename U = T>
std::vector<std::shared_ptr<U>> make_shared_circular(CircularBuffer<T> *buffer, std::vector<typename CircularBuffer<T>::CircularItem *> items,  std::vector<U *> data = {})
{
    if (data.empty()) {
        data.resize(items.size(), nullptr);
    }

    std::vector<std::shared_ptr<U>> pointers;

    for (std::size_t i = 0; i < items.size(); ++i) {
        pointers.push_back(make_shared_circular<T, U>(buffer, items.at(i), data.at(i)));
    }

    return pointers;
}

//#######################################################################################
//# CircularBuffer function definitions
//#######################################################################################

template<typename T>
CircularBuffer<T>::CircularBuffer(std::size_t size)
{
    for (std::size_t i = 0; i < size; ++i) {
        items.push_back(new CircularItem(new T{}, i));
    }

    // Connect items to circle
    for (std::size_t i = 0; i < items.size() - 1; ++i) {
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

template<typename T>
CircularBuffer<T>::CircularBuffer(const std::vector<T *> &&data)
{
    for (std::size_t i = 0; i < data.size(); ++i) {
        items.push_back(new CircularItem(data.at(i), i));
    }

    // Connect items to circle
    for (std::size_t i = 0; i < items.size() - 1; ++i) {
        items[i]->setNext(items[i + 1]);
        items[i + 1]->setLast(items[i]);
    }

    // Current item, next is always std::size_t
    current = items.back();
    items.back()->setNext(items.front());

    // Final item, last is always current
    final = items.front();
    items.front()->setLast(items.back());
}

template<typename T>
CircularBuffer<T>::~CircularBuffer()
{
    mutex.lock();
    for (CircularItem *item: items) {
        assert(item->isUsed() != true);
        delete item;
    }
    items.clear();
}

template<typename T>
void CircularBuffer<T>::waitForNew()
{
    std::unique_lock<std::mutex> lck(mutex);
    CircularItem *test = current;

    new_item_ready.wait(lck, [&]{ return test != current; });
}

template<typename T>
typename CircularBuffer<T>::CircularItem *CircularBuffer<T>::getNewCurrent()
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

    //current->setNext(item);
    item->setLast(current);

    // Set new current item
    //current = item;

    // Keep the circle
    current->setNext(final);
    final->setLast(current);

    item->setValid(false);
    return item;
}

template<typename T>
void CircularBuffer<T>::setNewReady(CircularItem *item)
{
    std::lock_guard<std::mutex> lock(mutex);

    current->setNext(item);
    current = item;

    // Keep the circle
    current->setNext(final);
    final->setLast(current);

    item->setValid(true);

    new_item_ready.notify_all();
}

template<typename T>
void CircularBuffer<T>::print()
{
    std::lock_guard<std::mutex> lock(mutex);
    std::cout << "CircularBuffer:" << std::endl;
    int counter = 0;
    CircularItem *item = current;
    do {
        item = item->getNext();
        if (item == current) {
            std::cout << "{" << item->getId() << "}" << std::endl;
        } else if (item == final) {
        std::cout << "[" << item->getId() << "]" << " -> ";
        } else {
            std::cout << item->getId() << " -> ";
        }
        counter++;
    } while (item != current);

    std::cout << "- Total frames = " << counter << std::endl;
}

template<typename T>
typename CircularBuffer<T>::CircularItem *CircularBuffer<T>::getCurrent()
{
    std::lock_guard<std::mutex> lock(mutex);
    current->hold();
    return current;
}

template<typename T>
typename CircularBuffer<T>::CircularItem *CircularBuffer<T>::getNth(std::size_t nth)
{
    std::lock_guard<std::mutex> lock(mutex);

    CircularItem *item = current;
    for (std::size_t i = 0; i < nth; ++i) {
        item = item->getLast();

        if (item == current) {
            return nullptr;
        }
    }
    item->hold();
    return item;
}

template<typename T>
std::vector<typename CircularBuffer<T>::CircularItem *> CircularBuffer<T>::getCurrent(std::size_t n)
{
    std::lock_guard<std::mutex> lock(mutex);

    std::vector<CircularItem *> taken_items;
    current->hold();
    taken_items.push_back(current);

    for (std::size_t i = 1; i < n; ++i) {
        CircularItem *item = taken_items.back()->getLast();

        if (item == current) {
            return taken_items;
        }

        item->hold();
        taken_items.push_back(item);
    }

    return taken_items;
}

template<typename T>
typename CircularBuffer<T>::CircularItem *CircularBuffer<T>::getFinal()
{
    std::lock_guard<std::mutex> lock(mutex);
    final->hold();
    return final;
}

template<typename T>
std::vector<typename CircularBuffer<T>::CircularItem *> CircularBuffer<T>::getFinal(std::size_t n)
{
    std::lock_guard<std::mutex> lock(mutex);

    std::vector<CircularItem *> taken_items;
    final->hold();
    taken_items.push_back(final);

    for (std::size_t i = 1; i < n; ++i) {
        CircularItem *item = taken_items.back()->getNext();

        if (item == final) {
            return taken_items;
        }

        item->hold();
        taken_items.push_back(item);
    }

    return taken_items;
}

template<typename T>
typename CircularBuffer<T>::CircularItem *CircularBuffer<T>::getNextWait(CircularItem *item)
{
    std::unique_lock<std::mutex> lck(mutex);

    CircularItem *itemNext;
    itemNext = item->getNext();

    if (itemNext != final) {
        itemNext->hold();
        return itemNext;
    }

    new_item_ready.wait(lck, [&]()
    {
        itemNext = item->getNext();
        return itemNext != final;
    });

    itemNext->hold();
    return itemNext;
}

template<typename T>
typename CircularBuffer<T>::CircularItem *CircularBuffer<T>::getNextWait()
{
    return getNextWait(current);
}

template<typename T>
std::vector<typename CircularBuffer<T>::CircularItem *> CircularBuffer<T>::getNextWait(std::size_t n)
{
    if (n > items.size()) {
        n = items.size();
    }
    std::vector<CircularItem *> taken_items;
    mutex.lock();
    CircularItem *item = current;
    mutex.unlock();

    for (std::size_t i = 0; i < n; ++i) {
        item = getNextWait(item);

        if (!item) {
            return taken_items;
        }

        taken_items.push_back(item);
    }

    return taken_items;
}

template<typename T>
void CircularBuffer<T>::holdItem(CircularItem *item)
{
    //std::cout << __PRETTY_FUNCTION__ << std::endl;
    std::lock_guard<std::mutex> lock(mutex);
    item->hold();
}

template<typename T>
void CircularBuffer<T>::releaseItem(CircularItem *item)
{
    //std::cout << __PRETTY_FUNCTION__ << std::endl;
    std::lock_guard<std::mutex> lock(mutex);
    item->release();
}

#endif // CIRCULARBUFFER_H
