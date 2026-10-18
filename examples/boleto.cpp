#include <celerpay/boleto.hpp>

#include <chrono>
#include <cstdio>

using namespace std;
using namespace std::chrono;
using namespace celerpay;

int main() {
    const auto barcode = boleto::parse("00190.50095 40144.816069 06809.350314 3 37370000000100");
    if (!barcode) {
        printf("erro: %s\n", string(barcode.error().detail).c_str());
        return 1;
    }
    const auto fields = boleto::bankFields(*barcode);
    printf("código de barras: %s\n", string(barcode->digits()).c_str());
    printf("banco: %s\n", string(fields.bank).c_str());
    printf("valor: %s\n", fields.amount ? formatBrl(*fields.amount).c_str() : "livre");
    if (const auto due = boleto::dueDate(fields.dueFactor, 2008y / January / 1d)) {
        printf("vencimento: %02u/%02u/%d\n", static_cast<unsigned>(due->day()), static_cast<unsigned>(due->month()),
               static_cast<int>(due->year()));
    }
    printf("fator de 22/02/2025: %u\n", static_cast<unsigned>(*boleto::dueFactor(2025y / February / 22d)));
    return 0;
}
