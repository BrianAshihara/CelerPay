#include "statement/common.hpp"

#include <array>

using namespace std;
using namespace std::chrono;

namespace celerpay::statement {

namespace {

using namespace detail;

constexpr string_view kOpen = "<STMTTRN>";
constexpr string_view kClose = "</STMTTRN>";

struct Fields {
    string_view type;
    string_view posted;
    string_view amount;
    string_view fitid;
    string_view name;
    string_view memo;
    string_view refnum;
    string_view checknum;
};

void assign(Fields& f, string_view tag, string_view value) noexcept {
    if (tag == "TRNTYPE") {
        f.type = value;
    } else if (tag == "DTPOSTED") {
        f.posted = value;
    } else if (tag == "TRNAMT") {
        f.amount = value;
    } else if (tag == "FITID") {
        f.fitid = value;
    } else if (tag == "NAME") {
        f.name = value;
    } else if (tag == "MEMO") {
        f.memo = value;
    } else if (tag == "REFNUM") {
        f.refnum = value;
    } else if (tag == "CHECKNUM") {
        f.checknum = value;
    }
}

Result<Fields> readFields(string_view block, size_t base) noexcept {
    Fields fields;
    for (size_t i = block.find('<'); i != string_view::npos; i = block.find('<', i)) {
        const size_t close = block.find('>', i);
        if (close == string_view::npos) {
            return fail(ErrorCode::UnexpectedEnd, base + i, "tag OFX sem '>'");
        }
        const string_view tag = block.substr(i + 1, close - i - 1);
        const size_t next = block.find('<', close);
        if (!tag.empty() && tag[0] != '/') {
            assign(fields, tag, trim(block.substr(close + 1, next == string_view::npos ? string_view::npos : next - close - 1)));
        }
        i = close;
    }
    return fields;
}

Result<Money> parseSigned(string_view text, size_t offset) noexcept {
    bool negative = false;
    if (!text.empty() && (text[0] == '-' || text[0] == '+')) {
        negative = text[0] == '-';
        text.remove_prefix(1);
        ++offset;
    }
    array<char, 32> buffer{};
    if (text.size() > buffer.size()) {
        return fail(ErrorCode::ValueTooLong, offset, "valor OFX longo demais");
    }
    for (size_t i = 0; i < text.size(); ++i) {
        buffer[i] = text[i] == ',' ? '.' : text[i];
    }
    const auto amount = parseAmount(string_view(buffer.data(), text.size()));
    if (!amount) {
        Error error = amount.error();
        error.offset += offset;
        return unexpected(error);
    }
    return negative ? -*amount : *amount;
}

Result<year_month_day> parseDate(string_view text, size_t offset) noexcept {
    const auto y = parseDigits(text.substr(0, 4));
    const auto m = text.size() >= 6 ? parseDigits(text.substr(4, 2)) : nullopt;
    const auto d = text.size() >= 8 ? parseDigits(text.substr(6, 2)) : nullopt;
    const auto date = y && m && d ? makeDate(*y, *m, *d) : nullopt;
    if (!date) {
        return fail(ErrorCode::InvalidValue, offset, "DTPOSTED deve começar com AAAAMMDD");
    }
    return *date;
}

size_t offsetOf(string_view data, string_view part) noexcept { return static_cast<size_t>(part.data() - data.data()); }

}

Result<Summary> parseOfx(string_view data, const Sink& sink) {
    if (data.find("<OFX>") == string_view::npos) {
        return fail(ErrorCode::InvalidValue, 0, "arquivo sem <OFX>");
    }
    const string_view header = data.substr(0, data.find("<OFX>"));
    const bool latin1 = header.find("1252") != string_view::npos || header.find("8859-1") != string_view::npos;
    array<string, 5> scratch;
    Summary summary;
    for (size_t pos = data.find(kOpen); pos != string_view::npos; pos = data.find(kOpen, pos)) {
        const size_t end = data.find(kClose, pos);
        if (end == string_view::npos) {
            return fail(ErrorCode::UnexpectedEnd, pos, "<STMTTRN> sem fechamento");
        }
        const size_t start = pos + kOpen.size();
        const auto fields = readFields(data.substr(start, end - start), start);
        if (!fields) {
            return unexpected(fields.error());
        }
        ++summary.records;
        if (fields->posted.empty() || fields->amount.empty() || fields->fitid.empty()) {
            return fail(ErrorCode::MissingField, pos, "STMTTRN sem DTPOSTED, TRNAMT ou FITID");
        }
        const auto amount = parseSigned(fields->amount, offsetOf(data, fields->amount));
        if (!amount) {
            return unexpected(amount.error());
        }
        const auto date = parseDate(fields->posted, offsetOf(data, fields->posted));
        if (!date) {
            return unexpected(date.error());
        }

        Transaction tx;
        tx.source = Source::Ofx;
        tx.position = pos;
        tx.amount = *amount;
        tx.date = *date;
        tx.id = decodeOfx(fields->fitid, latin1, scratch[0]);
        tx.reference = decodeOfx(fields->refnum.empty() ? fields->checknum : fields->refnum, latin1, scratch[1]);
        tx.payerName = decodeOfx(fields->name, latin1, scratch[2]);
        tx.description = decodeOfx(fields->memo, latin1, scratch[3]);
        tx.movement = decodeOfx(fields->type, latin1, scratch[4]);
        if (const auto added = accumulate(summary, tx.amount); !added) {
            return fail(ErrorCode::Overflow, pos, "total do arquivo excede o limite");
        }
        sink(tx);
        pos = end + kClose.size();
    }
    return summary;
}

}
