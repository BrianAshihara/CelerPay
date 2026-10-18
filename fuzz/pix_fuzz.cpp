#include <celerpay/pix.hpp>

#include <cstdint>
#include <cstdlib>
#include <string>

using namespace std;
using namespace celerpay;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const string_view input(reinterpret_cast<const char*>(data), size);

    (void)pix::validateKey(input);
    (void)pix::removeAccents(input);

    const auto parsed = pix::parse(input);
    if (!parsed) {
        if (parsed.error().offset > input.size()) {
            abort();
        }
        return 0;
    }
    const auto built = pix::build(*parsed);
    if (!built) {
        abort();
    }
    const auto reparsed = pix::parse(*built);
    if (!reparsed || !(*reparsed == *parsed)) {
        abort();
    }
    return 0;
}
