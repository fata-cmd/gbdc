#pragma once
#include <queue>
#include <mutex>

template <typename T>
class MPSCQueue
{
private:
    std::queue<T> q;
    std::mutex m;

public:
    void push(T &&t)
    {
        m.lock();
        q.push(t);
        m.unlock();
    }

    T pop()
    {
        T t = q.front();
        q.pop();
        return t;
    }

    bool empty()
    {
        return q.empty();
    }
};
