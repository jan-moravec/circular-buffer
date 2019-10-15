# Circular Buffer

The goal is to create a Template Class Container for multithreaded shared data with model One Producer Multiple Consumers.

I need to work with frames from the camera in an embedded device, where performance is the main issue. The data are created in one thread and must be accessible from multiple threads with minimum copying. 
So I created these classes for representing the camera data and for accessing the data itself safely.

Assignment:
- Have N frames or measurements (or any piece of data).
- Access the data from multiple threads.
- Generate the data in one thread.
- The data itself must be shared memory due to performance.

## Getting Started

The project has main.cpp and a curcularbuffer.h container header. The main.cpp contains unit testing.

I used CMake for compilation, but it can be compiled with command:
```
g++ main.cpp -pthread -I./
```

### Prerequisites

I am using C++17, so newer toolchain is needed.

## Internals Description

The items of buffer are stored as pointer in a vector. There is a pointer to the current frame and final frame. 
- Current frame: next is always Final.
- Final frame: last is always Current.

The frames thus are represented as a circular buffer. Each frame itself also has a pointer to next and precious. Each frame can be taken (semaphore is not a zero), or invalid (it is being updated).

The functions:
```
CircularItem *getNewCurrent();
void setNewReady(CircularItem *item);
```
are used for getting an unused and valid frame, that will be updated (populate with new data), and then set as ready.

Since none or all frames can be taken or invalid, the user must check if the frame is not a nullptr.

If some frames are taken, they will not be updated until they are available. That means that they will be skipped in the getNewCurrent() function. 

## Recommendation

I tried to test all the function to guarantee it to be thread-safe. But for the best performance, you should always create enough frames in the buffer. If you know your application will take 32 frames at a time, make the buffer like 64 frames wide. 

Once all frames are taken, it can not save any new data.

## Author

* **Jan Moravec**


## License

This project is licensed under the MIT License - see the [LICENSE.md](LICENSE.md) file for details
