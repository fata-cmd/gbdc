#pragma once
#include <queue>
#include <mutex>
#include <list>

template <typename T>
class MPSCQueue
{
private:
    std::queue<T> q;
    std::uint16_t producers;
    std::mutex m;

public:
    explicit MPSCQueue(std::uint16_t _producers) : q(), producers(_producers), m() {}
    void push(T &&t)
    {
        std::unique_lock<std::mutex> l(m);
        q.push(t);
    }

    T pop()
    {
        std::unique_lock<std::mutex> l(m);
        T t = q.front();
        q.pop();
        return t;
    }

    bool empty()
    {
        return q.empty();
    }

    bool done(){
        return producers == 0;
    }
};
