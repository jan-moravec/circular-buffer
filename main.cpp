#include <iostream>

#include "library/circularbuffer.h"

class TestBuffer: public CircularBuffer<std::vector<int>>
{
public:
    TestBuffer()
    {
        for (int i = 0; i < 16; ++i) {
            std::vector<int> *pi = new std::vector<int>;
            *pi = {i};
            push(pi);
        }
        //connect();
    }
};

int main()
{
    CircularBuffer<int> test;
    TestBuffer buffer;

    TestBuffer::Holder current = buffer.getCurrent();
    std::cout << current->at(0) << std::endl;

    {
        TestBuffer::Holder nth = buffer.getNth(5);
        std::cout << nth->at(0) << std::endl;
    }
    {
        TestBuffer::Holder final = buffer.getFinal();
        std::cout << final->at(0) << std::endl;
    }



    {
        std::vector<TestBuffer::Holder> vector = buffer.getCurrent(5);
        std::cout << vector.at(4)->at(0) << std::endl;
    }
    return 0;
}
