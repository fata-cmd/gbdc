#include <iostream>
#include <memory>
#include <vector>

template <typename T>
class TrackingAllocator {
public:
    using value_type = T;

    TrackingAllocator() = default;

    template <typename U>
    constexpr TrackingAllocator(const TrackingAllocator<U>&) noexcept {}

    T* allocate(std::size_t n) {
        std::size_t bytes = n * sizeof(T);
        totalAllocated += bytes;
        totalOutstanding += bytes;
        std::cout << "Allocating " << bytes << " bytes. Total allocated: " << totalAllocated << " bytes.\n";
        return static_cast<T*>(::operator new(bytes));
    }

    void deallocate(T* p, std::size_t n) noexcept {
        std::size_t bytes = n * sizeof(T);
        totalOutstanding -= bytes;
        std::cout << "Deallocating " << bytes << " bytes. Total outstanding: " << totalOutstanding << " bytes.\n";
        ::operator delete(p);
    }

    static std::size_t getTotalAllocated() {
        return totalAllocated;
    }

    static std::size_t getTotalOutstanding() {
        return totalOutstanding;
    }

private:
    static inline std::size_t totalAllocated = 0;
    static inline std::size_t totalOutstanding = 0;
};

template <typename T, typename U>
bool operator==(const TrackingAllocator<T>&, const TrackingAllocator<U>&) { return true; }

template <typename T, typename U>
bool operator!=(const TrackingAllocator<T>&, const TrackingAllocator<U>&) { return false; }

int main() {
    {
        std::vector<int, TrackingAllocator<int>> vec;
        vec.reserve(10);
        vec.push_back(1);
        vec.push_back(2);
        vec.push_back(3);
    }

    std::cout << "Total allocated: " << TrackingAllocator<int>::getTotalAllocated() << " bytes.\n";
    std::cout << "Total outstanding: " << TrackingAllocator<int>::getTotalOutstanding() << " bytes.\n";

    return 0;
}
