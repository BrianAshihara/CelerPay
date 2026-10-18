#pragma once

#include <celerpay/document.hpp>
#include <celerpay/reconcile.hpp>

#include <chrono>
#include <deque>
#include <string>
#include <vector>

namespace synthetic {

enum class Kind {
    ExactTxid,
    ExactOurNumber,
    Heuristic,
    Explained,
    PartialKey,
    OverpaidKey,
    Approximate,
    DuplicateKey,
    DuplicateEndToEnd,
    Unidentified,
    Grouped,
    Split,
    Pending,
    Overdue,
    WithoutPayer,
    DuplicateKeyless,
    DuplicateBankId,
};

inline constexpr int kKinds = 17;

struct Truth {
    celerpay::reconcile::Status status;
    celerpay::reconcile::Pass pass;
    Kind kind;
};

struct Dataset {
    std::deque<std::string> text;
    std::vector<celerpay::reconcile::Charge> charges;
    std::vector<celerpay::reconcile::Payment> payments;
    std::vector<Truth> chargeTruth;
    std::vector<Truth> paymentTruth;
    std::chrono::year_month_day reference;

    std::string_view keep(std::string value) { return text.emplace_back(std::move(value)); }
};

/// Gera aproximadamente `cases` casos, um de cada tipo em sequência, com valores únicos por cobrança.
inline Dataset generate(std::size_t cases, const celerpay::rules::Calendar& calendar) {
    using namespace std::chrono;
    using celerpay::Money;
    using celerpay::reconcile::Pass;
    using celerpay::reconcile::Status;
    namespace rules = celerpay::rules;

    Dataset d;
    d.reference = 2025y / July / 31d;
    const sys_days base{2025y / May / 5d};
    std::int64_t nextAmount = 1'000'000;
    std::uint64_t nextDocument = 100'000'000;

    auto document = [&] {
        for (;;) {
            if (const auto cpf = celerpay::makeCpf(std::to_string(nextDocument++))) {
                return d.keep(std::string(cpf->digits()));
            }
        }
    };
    auto charge = [&](Kind kind, Status status, Pass pass, std::string_view doc, sys_days due) -> std::uint32_t {
        celerpay::reconcile::Charge c;
        c.id = d.keep("C" + std::to_string(d.charges.size()));
        c.payerDocument = doc;
        c.amount = Money::fromCents(nextAmount);
        nextAmount += 13;
        c.dueDate = year_month_day{due};
        d.charges.push_back(c);
        d.chargeTruth.push_back({status, pass, kind});
        return static_cast<std::uint32_t>(d.charges.size() - 1);
    };
    auto payment = [&](Kind kind, Status status, Pass pass, std::string_view id, std::string_view doc, Money amount, sys_days date,
                       std::string_view e2e = {}, std::string_view bankId = {}) {
        celerpay::reconcile::Payment p;
        p.id = id;
        p.bankId = bankId;
        p.endToEndId = e2e.empty() ? d.keep("E" + std::to_string(d.payments.size())) : e2e;
        p.payerDocument = doc;
        p.amount = amount;
        p.date = year_month_day{date};
        d.payments.push_back(p);
        d.paymentTruth.push_back({status, pass, kind});
    };
    auto percentOf = [](Money m, int percent) { return Money::fromCents(m.cents() * percent / 100); };

    for (std::size_t i = 0; i < cases; ++i) {
        const auto kind = static_cast<Kind>(i % kKinds);
        const sys_days due = base + days{static_cast<int>(i % 40)};
        const std::string_view doc = document();
        switch (kind) {
        case Kind::ExactTxid: {
            const auto c = charge(kind, Status::Matched, Pass::ExactKey, doc, due);
            d.charges[c].txid = d.keep("TX" + std::to_string(i));
            payment(kind, Status::Matched, Pass::ExactKey, d.charges[c].txid, doc, d.charges[c].amount, due - days{2});
            break;
        }
        case Kind::ExactOurNumber: {
            const auto c = charge(kind, Status::Matched, Pass::ExactKey, doc, due);
            d.charges[c].ourNumber = d.keep(std::to_string(i));
            const std::string number = std::to_string(i);
            payment(kind, Status::Matched, Pass::ExactKey, d.keep(std::string(20 - number.size(), '0') + number), doc,
                    d.charges[c].amount, due);
            break;
        }
        case Kind::Heuristic: {
            const auto c = charge(kind, Status::Matched, Pass::Heuristic, doc, due);
            payment(kind, Status::Matched, Pass::Heuristic, {}, doc, d.charges[c].amount, due - days{1});
            break;
        }
        case Kind::Explained: {
            const auto c = charge(kind, Status::Matched, Pass::Heuristic, doc, due);
            auto& ch = d.charges[c];
            ch.interest = {rules::InterestType::MonthlyRate, {}, *rules::parsePercent("1")};
            ch.fine = {rules::FineType::Percent, {}, *rules::parsePercent("2")};
            const sys_days paid = sys_days{calendar.nextBusinessDay(ch.dueDate)} + days{10};
            rules::ChargeTerms terms;
            terms.principal = ch.amount;
            terms.dueDate = ch.dueDate;
            terms.interest = ch.interest;
            terms.fine = ch.fine;
            payment(kind, Status::Matched, Pass::Heuristic, {}, doc, rules::amountDue(terms, year_month_day{paid}, calendar)->total, paid);
            break;
        }
        case Kind::PartialKey: {
            const auto c = charge(kind, Status::Partial, Pass::ExactKey, doc, due);
            d.charges[c].ourNumber = d.keep("9" + std::to_string(i));
            payment(kind, Status::Partial, Pass::ExactKey, d.charges[c].ourNumber, doc, percentOf(d.charges[c].amount, 90), due);
            break;
        }
        case Kind::OverpaidKey: {
            const auto c = charge(kind, Status::Overpaid, Pass::ExactKey, doc, due);
            d.charges[c].txid = d.keep("TX" + std::to_string(i));
            payment(kind, Status::Overpaid, Pass::ExactKey, d.charges[c].txid, doc, percentOf(d.charges[c].amount, 105), due);
            break;
        }
        case Kind::Approximate: {
            const auto c = charge(kind, Status::Partial, Pass::Approximate, doc, due);
            payment(kind, Status::Partial, Pass::Approximate, {}, doc, percentOf(d.charges[c].amount, 99), due);
            break;
        }
        case Kind::DuplicateKey: {
            const auto c = charge(kind, Status::Matched, Pass::ExactKey, doc, due);
            d.charges[c].txid = d.keep("TX" + std::to_string(i));
            payment(kind, Status::Matched, Pass::ExactKey, d.charges[c].txid, doc, d.charges[c].amount, due - days{3});
            payment(kind, Status::Duplicate, Pass::ExactKey, d.charges[c].txid, doc, d.charges[c].amount, due - days{1});
            break;
        }
        case Kind::DuplicateEndToEnd: {
            const auto c = charge(kind, Status::Matched, Pass::ExactKey, doc, due);
            d.charges[c].txid = d.keep("TX" + std::to_string(i));
            const std::string_view e2e = d.keep("E2E" + std::to_string(i));
            payment(kind, Status::Matched, Pass::ExactKey, d.charges[c].txid, doc, d.charges[c].amount, due, e2e);
            payment(kind, Status::Duplicate, Pass::ExactKey, d.charges[c].txid, doc, d.charges[c].amount, due, e2e);
            break;
        }
        case Kind::Unidentified:
            payment(kind, Status::Unidentified, Pass::None, {}, doc, Money::fromCents(500 + static_cast<std::int64_t>(i % 400)), due);
            break;
        case Kind::Grouped: {
            std::int64_t total = 0;
            for (int k = 0; k < 3; ++k) {
                total += d.charges[charge(kind, Status::Matched, Pass::Grouped, doc, due + days{k})].amount.cents();
            }
            payment(kind, Status::Matched, Pass::Grouped, {}, doc, Money::fromCents(total), due);
            break;
        }
        case Kind::Split: {
            const auto c = charge(kind, Status::Matched, Pass::Grouped, doc, due);
            const std::int64_t half = d.charges[c].amount.cents() / 2;
            payment(kind, Status::Matched, Pass::Grouped, {}, doc, Money::fromCents(half), due - days{5});
            payment(kind, Status::Matched, Pass::Grouped, {}, doc, Money::fromCents(d.charges[c].amount.cents() - half), due - days{1});
            break;
        }
        case Kind::Pending:
            charge(kind, Status::Pending, Pass::None, doc, sys_days{d.reference} + days{10});
            break;
        case Kind::Overdue:
            charge(kind, Status::Overdue, Pass::None, doc, sys_days{d.reference} - days{20});
            break;
        case Kind::DuplicateKeyless: {
            const auto c = charge(kind, Status::Matched, Pass::Heuristic, doc, due);
            payment(kind, Status::Matched, Pass::Heuristic, {}, doc, d.charges[c].amount, due - days{2});
            payment(kind, Status::Duplicate, Pass::Heuristic, {}, doc, d.charges[c].amount, due - days{1});
            break;
        }
        case Kind::DuplicateBankId: {
            const auto c = charge(kind, Status::Matched, Pass::Heuristic, doc, due);
            const std::string_view fitid = d.keep("FITID" + std::to_string(i));
            payment(kind, Status::Matched, Pass::Heuristic, {}, doc, d.charges[c].amount, due, {}, fitid);
            payment(kind, Status::Duplicate, Pass::ExactKey, {}, doc, d.charges[c].amount, due, {}, fitid);
            break;
        }
        case Kind::WithoutPayer: {
            const auto c = charge(kind, Status::Matched, Pass::Approximate, doc, due);
            payment(kind, Status::Matched, Pass::Approximate, {}, {}, d.charges[c].amount, due);
            break;
        }
        }
    }
    return d;
}

}
