#include <iostream>
#include <cassert>
#include <chrono>
#include <memory>
#include <atomic>
#include <thread>
#include <random>

#include "circularbuffer.h"

// Should be at least 16
static const std::size_t buffer_size = 32;

struct Data {
    std::unique_ptr<int> data;
    std::chrono::high_resolution_clock::time_point timepoint;
};

using BufferInt = CircularBuffer<int>;
using BufferVectorInt = CircularBuffer<std::vector<int>>;
using BufferData = CircularBuffer<Data>;

void checkInitialization();
void checkBasic();
void checkUpdating();
void checkUpdatingAndLock();
void checkLockAndExceptions();
void checkDataLosses();
void checkThreadSynchronization();
void checkMultithreaded();

int main()
{
    checkInitialization();
    checkBasic();
    checkUpdating();
    checkUpdatingAndLock();
    checkLockAndExceptions();
    checkDataLosses();
    checkThreadSynchronization();
    checkMultithreaded();

    std::cout << "All Ok" << std::endl;
    return 0;
}

void checkInitialization()
{
    std::cout << "Checking basic initialization" << std::endl;

    BufferInt buffer_int(buffer_size);
    BufferVectorInt buffer_vector(buffer_size);

    std::vector<Data *> vector_data;
    for (std::size_t i = 0; i < buffer_size; ++i) {
        Data *data = new Data{};
        data->data = std::make_unique<int>(i);
        vector_data.push_back(data);
    }

    BufferData buffer_data(std::move(vector_data));
}

void checkBasic()
{
    std::cout << "Checking basic functions" << std::endl;

    std::vector<Data *> vector_data;
    for (std::size_t i = 0; i < buffer_size; ++i) {
        Data *data = new Data{};
        data->data = std::make_unique<int>(i);
        vector_data.push_back(data);
    }

    BufferData buffer_data(std::move(vector_data));

    {
        std::shared_ptr<Data> holder = make_shared_circular<Data>(&buffer_data, buffer_data.getCurrent());
        assert(holder);
        assert(*holder->data == buffer_size - 1);
    }

    {
        std::shared_ptr<Data> holder1 = make_shared_circular<Data>(&buffer_data, buffer_data.getFinal());
        assert(holder1);
        assert(*holder1->data == 0);

        // Copy constructor
        std::shared_ptr<Data> holder2(holder1);
        assert(holder2);
        assert(*holder2->data == *holder1->data);

        // Destructor assert happens here!
    }

    for (std::size_t i = 0; i < buffer_size; ++i) {
        std::shared_ptr<Data> holder = make_shared_circular<Data>(&buffer_data, buffer_data.getNth(i));
        assert(holder);
        assert(*holder->data == buffer_size - i - 1);
    }
}

void checkUpdating()
{
    std::cout << "Checking data placing." << std::endl;

    BufferInt buffer_int(buffer_size);

    // Initialize the whole buffer
    for (std::size_t i = 0; i < buffer_size; ++i) {
        BufferInt::CircularItem *item = buffer_int.getNewCurrent();
        *item->data() = i;
        buffer_int.setNewReady(item);

        std::shared_ptr<int> holder = make_shared_circular<int>(&buffer_int, buffer_int.getCurrent());
        assert(*holder.get() == i);
    }

    // Check the buffer
    for (std::size_t i = 0; i < buffer_size; ++i) {
        std::shared_ptr<int> holder = make_shared_circular<int>(&buffer_int, buffer_int.getNth(i));
        assert(*holder.get() == buffer_size - i - 1);
    }

    // Continue with placing new data and replacing the old
    for (std::size_t i = buffer_size; i < buffer_size * 8; ++i) {
        BufferInt::CircularItem *item = buffer_int.getNewCurrent();
        *item->data() = i;
        buffer_int.setNewReady(item);

        std::shared_ptr<int> holder = make_shared_circular<int>(&buffer_int, buffer_int.getCurrent());
        assert(*holder.get() == i);

        holder = make_shared_circular<int>(&buffer_int, buffer_int.getFinal());
        assert(*holder.get() == i + 1 - buffer_size);
    }
}

