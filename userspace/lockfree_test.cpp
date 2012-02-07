#include <iostream>
#include <atomic>

int main()
{
    std::atomic_flag f;
    bool res;

    std::atomic<long unsigned int> ui;

    std::cout << "Is ulong lock-free? " 
              << ui.is_lock_free() 
              << std::endl;

    f.clear();
    res = f.test_and_set();
    std::cout << res << std::endl;
    res = f.test_and_set();
    std::cout << res << std::endl;
}
