#pragma once

#include <celerpay/error.hpp>
#include <celerpay/export.hpp>
#include <celerpay/money.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace celerpay::rules {

/// Taxa como fração com escala 10^10. 1% é 100'000'000.
class Rate {
public:
    static constexpr std::int64_t kScale = 10'000'000'000;

    constexpr Rate() noexcept = default;
    static constexpr Rate fromScaled(std::int64_t units) noexcept { return Rate{units}; }
    constexpr std::int64_t scaled() const noexcept { return units_; }

    friend constexpr auto operator<=>(Rate, Rate) noexcept = default;

private:
    explicit constexpr Rate(std::int64_t units) noexcept : units_{units} {}

    std::int64_t units_ = 0;
};

/// "1.5" vira 1,5%. Até 8 casas decimais, de 0 a 1000.
CELERPAY_API Result<Rate> parsePercent(std::string_view text) noexcept;

CELERPAY_API std::chrono::year_month_day easter(std::chrono::year year) noexcept;

class Calendar {
public:
    /// Só sábados e domingos são não úteis.
    CELERPAY_API static Calendar weekendsOnly();
    /// Padrão ANBIMA: feriados nacionais, Carnaval, Sexta-feira Santa e Corpus Christi.
    CELERPAY_API static Calendar brazil();

    CELERPAY_API Result<void> addHoliday(std::chrono::year_month_day date);
    CELERPAY_API bool isHoliday(std::chrono::year_month_day date) const noexcept;
    CELERPAY_API bool isBusinessDay(std::chrono::year_month_day date) const noexcept;
    /// A própria data se for dia útil, senão o próximo dia útil.
    CELERPAY_API std::chrono::year_month_day nextBusinessDay(std::chrono::year_month_day date) const noexcept;
    CELERPAY_API std::chrono::year_month_day addBusinessDays(std::chrono::year_month_day date, int count) const noexcept;
    /// Dias úteis em [from, to). Negativo se to vier antes de from.
    CELERPAY_API int businessDaysBetween(std::chrono::year_month_day from, std::chrono::year_month_day to) const noexcept;

private:
    bool national_ = false;
    std::vector<std::chrono::sys_days> extras_;
};

enum class InterestType { None, DailyAmount, MonthlyRate, DailyRate, MonthlyCompound };

struct Interest {
    InterestType type = InterestType::None;
    Money amount;
    Rate rate;
};

enum class FineType { None, Amount, Percent };

struct Fine {
    FineType type = FineType::None;
    Money amount;
    Rate rate;
};

enum class DiscountType { None, AmountUntil, PercentUntil, PerDayInAdvance };

struct DiscountTier {
    std::chrono::year_month_day until;
    Money amount;
    Rate rate;
};

struct Discount {
    DiscountType type = DiscountType::None;
    std::array<DiscountTier, 3> tiers{};
    std::size_t tierCount = 0;
    Money perDay;
};

struct ChargeTerms {
    Money principal;
    std::chrono::year_month_day dueDate;
    Interest interest;
    Fine fine;
    Discount discount;
};

struct Breakdown {
    Money principal;
    Money interest;
    Money fine;
    Money discount;
    Money total;
    std::chrono::year_month_day effectiveDueDate;
    int daysLate = 0;
    int daysInAdvance = 0;

    friend bool operator==(const Breakdown&, const Breakdown&) noexcept = default;
};

CELERPAY_API Result<Breakdown> amountDue(const ChargeTerms& terms, std::chrono::year_month_day paymentDate,
                                         const Calendar& calendar);

/// Texto curto com cada parcela, ex: "principal R$ 100,00 + juros R$ 0,33 (1 dia de atraso) = R$ 100,33, vencimento efetivo 05/03/2025".
CELERPAY_API std::string explain(const Breakdown& breakdown);

}
