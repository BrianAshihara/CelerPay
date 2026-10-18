#include <celerpay/document.hpp>

#include <cstdio>

using namespace std;
using namespace celerpay;

int main() {
    if (const auto cpf = parseCpf("529.982.247-25")) {
        printf("CPF válido: %s\n", formatCpf(*cpf).c_str());
    }
    if (const auto cnpj = parseCnpj("12abc34501de35")) {
        printf("CNPJ alfanumérico: %s\n", formatCnpj(*cnpj).c_str());
    }
    if (const auto generated = makeCnpj("AB12CD34EF56")) {
        printf("CNPJ gerado: %s\n", formatCnpj(*generated).c_str());
    }
    const auto bad = parseCpf("529.982.247-26");
    if (!bad) {
        printf("CPF inválido: %s na posição %llu\n", string(bad.error().detail).c_str(),
               static_cast<unsigned long long>(bad.error().offset));
    }
    return 0;
}
