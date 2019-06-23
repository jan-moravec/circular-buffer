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
    class CircularItem;
    class Holder;

    CircularBuffer() = default;
    virtual ~CircularBuffer()
    {
        for (CircularItem *item: items) {
            delete item;
        }
    }
    CircularBuffer(const CircularBuffer &) = delete;
    CircularBuffer &operator=(const CircularBuffer &) = delete;

    void waitForNew()
    {
        std::unique_lock<std::mutex> lck(mutex);
        CircularItem *test = current;

        new_frame_ready.wait(lck, [&]{ return test != current; });
    }

    Holder prepareHolder(CircularItem *item)
    {
        return Holder(item);
    }

    std::vector<Holder> prepareHolderVector(const std::vector<CircularItem *> &items)
    {
        std::vector<Holder> items_vector;
        for (CircularItem *item: items) {
            items_vector.push_back(Holder(item));
        }

        return items_vector;
    }

    Holder getCurrent() { return prepareHolder(getCurrentItem()); }
    Holder getNth(unsigned nth) { return prepareHolder(getNthItem(nth)); }
    std::vector<Holder> getCurrentN(unsigned n) { return prepareHolderVector(getCurrentNItems(n)); }
    Holder getFinal() { return prepareHolder(getFinalItem()); }
    std::vector<Holder> getFinalN(unsigned n) { return prepareHolderVector(getFinalNItems(n)); }
    Holder getNext(Holder &holder) { return prepareHolder(getNextItem(holder.getItem())); }
    Holder getNextWait(Holder &holder) { return prepareHolder(getNextWaitItem(holder.getItem())); }
    Holder getNextWait() { return prepareHolder(getNextWaitItem()); }
    Holder getLast(Holder &holder) { return prepareHolder(getLastItem(holder.getItem())); }

protected:
    /// Functions for accessing the frames
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

        std::vector<CircularItem *> taken_frames;
        CircularItem *frame = getLastValid(current);
        if (!frame) {
            return taken_frames;
        }
        taken_frames.push_back(frame);

        for (unsigned i = 1; i < n; ++i) {
            CircularItem *frame = getLastValid(taken_frames.back()->getLast());
            if (!frame) {
                return taken_frames;
            }
            taken_frames.push_back(frame);
        }

        return taken_frames;
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

        std::vector<CircularItem *> taken_frames;
        CircularItem *frame = getNextValid(final);
        if (!frame) {
            return taken_frames;
        }
        taken_frames.push_back(frame);

        for (unsigned i = 1; i < n; ++i) {
            CircularItem *frame = getNextValid(taken_frames.back()->getNext());
            if (!frame) {
                return taken_frames;
            }
            taken_frames.push_back(frame);
        }

        return taken_frames;
    }
    CircularItem *getNextItem(CircularItem *frame)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return getNextValid(frame->getNext());
    }
    CircularItem *getNextWaitItem(CircularItem *frame)
    {
        CircularItem *frameNext;

        while (true) {
            frameNext = getNextItem(frame);
            if (frameNext) {
                break;
            } else {
                std::unique_lock<std::mutex> lck(mutex);
                new_frame_ready.wait(lck);
            }
        }

        return frameNext;
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

        std::vector<CircularItem *> taken_frames;
        CircularItem *frame = getCurrentItem();
        if (!frame) {
            return taken_frames;
        }
        taken_frames.push_back(frame);

        for (unsigned i = 1; i < n; ++i) {
            taken_frames.push_back(getNextWaitItem(taken_frames.back()));
        }

        return taken_frames;
    }
    CircularItem *getLastItem(CircularItem *frame)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return getLastValid(frame->getLast());
    }

    /// Do not forget to release taken frames
    void releaseItem(CircularItem *frame)
    {
        std::lock_guard<std::mutex> lock(mutex);
        frame->release();
    }

protected:
    /// Get frame for updating with new data
    CircularItem *getNewCurrent()
    {
        std::lock_guard<std::mutex> lock(mutex);

        CircularItem *frame = nullptr;
        // Try if skipped frames are available
        for (CircularItem *f: skipped) {
            if (f->isValid() && !f->isUsed()) {
                skipped.erase(f);
                frame = f;
                break;
            }
        }

        // If not, find next available frame
        if (frame == nullptr) {
            frame = current;
            while (true) {
                frame = frame->getNext();

                if (frame == current) {
                    return nullptr;
                }

                if (frame->isValid() && !frame->isUsed()) {
                    break;
                } else {
                    skipped.insert(frame); // Save the skipped ones
                }
            }

            // Update final. If available frame is skipped one, do not move final.
            final = frame->getNext();
        }

        current->setNext(frame);
        frame->setLast(current);

        // Set new current frame
        current = frame;

        // Keep the circle
        current->setNext(final);
        final->setLast(current);

        frame->updateTimestamp();
        frame->setValid(false);
        return frame;
    }
    /// Once updated, set as ready
    void setNewReady(CircularItem *frame)
    {
        std::lock_guard<std::mutex> lock(mutex);
        frame->setValid(true);
        newFrameReady();
    }
    void newFrameReady()
    {
        new_frame_ready.notify_all();
    }

    CircularItem *getNextValid(CircularItem *frame)
    {
        if (current == frame) {
            return nullptr;
        }

        CircularItem *next = frame;
        while (!next->isValid()) {
            next = next->getNext();
            if (next == frame) {
                return nullptr;
            }
        }

        next->hold();
        return next;
    }
    CircularItem *getLastValid(CircularItem *frame)
    {
        if (final == frame) {
            return nullptr;
        }

        CircularItem *last = frame;
        while (!last->isValid()) {
            last = last->getLast();
            if (last == frame) {
                return nullptr;
            }
        }

        last->hold();
        return last;
    }

protected:
    void push(T *data)
    {
        items.push_back(new CircularItem(this, data));
    }
    void connect()
    {
        // Connect items, except current and final
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

private:
    std::vector<CircularItem *> items;
    CircularItem *current = nullptr;
    CircularItem *final = nullptr;

    std::mutex mutex;
    std::condition_variable new_frame_ready;
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
    CircularItem(CircularBuffer<T> *buffer, T *data): buffer(buffer), data(data) {}
    ~CircularItem() { delete data; }
    CircularItem(const CircularItem &) = delete;
    CircularItem &operator=(const CircularItem &) = delete;

    T *get() { return data; }
    CircularBuffer<T> *getBuffer() {return buffer; }

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
    CircularBuffer<T> *buffer;
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
    using Item = typename CircularBuffer<T>::CircularItem;
    using Releaser = std::function<void(Item *)>;

    const Releaser releaser = [](Item *item) { item->getBuffer()->releaseItem(item); };
    std::unique_ptr<Item, Releaser> itemPtr;

public:
    Holder(typename CircularBuffer<T>::CircularItem *item): itemPtr(item, releaser) { }

    const T *get() const { return itemPtr->get(); }
    const T *operator*() const { return itemPtr->get(); }
    const T *operator->() const { return itemPtr->get(); }
    Item *getItem() const { return itemPtr.get(); }
};

#endif // CIRCULARBUFFER_H
