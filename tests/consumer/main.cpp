#include <celerpay/celerpay.hpp>

#include <cstdio>

using namespace std;

int main() {
    const auto amount = celerpay::parseAmount("1234.56");
    if (!amount || celerpay::formatBrl(*amount) != "R$ 1.234,56") {
        return 1;
    }
    puts(celerpay::formatBrl(*amount).c_str());
    return 0;
}