void checkUpdatingAndLock()
{
    std::cout << "Checking data placing and lock." << std::endl;

    BufferInt buffer_int(buffer_size);

    // Initialize the whole buffer
    for (std::size_t i = 0; i < buffer_size; ++i) {
        BufferInt::CircularItem *item = buffer_int.getNewCurrent();
        *item->data() = i;
        buffer_int.setNewReady(item);
    }

    std::vector<std::shared_ptr<int>> holders = make_shared_circular<int>(&buffer_int, buffer_int.getCurrent(buffer_size / 2));
    assert(holders.size() == buffer_size / 2);

    std::vector<int> values;
    for (std::shared_ptr<int> &holder : holders) {
        values.push_back(*holder.get());
    }

    for (std::size_t i = 0; i < buffer_size / 2; ++i) {
        BufferInt::CircularItem *item = buffer_int.getNewCurrent();
        *item->data() = -static_cast<int>(i);
        buffer_int.setNewReady(item);
    }

    std::size_t counter = 0;
    for (std::shared_ptr<int> &holder : holders) {
        assert(*holder.get() == values.at(counter++));
    }

    holders.clear();

    for (std::size_t i = 0; i < buffer_size / 2; ++i) {
        std::shared_ptr<int> holder = make_shared_circular<int>(&buffer_int, buffer_int.getNth(i));
        assert(*holder.get() == -(static_cast<int>(buffer_size) / 2) + 1 + static_cast<int>(i));
    }

    for (std::size_t i = buffer_size/2; i < buffer_size; ++i) {
        std::shared_ptr<int> holder = make_shared_circular<int>(&buffer_int, buffer_int.getNth(i));
        assert(*holder.get() == buffer_size - i - 1 + buffer_size/2);
    }

    for (std::size_t i = 0; i < buffer_size; ++i) {
        BufferInt::CircularItem *item = buffer_int.getNewCurrent();
        *item->data() = i;
        buffer_int.setNewReady(item);

        std::shared_ptr<int> holder = make_shared_circular<int>(&buffer_int, buffer_int.getCurrent());
        assert(*holder.get() == i);
    }

    for (std::size_t i = 0; i < buffer_size; ++i) {
        std::shared_ptr<int> holder = make_shared_circular<int>(&buffer_int, buffer_int.getNth(i));;
        assert(*holder.get() == buffer_size - i - 1);
    }
}

void checkDataLosses()
{
    std::cout << "Checking for data lossess." << std::endl;

    BufferInt buffer_int(buffer_size);

    // Initialize the whole buffer
    for (std::size_t i = 0; i < buffer_size; ++i) {
        BufferInt::CircularItem *item = buffer_int.getNewCurrent();
        *item->data() = i;
        buffer_int.setNewReady(item);
    }

    std::vector<std::shared_ptr<int>> holders = make_shared_circular<int>(&buffer_int, buffer_int.getFinal(buffer_size));
    assert(holders.size() == buffer_size);
    holders.clear();

    holders = make_shared_circular<int>(&buffer_int, buffer_int.getFinal(buffer_size / 2));
    for (std::size_t i = 0; i < buffer_size / 2; ++i) {
        BufferInt::CircularItem *item = buffer_int.getNewCurrent();
        *item->data() = -static_cast<int>(i);
        buffer_int.setNewReady(item);
    }
    holders.clear();

    holders = make_shared_circular<int>(&buffer_int, buffer_int.getFinal(buffer_size));
    assert(holders.size() == buffer_size / 2);
}

void checkLockAndExceptions()
{
    std::cout << "Checking lock and exceptions for inproper use." << std::endl;

    BufferInt buffer_int(buffer_size);

    // Initialize the whole buffer
    for (std::size_t i = 0; i < buffer_size; ++i) {
        BufferInt::CircularItem *item = buffer_int.getNewCurrent();
        *item->data() = i;
        buffer_int.setNewReady(item);
    }

    std::vector<std::shared_ptr<int>> holders = make_shared_circular<int>(&buffer_int, buffer_int.getCurrent(buffer_size));
    assert(holders.size() == buffer_size);

    // Cannot get more that buffer_size
    holders = make_shared_circular<int>(&buffer_int, buffer_int.getCurrent(buffer_size * 100));
    assert(holders.size() == buffer_size);

    // Cannot update anything
    BufferInt::CircularItem *item = buffer_int.getNewCurrent();
    assert(item == nullptr);

    holders.pop_back();

    // Now it should work
    item = buffer_int.getNewCurrent();
    assert(item != nullptr);
    *item->data() = 123;
    buffer_int.setNewReady(item);

    std::shared_ptr<int> holder = make_shared_circular<int>(&buffer_int, buffer_int.getCurrent());
    assert(*holder.get() == 123);
}

