#include <functional>
int main(){ std::copyable_function<int(int)> f; (void)f; return 0; }
