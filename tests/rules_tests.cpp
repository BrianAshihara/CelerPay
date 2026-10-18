#include <celerpay/rules.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace std;
using namespace std::chrono;
using namespace celerpay;
using namespace celerpay::rules;

namespace {

Rate percent(string_view text) { return *parsePercent(text); }

ChargeTerms charge(int64_t cents, year_month_day due) {
    ChargeTerms terms;
    terms.principal = Money::fromCents(cents);
    terms.dueDate = due;
    return terms;
}

Breakdown due(const ChargeTerms& terms, year_month_day payment) {
    const auto result = amountDue(terms, payment, Calendar::brazil());
    REQUIRE(result);
    return *result;
}

}

TEST_CASE("parsePercent", "[rules]") {
    CHECK(percent("1").scaled() == 100'000'000);
    CHECK(percent("1.5").scaled() == 150'000'000);
    CHECK(percent("0.033").scaled() == 3'300'000);
    CHECK(percent("0.00000001").scaled() == 1);
    CHECK(percent("1000").scaled() == 100'000'000'000);
    CHECK(parsePercent("").error().code == ErrorCode::EmptyInput);
    CHECK(parsePercent(".5").error().code == ErrorCode::InvalidCharacter);
    CHECK(parsePercent("1.").error().code == ErrorCode::UnexpectedEnd);
    CHECK(parsePercent("1,5").error().code == ErrorCode::InvalidCharacter);
    CHECK(parsePercent("1.123456789").error().code == ErrorCode::TooManyDecimals);
    CHECK(parsePercent("1001").error().code == ErrorCode::InvalidValue);
    CHECK(parsePercent("1000.00000001").error().code == ErrorCode::InvalidValue);
}

TEST_CASE("Pascoa", "[rules][calendar]") {
    CHECK(easter(2000y) == 2000y / April / 23d);
    CHECK(easter(2008y) == 2008y / March / 23d);
    CHECK(easter(2019y) == 2019y / April / 21d);
    CHECK(easter(2024y) == 2024y / March / 31d);
    CHECK(easter(2025y) == 2025y / April / 20d);
    CHECK(easter(2026y) == 2026y / April / 5d);
    CHECK(easter(2038y) == 2038y / April / 25d);
}

TEST_CASE("feriados nacionais e moveis", "[rules][calendar]") {
    const Calendar calendar = Calendar::brazil();
    for (const auto date : {2025y / January / 1d, 2025y / March / 3d, 2025y / March / 4d, 2025y / April / 18d, 2025y / April / 21d,
                            2025y / May / 1d, 2025y / June / 19d, 2025y / October / 12d, 2025y / November / 20d,
                            2025y / December / 25d, 2026y / February / 16d, 2026y / February / 17d, 2026y / April / 3d,
                            2026y / June / 4d}) {
        INFO(static_cast<int>(date.year()) << "-" << static_cast<unsigned>(date.month()) << "-" << static_cast<unsigned>(date.day()));
        CHECK(calendar.isHoliday(date));
        CHECK_FALSE(calendar.isBusinessDay(date));
    }
    CHECK_FALSE(calendar.isHoliday(2023y / November / 20d));
    CHECK(calendar.isBusinessDay(2023y / November / 20d));
    CHECK(calendar.isHoliday(2024y / November / 20d));
    CHECK(calendar.isBusinessDay(2025y / March / 5d));
    CHECK_FALSE(calendar.isBusinessDay(2025y / March / 1d));
    CHECK(Calendar::weekendsOnly().isBusinessDay(2025y / December / 25d));
    CHECK_FALSE(Calendar::weekendsOnly().isBusinessDay(2025y / December / 27d));
}

TEST_CASE("operacoes com dias uteis", "[rules][calendar]") {
    Calendar calendar = Calendar::brazil();
    CHECK(calendar.nextBusinessDay(2025y / March / 1d) == 2025y / March / 5d);
    CHECK(calendar.nextBusinessDay(2025y / March / 5d) == 2025y / March / 5d);
    CHECK(calendar.addBusinessDays(2025y / December / 24d, 1) == 2025y / December / 26d);
    CHECK(calendar.addBusinessDays(2025y / December / 26d, -1) == 2025y / December / 24d);
    CHECK(calendar.addBusinessDays(2025y / March / 1d, 0) == 2025y / March / 1d);
    CHECK(calendar.businessDaysBetween(2025y / January / 1d, 2025y / February / 1d) == 22);
    CHECK(calendar.businessDaysBetween(2025y / February / 1d, 2025y / January / 1d) == -22);
    CHECK(calendar.businessDaysBetween(2024y / February / 1d, 2024y / March / 1d) == 19);

    REQUIRE(calendar.addHoliday(2025y / July / 9d));
    REQUIRE(calendar.addHoliday(2025y / July / 9d));
    CHECK(calendar.isHoliday(2025y / July / 9d));
    CHECK(calendar.nextBusinessDay(2025y / July / 9d) == 2025y / July / 10d);
    CHECK(calendar.addHoliday(2025y / February / 30d).error().code == ErrorCode::InvalidValue);
}

TEST_CASE("pagamento ate o vencimento nao tem encargos", "[rules]") {
    ChargeTerms terms = charge(100000, 2025y / March / 20d);
    terms.interest = {InterestType::MonthlyRate, {}, percent("1")};
    terms.fine = {FineType::Percent, {}, percent("2")};
    const auto b = due(terms, 2025y / March / 20d);
    CHECK(b.total.cents() == 100000);
    CHECK(b.daysLate == 0);
}

TEST_CASE("vencimento em dia nao util e prorrogado", "[rules]") {
    ChargeTerms terms = charge(100000, 2025y / March / 1d);
    terms.interest = {InterestType::DailyAmount, Money::fromCents(33), {}};
    terms.fine = {FineType::Amount, Money::fromCents(500), {}};
    const auto onTime = due(terms, 2025y / March / 5d);
    CHECK(onTime.effectiveDueDate == 2025y / March / 5d);
    CHECK(onTime.total.cents() == 100000);
    const auto late = due(terms, 2025y / March / 6d);
    CHECK(late.daysLate == 5);
    CHECK(late.interest.cents() == 165);
    CHECK(late.fine.cents() == 500);
    CHECK(late.total.cents() == 100665);
}

TEST_CASE("fim de mes e ano bissexto", "[rules]") {
    ChargeTerms terms = charge(100000, 2024y / February / 29d);
    terms.interest = {InterestType::DailyRate, {}, percent("0.033")};
    CHECK(due(terms, 2024y / March / 1d).daysLate == 1);
    CHECK(due(terms, 2024y / March / 1d).interest.cents() == 33);
    CHECK(due(terms, 2025y / February / 28d).daysLate == 365);

    terms.dueDate = 2025y / January / 31d;
    const auto b = due(terms, 2025y / February / 3d);
    CHECK(b.daysLate == 3);
    CHECK(b.interest.cents() == 99);
}

TEST_CASE("juros simples arredondam meio para cima", "[rules]") {
    ChargeTerms terms = charge(100000, 2025y / March / 20d);
    terms.interest = {InterestType::MonthlyRate, {}, percent("1")};
    CHECK(due(terms, 2025y / March / 21d).interest.cents() == 33);
    CHECK(due(terms, 2025y / March / 30d).interest.cents() == 333);
    CHECK(due(terms, 2025y / April / 4d).interest.cents() == 500);

    terms.principal = Money::fromCents(1500);
    CHECK(due(terms, 2025y / March / 21d).interest.cents() == 1);
    terms.principal = Money::fromCents(1499);
    CHECK(due(terms, 2025y / March / 21d).interest.cents() == 0);
}

TEST_CASE("juros composto mensal", "[rules]") {
    ChargeTerms terms = charge(100000, 2025y / March / 20d);
    terms.interest = {InterestType::MonthlyCompound, {}, percent("1")};
    const sys_days dueDay{terms.dueDate};
    auto after = [&](int n) { return year_month_day{dueDay + days{n}}; };
    CHECK(due(terms, after(30)).interest.cents() == 1000);
    CHECK(due(terms, after(60)).interest.cents() == 2010);
    CHECK(due(terms, after(15)).interest.cents() == 499);
    CHECK(due(terms, after(1)).interest.cents() == 33);
    CHECK(due(terms, after(45)).interest.cents() == 1504);

    terms.principal = Money::fromCents(1'000'000'000'000);
    terms.interest.rate = percent("1000");
    CHECK(amountDue(terms, after(3650), Calendar::brazil()).error().code == ErrorCode::Overflow);
}

TEST_CASE("multa", "[rules]") {
    ChargeTerms terms = charge(100000, 2025y / March / 20d);
    terms.fine = {FineType::Percent, {}, percent("2")};
    CHECK(due(terms, 2025y / March / 21d).fine.cents() == 2000);
    CHECK(due(terms, 2025y / May / 21d).fine.cents() == 2000);
}

TEST_CASE("descontos por faixa", "[rules]") {
    ChargeTerms terms = charge(100000, 2025y / March / 20d);
    terms.discount.type = DiscountType::AmountUntil;
    terms.discount.tiers[0] = {2025y / March / 10d, Money::fromCents(2000), {}};
    terms.discount.tiers[1] = {2025y / March / 15d, Money::fromCents(1000), {}};
    terms.discount.tierCount = 2;
    const auto early = due(terms, 2025y / March / 5d);
    CHECK(early.discount.cents() == 2000);
    CHECK(early.daysInAdvance == 15);
    CHECK(due(terms, 2025y / March / 12d).discount.cents() == 1000);
    CHECK(due(terms, 2025y / March / 17d).discount.cents() == 1000);
    CHECK(due(terms, 2025y / March / 18d).discount.cents() == 0);

    terms.discount.type = DiscountType::PercentUntil;
    terms.discount.tiers[0].rate = percent("5");
    CHECK(due(terms, 2025y / March / 5d).discount.cents() == 5000);

    terms.discount.tiers[1].until = 2025y / March / 1d;
    CHECK(amountDue(terms, 2025y / March / 5d, Calendar::brazil()).error().code == ErrorCode::InvalidValue);
}

TEST_CASE("desconto por antecipacao e limite no principal", "[rules]") {
    ChargeTerms terms = charge(100000, 2025y / March / 20d);
    terms.discount.type = DiscountType::PerDayInAdvance;
    terms.discount.perDay = Money::fromCents(10);
    CHECK(due(terms, 2025y / March / 15d).discount.cents() == 50);
    CHECK(due(terms, 2025y / March / 20d).discount.cents() == 0);
    terms.discount.perDay = Money::fromCents(100000);
    CHECK(due(terms, 2025y / March / 15d).total.cents() == 0);
}

TEST_CASE("validacao das regras", "[rules]") {
    ChargeTerms terms = charge(-1, 2025y / March / 20d);
    CHECK(amountDue(terms, 2025y / March / 20d, Calendar::brazil()).error().code == ErrorCode::InvalidValue);
    terms = charge(100, 2025y / February / 30d);
    CHECK(amountDue(terms, 2025y / March / 20d, Calendar::brazil()).error().code == ErrorCode::InvalidValue);
    terms = charge(100, 2025y / March / 20d);
    terms.interest = {InterestType::MonthlyRate, {}, Rate::fromScaled(-1)};
    CHECK(amountDue(terms, 2025y / March / 21d, Calendar::brazil()).error().code == ErrorCode::InvalidValue);
    terms.interest = {InterestType::DailyAmount, Money::fromCents(numeric_limits<int64_t>::max()), {}};
    CHECK(amountDue(terms, 2025y / March / 25d, Calendar::brazil()).error().code == ErrorCode::Overflow);
}

TEST_CASE("explain", "[rules]") {
    ChargeTerms terms = charge(100000, 2025y / March / 1d);
    terms.interest = {InterestType::DailyAmount, Money::fromCents(33), {}};
    terms.fine = {FineType::Percent, {}, percent("2")};
    CHECK(explain(due(terms, 2025y / March / 6d)) ==
          "principal R$ 1.000,00 + juros R$ 1,65 (5 dias de atraso) + multa R$ 20,00 = R$ 1.021,65, vencimento efetivo 05/03/2025");

    terms.discount.type = DiscountType::PerDayInAdvance;
    terms.discount.perDay = Money::fromCents(10);
    CHECK(explain(due(terms, 2025y / February / 28d)) ==
          "principal R$ 1.000,00 - desconto R$ 0,10 (1 dia de antecipação) = R$ 999,90, vencimento efetivo 05/03/2025");
}
