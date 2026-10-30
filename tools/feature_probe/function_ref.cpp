#include <functional>
int main(){ std::function_ref<int(int)> f; (void)f; return 0; }
