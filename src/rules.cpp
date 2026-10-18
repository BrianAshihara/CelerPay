#include <celerpay/rules.hpp>

#include "wide.hpp"

#include <algorithm>
#include <format>

using namespace std;
using namespace std::chrono;

namespace celerpay::rules {

namespace {

using celerpay::detail::fail;
using celerpay::detail::isDigit;
using celerpay::detail::mulDivRound;

constexpr int64_t kMaxPercentUnits = 1000 * 100'000'000LL;
constexpr uint64_t kFixed = 1'000'000'000'000'000;
constexpr uint64_t kRateToFixed = kFixed / static_cast<uint64_t>(Rate::kScale);

struct FixedHoliday {
    unsigned month;
    unsigned day;
    int since;
};

constexpr array<FixedHoliday, 9> kFixedHolidays = {{
    {1, 1, 0},
    {4, 21, 0},
    {5, 1, 0},
    {9, 7, 0},
    {10, 12, 1980},
    {11, 2, 0},
    {11, 15, 0},
    {11, 20, 2024},
    {12, 25, 0},
}};

constexpr array<int, 4> kEasterOffsets = {-48, -47, -2, 60};

bool isNationalHoliday(year_month_day date) noexcept {
    const int y = static_cast<int>(date.year());
    const unsigned m = static_cast<unsigned>(date.month());
    const unsigned d = static_cast<unsigned>(date.day());
    for (const FixedHoliday& holiday : kFixedHolidays) {
        if (holiday.month == m && holiday.day == d && y >= holiday.since) {
            return true;
        }
    }
    const sys_days day{date};
    const sys_days easterDay{easter(date.year())};
    return any_of(kEasterOffsets.begin(), kEasterOffsets.end(), [&](int offset) { return day == easterDay + days{offset}; });
}

Result<Money> percentOf(Money principal, Rate rate) noexcept {
    const auto value = mulDivRound(static_cast<uint64_t>(principal.cents()), static_cast<uint64_t>(rate.scaled()), 1,
                                   static_cast<uint64_t>(Rate::kScale));
    if (!value || *value > static_cast<uint64_t>(numeric_limits<int64_t>::max())) {
        return fail(ErrorCode::Overflow, 0, "valor calculado excede o limite");
    }
    return Money::fromCents(static_cast<int64_t>(*value));
}

optional<uint64_t> fixedMul(uint64_t a, uint64_t b) noexcept { return mulDivRound(a, b, 1, kFixed); }

optional<uint64_t> fixedPow(uint64_t base, int64_t exponent) noexcept {
    uint64_t result = kFixed;
    while (exponent > 0) {
        if (exponent & 1) {
            const auto next = fixedMul(result, base);
            if (!next) {
                return nullopt;
            }
            result = *next;
        }
        exponent >>= 1;
        if (exponent > 0) {
            const auto squared = fixedMul(base, base);
            if (!squared) {
                return nullopt;
            }
            base = *squared;
        }
    }
    return result;
}

uint64_t root30(uint64_t value) noexcept {
    uint64_t low = kFixed;
    uint64_t high = value;
    while (low < high) {
        const uint64_t mid = low + (high - low + 1) / 2;
        const auto power = fixedPow(mid, 30);
        if (power && *power <= value) {
            low = mid;
        } else {
            high = mid - 1;
        }
    }
    return low;
}

Result<Money> compoundInterest(Money principal, Rate rate, int64_t daysLate) noexcept {
    const uint64_t base = kFixed + static_cast<uint64_t>(rate.scaled()) * kRateToFixed;
    const auto months = fixedPow(base, daysLate / 30);
    const auto partial = fixedPow(root30(base), daysLate % 30);
    if (!months || !partial) {
        return fail(ErrorCode::Overflow, 0, "fator de juros composto excede o limite");
    }
    const auto factor = fixedMul(*months, *partial);
    if (!factor) {
        return fail(ErrorCode::Overflow, 0, "fator de juros composto excede o limite");
    }
    const auto value = mulDivRound(static_cast<uint64_t>(principal.cents()), *factor - kFixed, 1, kFixed);
    if (!value || *value > static_cast<uint64_t>(numeric_limits<int64_t>::max())) {
        return fail(ErrorCode::Overflow, 0, "juros excedem o limite");
    }
    return Money::fromCents(static_cast<int64_t>(*value));
}

Result<Money> simpleInterest(Money principal, Rate rate, int64_t daysLate, uint64_t divisor) noexcept {
    const auto value = mulDivRound(static_cast<uint64_t>(principal.cents()), static_cast<uint64_t>(rate.scaled()),
                                   static_cast<uint64_t>(daysLate), static_cast<uint64_t>(Rate::kScale) * divisor);
    if (!value || *value > static_cast<uint64_t>(numeric_limits<int64_t>::max())) {
        return fail(ErrorCode::Overflow, 0, "juros excedem o limite");
    }
    return Money::fromCents(static_cast<int64_t>(*value));
}

Result<Money> interestFor(const Interest& interest, Money principal, int64_t daysLate) noexcept {
    switch (interest.type) {
    case InterestType::None:
        return Money{};
    case InterestType::DailyAmount:
        return checkedMul(interest.amount, daysLate);
    case InterestType::MonthlyRate:
        return simpleInterest(principal, interest.rate, daysLate, 30);
    case InterestType::DailyRate:
        return simpleInterest(principal, interest.rate, daysLate, 1);
    case InterestType::MonthlyCompound:
        return compoundInterest(principal, interest.rate, daysLate);
    }
    return Money{};
}

Result<Money> fineFor(const Fine& fine, Money principal) noexcept {
    switch (fine.type) {
    case FineType::None:
        return Money{};
    case FineType::Amount:
        return fine.amount;
    case FineType::Percent:
        return percentOf(principal, fine.rate);
    }
    return Money{};
}

Result<Money> discountFor(const Discount& discount, Money principal, year_month_day dueDate, year_month_day payment,
                          const Calendar& calendar, int& daysInAdvance) noexcept {
    daysInAdvance = max(0, static_cast<int>((sys_days{dueDate} - sys_days{payment}).count()));
    Result<Money> value = Money{};
    switch (discount.type) {
    case DiscountType::None:
        break;
    case DiscountType::AmountUntil:
    case DiscountType::PercentUntil:
        for (size_t i = 0; i < discount.tierCount; ++i) {
            const DiscountTier& tier = discount.tiers[i];
            if (sys_days{payment} <= sys_days{calendar.nextBusinessDay(tier.until)}) {
                value = discount.type == DiscountType::AmountUntil ? Result<Money>{tier.amount} : percentOf(principal, tier.rate);
                break;
            }
        }
        break;
    case DiscountType::PerDayInAdvance:
        value = checkedMul(discount.perDay, daysInAdvance);
        break;
    }
    if (value && *value > principal) {
        return principal;
    }
    return value;
}

bool validRate(Rate rate) noexcept { return rate.scaled() >= 0 && rate.scaled() <= kMaxPercentUnits; }

Result<void> validate(const ChargeTerms& terms, year_month_day paymentDate) noexcept {
    if (terms.principal.cents() < 0) {
        return fail(ErrorCode::InvalidValue, 0, "principal negativo");
    }
    if (!terms.dueDate.ok() || !paymentDate.ok()) {
        return fail(ErrorCode::InvalidValue, 0, "data inválida");
    }
    if (terms.interest.amount.cents() < 0 || terms.fine.amount.cents() < 0 || terms.discount.perDay.cents() < 0) {
        return fail(ErrorCode::InvalidValue, 0, "valor negativo nas regras");
    }
    if (!validRate(terms.interest.rate) || !validRate(terms.fine.rate)) {
        return fail(ErrorCode::InvalidValue, 0, "taxa fora de 0 a 1000%");
    }
    if (terms.discount.tierCount > terms.discount.tiers.size()) {
        return fail(ErrorCode::InvalidValue, 0, "no máximo 3 faixas de desconto");
    }
    for (size_t i = 0; i < terms.discount.tierCount; ++i) {
        const DiscountTier& tier = terms.discount.tiers[i];
        if (!tier.until.ok() || tier.amount.cents() < 0 || !validRate(tier.rate)) {
            return fail(ErrorCode::InvalidValue, i, "faixa de desconto inválida");
        }
        if (i > 0 && sys_days{tier.until} <= sys_days{terms.discount.tiers[i - 1].until}) {
            return fail(ErrorCode::InvalidValue, i, "faixas de desconto devem estar em ordem crescente de data");
        }
    }
    return {};
}

string formatDate(year_month_day date) {
    return format("{:02}/{:02}/{:04}", static_cast<unsigned>(date.day()), static_cast<unsigned>(date.month()),
                  static_cast<int>(date.year()));
}

}

Result<Rate> parsePercent(string_view text) noexcept {
    if (text.empty()) {
        return fail(ErrorCode::EmptyInput, 0, "taxa vazia");
    }
    int64_t units = 0;
    size_t i = 0;
    for (; i < text.size() && isDigit(text[i]); ++i) {
        units = units * 10 + (text[i] - '0');
        if (units > 1000) {
            return fail(ErrorCode::InvalidValue, i, "taxa acima de 1000%");
        }
    }
    if (i == 0) {
        return fail(ErrorCode::InvalidCharacter, 0, "esperado um dígito");
    }
    units *= 100'000'000;
    if (i < text.size()) {
        if (text[i] != '.') {
            return fail(ErrorCode::InvalidCharacter, i, "esperado um dígito ou '.'");
        }
        ++i;
        if (i == text.size()) {
            return fail(ErrorCode::UnexpectedEnd, i, "esperado um dígito após o '.'");
        }
        int64_t scale = 10'000'000;
        for (; i < text.size(); ++i) {
            if (!isDigit(text[i])) {
                return fail(ErrorCode::InvalidCharacter, i, "esperado um dígito");
            }
            if (scale == 0) {
                return fail(ErrorCode::TooManyDecimals, i, "mais de 8 casas decimais");
            }
            units += (text[i] - '0') * scale;
            scale /= 10;
        }
    }
    if (units > kMaxPercentUnits) {
        return fail(ErrorCode::InvalidValue, 0, "taxa acima de 1000%");
    }
    return Rate::fromScaled(units);
}

year_month_day easter(year y) noexcept {
    const int value = static_cast<int>(y);
    const int a = value % 19;
    const int b = value / 100;
    const int c = value % 100;
    const int d = b / 4;
    const int e = b % 4;
    const int f = (b + 8) / 25;
    const int g = (b - f + 1) / 3;
    const int h = (19 * a + b - d - g + 15) % 30;
    const int i = c / 4;
    const int k = c % 4;
    const int l = (32 + 2 * e + 2 * i - h - k) % 7;
    const int m = (a + 11 * h + 22 * l) / 451;
    const int month = (h + l - 7 * m + 114) / 31;
    const int day = (h + l - 7 * m + 114) % 31 + 1;
    return year_month_day{y, chrono::month{static_cast<unsigned>(month)}, chrono::day{static_cast<unsigned>(day)}};
}

Calendar Calendar::weekendsOnly() { return Calendar{}; }

Calendar Calendar::brazil() {
    Calendar calendar;
    calendar.national_ = true;
    return calendar;
}

Result<void> Calendar::addHoliday(year_month_day date) {
    if (!date.ok()) {
        return fail(ErrorCode::InvalidValue, 0, "data inválida");
    }
    const sys_days day{date};
    const auto it = lower_bound(extras_.begin(), extras_.end(), day);
    if (it == extras_.end() || *it != day) {
        extras_.insert(it, day);
    }
    return {};
}

bool Calendar::isHoliday(year_month_day date) const noexcept {
    return (national_ && isNationalHoliday(date)) || binary_search(extras_.begin(), extras_.end(), sys_days{date});
}

bool Calendar::isBusinessDay(year_month_day date) const noexcept {
    const weekday day{sys_days{date}};
    return day != Saturday && day != Sunday && !isHoliday(date);
}

year_month_day Calendar::nextBusinessDay(year_month_day date) const noexcept {
    sys_days day{date};
    while (!isBusinessDay(year_month_day{day})) {
        day += days{1};
    }
    return year_month_day{day};
}

year_month_day Calendar::addBusinessDays(year_month_day date, int count) const noexcept {
    sys_days day{date};
    const days step{count >= 0 ? 1 : -1};
    for (int remaining = count >= 0 ? count : -count; remaining > 0;) {
        day += step;
        if (isBusinessDay(year_month_day{day})) {
            --remaining;
        }
    }
    return year_month_day{day};
}

int Calendar::businessDaysBetween(year_month_day from, year_month_day to) const noexcept {
    sys_days begin{from};
    sys_days end{to};
    const int sign = begin <= end ? 1 : -1;
    if (sign < 0) {
        swap(begin, end);
    }
    int count = 0;
    for (sys_days day = begin; day < end; day += days{1}) {
        count += isBusinessDay(year_month_day{day}) ? 1 : 0;
    }
    return sign * count;
}

Result<Breakdown> amountDue(const ChargeTerms& terms, year_month_day paymentDate, const Calendar& calendar) {
    if (const auto valid = validate(terms, paymentDate); !valid) {
        return unexpected(valid.error());
    }
    Breakdown out;
    out.principal = terms.principal;
    out.effectiveDueDate = calendar.nextBusinessDay(terms.dueDate);

    if (sys_days{paymentDate} > sys_days{out.effectiveDueDate}) {
        out.daysLate = static_cast<int>((sys_days{paymentDate} - sys_days{terms.dueDate}).count());
        const auto interest = interestFor(terms.interest, terms.principal, out.daysLate);
        if (!interest) {
            return unexpected(interest.error());
        }
        const auto fine = fineFor(terms.fine, terms.principal);
        if (!fine) {
            return unexpected(fine.error());
        }
        out.interest = *interest;
        out.fine = *fine;
    } else {
        const auto discount = discountFor(terms.discount, terms.principal, terms.dueDate, paymentDate, calendar, out.daysInAdvance);
        if (!discount) {
            return unexpected(discount.error());
        }
        out.discount = *discount;
    }

    const auto withInterest = checkedAdd(out.principal, out.interest);
    const auto withFine = withInterest ? checkedAdd(*withInterest, out.fine) : withInterest;
    const auto total = withFine ? checkedSub(*withFine, out.discount) : withFine;
    if (!total) {
        return unexpected(total.error());
    }
    out.total = *total;
    return out;
}

string explain(const Breakdown& b) {
    string out = "principal " + formatBrl(b.principal);
    if (b.interest.cents() != 0) {
        out += " + juros " + formatBrl(b.interest) + " (" + to_string(b.daysLate) + (b.daysLate == 1 ? " dia" : " dias") + " de atraso)";
    }
    if (b.fine.cents() != 0) {
        out += " + multa " + formatBrl(b.fine);
    }
    if (b.discount.cents() != 0) {
        out += " - desconto " + formatBrl(b.discount);
        if (b.daysInAdvance > 0) {
            out += " (" + to_string(b.daysInAdvance) + (b.daysInAdvance == 1 ? " dia" : " dias") + " de antecipação)";
        }
    }
    out += " = " + formatBrl(b.total) + ", vencimento efetivo " + formatDate(b.effectiveDueDate);
    return out;
}

}
