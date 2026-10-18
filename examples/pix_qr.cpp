#include <celerpay/pix.hpp>
#include <celerpay/qr.hpp>

#include <cstdio>
#include <fstream>

using namespace std;
using namespace celerpay;

int main(int argc, char** argv) {
    pix::Payload payload;
    payload.key = "fulano@example.com";
    payload.merchantName = "Fulano de Tal";
    payload.merchantCity = "São Paulo";
    payload.amount = Money::fromCents(4990);
    payload.txid = "PEDIDO42";

    const auto code = pix::build(payload);
    if (!code) {
        printf("erro: %s\n", string(code.error().detail).c_str());
        return 1;
    }
    printf("copia e cola: %s\n", code->c_str());

    const auto svg = qr::pixToSvg(payload);
    if (!svg) {
        printf("erro: %s\n", string(svg.error().detail).c_str());
        return 1;
    }
    const char* path = argc > 1 ? argv[1] : "pix.svg";
    ofstream(path, ios::binary) << *svg;
    printf("QR salvo em %s\n", path);
    return 0;
}
