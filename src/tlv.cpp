#include <celerpay/tlv.hpp>

using namespace std;

namespace celerpay {

using detail::fail;

Result<void> appendTlv(string& out, string_view id, string_view value) {
    if (id.size() != 2) {
        return fail(ErrorCode::InvalidLength, 0, "id TLV deve ter 2 dígitos");
    }
    for (size_t i = 0; i < id.size(); ++i) {
        if (!detail::isDigit(id[i])) {
            return fail(ErrorCode::InvalidCharacter, i, "id TLV deve ter 2 dígitos");
        }
    }
    if (value.size() > 99) {
        return fail(ErrorCode::ValueTooLong, 99, "valor TLV acima de 99 bytes");
    }
    out.append(id);
    out.push_back(static_cast<char>('0' + value.size() / 10));
    out.push_back(static_cast<char>('0' + value.size() % 10));
    out.append(value);
    return {};
}

}
