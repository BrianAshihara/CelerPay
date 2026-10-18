#pragma once

#include <celerpay/error.hpp>
#include <celerpay/export.hpp>
#include <celerpay/money.hpp>
#include <celerpay/rules.hpp>
#include <celerpay/statement.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace celerpay::reconcile {

struct Charge {
    std::string_view id;
    std::string_view txid;
    std::string_view ourNumber;
    std::string_view payerDocument;
    Money amount;
    std::chrono::year_month_day dueDate;
    rules::Interest interest;
    rules::Fine fine;
    rules::Discount discount;
};

struct Payment {
    /// Chave da cobrança informada pelo pagamento: nosso número (CNAB) ou txid (PIX).
    std::string_view id;
    std::string_view endToEndId;
    /// Identificador da transação no banco (FITID do OFX). Não casa com cobranças, só detecta repetição.
    std::string_view bankId;
    std::string_view reference;
    std::string_view payerDocument;
    Money amount;
    std::chrono::year_month_day date;
};

/// Copia transações de um extrato para memória própria, para que sobrevivam ao callback.
class PaymentStore {
public:
    CELERPAY_API void add(const statement::Transaction& transaction);
    std::span<const Payment> payments() const noexcept { return payments_; }

private:
    std::string_view keep(std::string_view text);

    std::vector<std::unique_ptr<char[]>> blocks_;
    std::size_t used_ = 0;
    std::size_t capacity_ = 0;
    std::vector<Payment> payments_;
};

enum class Status { Matched, Partial, Overpaid, Duplicate, Unidentified, Pending, Overdue };

enum class Pass { ExactKey, Heuristic, Approximate, Grouped, None };

enum class Reason {
    Key,
    DuplicateKey,
    DuplicateEndToEnd,
    DuplicateBankId,
    DuplicatePayment,
    PayerAmountDate,
    ExplainedByRules,
    WithinTolerance,
    AmountDate,
    SubsetSum,
    NoCandidate,
    NotDueYet,
    PastDue,
};

struct Group {
    Status status = Status::Unidentified;
    Pass pass = Pass::None;
    Reason reason = Reason::NoCandidate;
    std::uint8_t confidence = 0;
    Money expected;
    Money paid;
};

struct Options {
    std::chrono::year_month_day referenceDate;
    int daysBefore = 30;
    int daysAfter = 60;
    rules::Rate tolerance = rules::Rate::fromScaled(200'000'000);
    std::size_t maxGroupSize = 5;
    std::uint64_t nodeBudget = 100'000;
    std::size_t maxGroupCandidates = 20;
};

class Report {
public:
    std::span<const Group> groups() const noexcept { return groups_; }
    CELERPAY_API std::span<const std::uint32_t> charges(std::size_t group) const noexcept;
    CELERPAY_API std::span<const std::uint32_t> payments(std::size_t group) const noexcept;
    std::uint32_t groupOfCharge(std::size_t charge) const noexcept { return chargeGroup_[charge]; }
    std::uint32_t groupOfPayment(std::size_t payment) const noexcept { return paymentGroup_[payment]; }
    CELERPAY_API std::size_t count(Status status) const noexcept;

private:
    friend struct ReportBuilder;

    std::vector<Group> groups_;
    std::vector<std::uint32_t> chargeBegin_;
    std::vector<std::uint32_t> paymentBegin_;
    std::vector<std::uint32_t> groupCharges_;
    std::vector<std::uint32_t> groupPayments_;
    std::vector<std::uint32_t> chargeGroup_;
    std::vector<std::uint32_t> paymentGroup_;
};

CELERPAY_API Result<Report> run(std::span<const Charge> charges, std::span<const Payment> payments, const Options& options,
                                      const rules::Calendar& calendar);

/// Texto curto com status, passada, motivo, itens e valores do grupo.
CELERPAY_API std::string explain(const Report& report, std::size_t group, std::span<const Charge> charges,
                                 std::span<const Payment> payments);

CELERPAY_API std::string_view toString(Status status) noexcept;
CELERPAY_API std::string_view toString(Pass pass) noexcept;
CELERPAY_API std::string_view toString(Reason reason) noexcept;

}
