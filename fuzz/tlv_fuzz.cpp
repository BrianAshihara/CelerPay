#include <celerpay/celerpay.hpp>

#include <cstdint>
#include <cstdlib>
#include <string>

using namespace std;
using namespace celerpay;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const string_view input(reinterpret_cast<const char*>(data), size);

    string rebuilt;
    size_t offset = 0;
    while (offset < input.size()) {
        const auto field = readTlv(input, offset);
        if (!field) {
            if (field.error().offset > input.size()) {
                abort();
            }
            break;
        }
        if (field->end <= offset || field->end > input.size()) {
            abort();
        }
        if (!appendTlv(rebuilt, field->id, field->value)) {
            abort();
        }
        offset = field->end;
    }
    if (rebuilt != input.substr(0, offset)) {
        abort();
    }

    if (const auto cpf = parseCpf(input); cpf && parseCpf(formatCpf(*cpf)) != cpf) {
        abort();
    }
    if (const auto cnpj = parseCnpj(input); cnpj && parseCnpj(formatCnpj(*cnpj)) != cnpj) {
        abort();
    }
    if (const auto amount = parseAmount(input); amount && amount->cents() < 0) {
        abort();
    }
    (void)crc16Ccitt(input);
    return 0;
}