void checkThreadSynchronization()
{
    std::cout << "Checking thread synchronization functions." << std::endl;

    BufferInt buffer_int(buffer_size);
    const int counter = 6;

    // Consument
    std::thread thread_consume([&](){
        for (std::size_t i = counter /2; i > 0; --i) {
            std::shared_ptr<int> holder = make_shared_circular<int>(&buffer_int, buffer_int.getNextWait());
            std::cout << "Consumed: " << *holder.get() << std::endl;
        }

        std::vector<std::shared_ptr<int>> holders = make_shared_circular<int>(&buffer_int, buffer_int.getNextWait(counter / 2));
        std::cout << "Consumed: " << holders.size() << std::endl;
        for (std::shared_ptr<int> &holder: holders) {
            std::cout << " - " << *holder.get() << std::endl;
        }
    });

    // Producent
    std::thread thread_generate([&](){
        for (std::size_t i = counter; i > 0; --i) {
            BufferInt::CircularItem *item = buffer_int.getNewCurrent();
            *item->data() = i;
            std::cout << "Generated: " << i << std::endl;
            buffer_int.setNewReady(item);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });

    thread_consume.join();
    thread_generate.join();
}

void assert_holders(const std::vector<std::shared_ptr<Data>> &holders)
{
    for (std::size_t i = 1; i < holders.size(); ++i) {
        assert(*holders.at(i-1).get()->data < *holders.at(i).get()->data);
        assert(holders.at(i-1).get()->timepoint < holders.at(i).get()->timepoint);
    }
}

void checkMultithreaded()
{
    std::cout << "Checking multithreaded usage." << std::endl;

    std::atomic_bool run_generate = true;
    std::atomic_bool run_consume = true;

    std::vector<Data *> vector_data;
    for (std::size_t i = 0; i < buffer_size; ++i) {
        Data *data = new Data{};
        data->data = std::make_unique<int>(i);
        data->timepoint = std::chrono::high_resolution_clock::now();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        vector_data.push_back(data);
    }

    BufferData buffer_data(std::move(vector_data));

    // Generate data
    std::thread thread_generate([&](){
        int counter = buffer_size;
        while (run_generate) {
            BufferData::CircularItem *item = buffer_data.getNewCurrent();
            *item->data()->data = counter++;
            item->data()->timepoint = std::chrono::high_resolution_clock::now();
            buffer_data.setNewReady(item);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });

    // Take data from front and check
    std::thread t_take_current([&](){
        std::vector<std::shared_ptr<Data>> holders;
        while (run_consume) {
            buffer_data.waitForNew();
            std::vector<std::shared_ptr<Data>> holders_new = make_shared_circular<Data>(&buffer_data, buffer_data.getCurrent(buffer_size / 16));
            std::reverse(holders_new.begin(), holders_new.end());
            assert_holders(holders_new);
            holders = holders_new;
        }
    });

    // Take data from back and check
    std::thread t_take_final([&](){
        std::vector<std::shared_ptr<Data>> holders;
        while (run_consume) {
            buffer_data.waitForNew();
            std::vector<std::shared_ptr<Data>> holders_new = make_shared_circular<Data>(&buffer_data, buffer_data.getFinal(buffer_size / 16));
            assert_holders(holders_new);
            holders = holders_new;
        }
    });

    std::random_device rd;
    std::mt19937 mt(rd());
    std::uniform_int_distribution<unsigned> dis(0, 100);
    std::vector<std::thread> take_new_random;
    for (unsigned i = 0; i < buffer_size / 8; ++i) {
        std::thread t([&](){
            while (run_consume) {
                std::vector<std::shared_ptr<Data>> holders = make_shared_circular<Data>(&buffer_data, buffer_data.getCurrent(buffer_size / 16));
                std::this_thread::sleep_for(std::chrono::milliseconds(dis(mt)));
                std::reverse(holders.begin(), holders.end());
                assert_holders(holders);
            }
        });

        take_new_random.push_back(std::move(t));
    }

    std::this_thread::sleep_for(std::chrono::seconds(5));

    run_consume = false;
    t_take_final.join();
    t_take_current.join();
    for (std::thread &t: take_new_random) {
        t.join();
    }

    std::this_thread::sleep_for(std::chrono::seconds(1)); // Refill the buffer
    run_generate = false;
    thread_generate.join();

    // Check the final state
    std::vector<std::shared_ptr<Data>> holders = make_shared_circular<Data>(&buffer_data, buffer_data.getCurrent(buffer_size));
    assert(holders.size() == buffer_size);
    std::reverse(holders.begin(), holders.end());
    assert_holders(holders);

    // Print the final state if interested
    buffer_data.print();
    for (const std::shared_ptr<Data> &holder : holders) {
        std::cout << *holder.get()->data << " "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(holder.get()->timepoint - holders.front().get()->timepoint).count()
                  << std::endl;
    }
}
