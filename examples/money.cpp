#include <celerpay/money.hpp>

#include <cstdio>

using namespace std;
using namespace celerpay;

int main() {
    const auto price = parseAmount("1999.90");
    if (!price) {
        printf("erro: %s\n", string(price.error().detail).c_str());
        return 1;
    }

    const auto total = checkedMul(*price, 3);
    if (!total) {
        printf("erro: %s\n", string(total.error().detail).c_str());
        return 1;
    }
    printf("total: %s\n", formatBrl(*total).c_str());

    const auto bad = parseAmount("1.999,90");
    if (!bad) {
        printf("entrada inválida: %s na posição %llu\n", string(bad.error().detail).c_str(),
               static_cast<unsigned long long>(bad.error().offset));
    }
    return 0;
}
