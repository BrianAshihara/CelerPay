#include <celerpay/boleto.hpp>

#include <cstdint>
#include <cstdlib>

using namespace std;
using namespace celerpay;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const string_view input(reinterpret_cast<const char*>(data), size);
    const auto barcode = boleto::parse(input);
    if (!barcode) {
        if (barcode.error().offset > input.size()) {
            abort();
        }
        return 0;
    }
    if (boleto::parseLine(boleto::formatLine(*barcode)) != barcode || boleto::parseBarcode(barcode->digits()) != barcode) {
        abort();
    }
    if (barcode->kind() == boleto::Kind::Bank && boleto::makeBankBarcode(boleto::bankFields(*barcode)) != barcode) {
        abort();
    }
    if (barcode->kind() == boleto::Kind::Collection &&
        boleto::makeCollectionBarcode(boleto::collectionFields(*barcode)) != barcode) {
        abort();
    }
    return 0;
}
