#include <celerpay/statement.hpp>

#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>

using namespace std;
using namespace celerpay;
using namespace celerpay::statement;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const string_view input(reinterpret_cast<const char*>(data), size);
    uint64_t count = 0;
    const Sink sink = [&](const Transaction& t) {
        if (!t.date.ok()) {
            abort();
        }
        ++count;
    };

    const auto memory = parseCnab240(input, cnab240::febraban(), sink);
    const uint64_t memoryCount = count;
    istringstream stream{string(input)};
    count = 0;
    const auto streamed = parseCnab240(stream, cnab240::febraban(), sink);
    if (memory.has_value() != streamed.has_value() || (memory && memoryCount != count)) {
        abort();
    }
    (void)parseOfx(input, sink);
    (void)parsePixJson(input, sink);
    return 0;
}
