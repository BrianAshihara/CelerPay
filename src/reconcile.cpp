#include <celerpay/reconcile.hpp>

#include "wide.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <tuple>
#include <unordered_map>

using namespace std;
using namespace std::chrono;

namespace celerpay::reconcile {

namespace {

using celerpay::detail::fail;

constexpr uint32_t kNone = numeric_limits<uint32_t>::max();
constexpr size_t kBlock = 1 << 20;

string_view stripZeros(string_view key) noexcept {
    const size_t first = key.find_first_not_of('0');
    return first == string_view::npos ? key.substr(key.empty() ? 0 : key.size() - 1) : key.substr(first);
}

bool hasRules(const Charge& c) noexcept {
    return c.interest.type != rules::InterestType::None || c.fine.type != rules::FineType::None ||
           c.discount.type != rules::DiscountType::None;
}

int64_t absDays(sys_days a, sys_days b) noexcept { return a > b ? (a - b).count() : (b - a).count(); }

struct Keyed {
    string_view key;
    sys_days date;
    uint32_t index;

    friend bool operator<(const Keyed& a, const Keyed& b) noexcept { return tie(a.key, a.date, a.index) < tie(b.key, b.date, b.index); }
};

struct Amounted {
    int64_t cents;
    sys_days date;
    uint32_t index;

    friend bool operator<(const Amounted& a, const Amounted& b) noexcept {
        return tie(a.cents, a.date, a.index) < tie(b.cents, b.date, b.index);
    }
};

class Engine {
public:
    Engine(span<const Charge> charges, span<const Payment> payments, const Options& options, const rules::Calendar& calendar)
        : charges_{charges}, payments_{payments}, options_{options}, calendar_{calendar},
          chargeGroup_(charges.size(), kNone), paymentGroup_(payments.size(), kNone) {}

    Result<void> prepare() {
        effectiveDue_.reserve(charges_.size());
        for (size_t i = 0; i < charges_.size(); ++i) {
            const auto due = rules::amountDue(terms(charges_[i]), charges_[i].dueDate, calendar_);
            if (!due) {
                return fail(ErrorCode::InvalidValue, i, "cobrança com regras inválidas");
            }
            effectiveDue_.push_back(sys_days{due->effectiveDueDate});
        }
        for (size_t i = 0; i < payments_.size(); ++i) {
            if (!payments_[i].date.ok()) {
                return fail(ErrorCode::InvalidValue, i, "pagamento com data inválida");
            }
        }
        return {};
    }

    void run() {
        duplicateEndToEnd();
        exactKey();
        byPayer(false);
        byPayer(true);
        withoutPayer();
        groupPaymentToCharges();
        groupChargeToPayments();
        leftovers();
    }

    vector<Group> groups;
    vector<vector<uint32_t>> groupCharges;
    vector<vector<uint32_t>> groupPayments;
    vector<uint32_t>& chargeGroup() { return chargeGroup_; }
    vector<uint32_t>& paymentGroup() { return paymentGroup_; }

private:
    static rules::ChargeTerms terms(const Charge& c) noexcept {
        rules::ChargeTerms t;
        t.principal = c.amount;
        t.dueDate = c.dueDate;
        t.interest = c.interest;
        t.fine = c.fine;
        t.discount = c.discount;
        return t;
    }

    Money expected(uint32_t c, year_month_day date) const {
        const Charge& charge = charges_[c];
        if (!hasRules(charge)) {
            return charge.amount;
        }
        const auto due = rules::amountDue(terms(charge), date, calendar_);
        return due ? due->total : charge.amount;
    }

    static Status statusFor(Money expectedAmount, Money paid) noexcept {
        if (paid == expectedAmount) {
            return Status::Matched;
        }
        return paid < expectedAmount ? Status::Partial : Status::Overpaid;
    }

    bool inWindow(uint32_t c, year_month_day date) const noexcept {
        const sys_days due{charges_[c].dueDate};
        const sys_days day{date};
        return day >= due - days{options_.daysBefore} && day <= due + days{options_.daysAfter};
    }

    void add(Status status, Pass pass, Reason reason, int confidence, Money expectedAmount, Money paid, vector<uint32_t> cs,
             vector<uint32_t> ps) {
        const auto id = static_cast<uint32_t>(groups.size());
        groups.push_back({status, pass, reason, static_cast<uint8_t>(clamp(confidence, 0, 100)), expectedAmount, paid});
        for (const uint32_t c : cs) {
            chargeGroup_[c] = id;
        }
        for (const uint32_t p : ps) {
            paymentGroup_[p] = id;
        }
        groupCharges.push_back(std::move(cs));
        groupPayments.push_back(std::move(ps));
    }

    void addSingle(Pass pass, Reason reason, int confidence, uint32_t c, uint32_t p) {
        const Money exp = expected(c, payments_[p].date);
        add(statusFor(exp, payments_[p].amount), pass, reason, confidence, exp, payments_[p].amount, {c}, {p});
    }

    bool chargeFree(uint32_t c) const noexcept { return chargeGroup_[c] == kNone; }
    bool paymentFree(uint32_t p) const noexcept { return paymentGroup_[p] == kNone; }

    void duplicateEndToEnd() {
        unordered_map<string_view, uint32_t> seenEndToEnd;
        unordered_map<string_view, uint32_t> seenBankId;
        seenEndToEnd.reserve(payments_.size());
        for (uint32_t p = 0; p < payments_.size(); ++p) {
            const string_view e2e = payments_[p].endToEndId;
            const string_view bankId = payments_[p].bankId;
            if (!e2e.empty() && !seenEndToEnd.emplace(e2e, p).second) {
                add(Status::Duplicate, Pass::ExactKey, Reason::DuplicateEndToEnd, 100, Money{}, payments_[p].amount, {}, {p});
            } else if (!bankId.empty() && !seenBankId.emplace(bankId, p).second) {
                add(Status::Duplicate, Pass::ExactKey, Reason::DuplicateBankId, 100, Money{}, payments_[p].amount, {}, {p});
            }
        }
    }

    void exactKey() {
        vector<Keyed> index;
        index.reserve(payments_.size());
        for (uint32_t p = 0; p < payments_.size(); ++p) {
            if (paymentFree(p) && !payments_[p].id.empty()) {
                index.push_back({stripZeros(payments_[p].id), sys_days{payments_[p].date}, p});
            }
        }
        sort(index.begin(), index.end());
        for (uint32_t c = 0; c < charges_.size(); ++c) {
            for (const string_view raw : {charges_[c].txid, charges_[c].ourNumber}) {
                if (raw.empty()) {
                    continue;
                }
                const string_view key = stripZeros(raw);
                auto it = lower_bound(index.begin(), index.end(), Keyed{key, sys_days::min(), 0});
                bool matched = false;
                uint32_t firstGroup = kNone;
                for (; it != index.end() && it->key == key; ++it) {
                    if (!paymentFree(it->index)) {
                        continue;
                    }
                    if (!matched) {
                        firstGroup = static_cast<uint32_t>(groups.size());
                        addSingle(Pass::ExactKey, Reason::Key, 100, c, it->index);
                        matched = true;
                    } else {
                        add(Status::Duplicate, Pass::ExactKey, Reason::DuplicateKey, 100, Money{}, payments_[it->index].amount, {c},
                            {it->index});
                        chargeGroup_[c] = firstGroup;
                    }
                }
                if (matched) {
                    break;
                }
            }
        }
    }

    vector<Keyed> chargesByPayer() const {
        vector<Keyed> index;
        for (uint32_t c = 0; c < charges_.size(); ++c) {
            if (chargeFree(c) && !charges_[c].payerDocument.empty()) {
                index.push_back({charges_[c].payerDocument, sys_days{charges_[c].dueDate}, c});
            }
        }
        sort(index.begin(), index.end());
        return index;
    }

    template <class Visit>
    void forCandidates(const vector<Keyed>& index, string_view key, year_month_day date, Visit&& visit) const {
        const sys_days day{date};
        const auto first = lower_bound(index.begin(), index.end(), Keyed{key, day - days{options_.daysAfter}, 0});
        for (auto it = first; it != index.end() && it->key == key && it->date <= day + days{options_.daysBefore}; ++it) {
            if (chargeFree(it->index)) {
                visit(it->index);
            }
        }
    }

    vector<Keyed> allChargesByPayer() const {
        vector<Keyed> index;
        for (uint32_t c = 0; c < charges_.size(); ++c) {
            if (!charges_[c].payerDocument.empty()) {
                index.push_back({charges_[c].payerDocument, sys_days{charges_[c].dueDate}, c});
            }
        }
        sort(index.begin(), index.end());
        return index;
    }

    uint32_t alreadyPaid(const vector<Keyed>& all, const Payment& payment) const {
        const sys_days day{payment.date};
        const auto first = lower_bound(all.begin(), all.end(), Keyed{payment.payerDocument, day - days{options_.daysAfter}, 0});
        for (auto it = first; it != all.end() && it->key == payment.payerDocument && it->date <= day + days{options_.daysBefore}; ++it) {
            const uint32_t g = chargeGroup_[it->index];
            if (g != kNone && groups[g].status == Status::Matched && groupCharges[g].size() == 1 &&
                expected(it->index, payment.date) == payment.amount) {
                return it->index;
            }
        }
        return kNone;
    }

    void byPayer(bool approximate) {
        const auto index = chargesByPayer();
        const auto all = approximate ? vector<Keyed>{} : allChargesByPayer();
        for (uint32_t p = 0; p < payments_.size(); ++p) {
            const Payment& payment = payments_[p];
            if (!paymentFree(p) || payment.payerDocument.empty()) {
                continue;
            }
            uint32_t best = kNone;
            int64_t bestDiff = 0;
            int64_t bestDistance = 0;
            Money bestExpected;
            forCandidates(index, payment.payerDocument, payment.date, [&](uint32_t c) {
                const Money exp = expected(c, payment.date);
                const int64_t diff = payment.amount.cents() > exp.cents() ? payment.amount.cents() - exp.cents()
                                                                          : exp.cents() - payment.amount.cents();
                if (approximate ? diff > limit(exp) : diff != 0) {
                    return;
                }
                const int64_t distance = absDays(sys_days{charges_[c].dueDate}, sys_days{payment.date});
                if (best == kNone || tie(diff, distance, c) < tie(bestDiff, bestDistance, best)) {
                    best = c;
                    bestDiff = diff;
                    bestDistance = distance;
                    bestExpected = exp;
                }
            });
            if (best == kNone) {
                if (!approximate) {
                    if (const uint32_t paid = alreadyPaid(all, payment); paid != kNone) {
                        const uint32_t original = chargeGroup_[paid];
                        add(Status::Duplicate, Pass::Heuristic, Reason::DuplicatePayment, 80, Money{}, payment.amount, {paid}, {p});
                        chargeGroup_[paid] = original;
                    }
                }
                continue;
            }
            if (!approximate) {
                const bool plain = bestExpected == charges_[best].amount;
                addSingle(Pass::Heuristic, plain ? Reason::PayerAmountDate : Reason::ExplainedByRules, plain ? 90 : 85, best, p);
            } else {
                const int64_t allowed = max<int64_t>(limit(bestExpected), 1);
                addSingle(Pass::Approximate, Reason::WithinTolerance, static_cast<int>(80 - 30 * bestDiff / allowed), best, p);
            }
        }
    }

    int64_t limit(Money exp) const noexcept {
        if (exp.cents() <= 0) {
            return 0;
        }
        const auto value = celerpay::detail::mulDivRound(static_cast<uint64_t>(exp.cents()), static_cast<uint64_t>(options_.tolerance.scaled()),
                                                         1, static_cast<uint64_t>(rules::Rate::kScale));
        return value && *value <= static_cast<uint64_t>(numeric_limits<int64_t>::max()) ? static_cast<int64_t>(*value) : 0;
    }

    void withoutPayer() {
        vector<Amounted> index;
        for (uint32_t c = 0; c < charges_.size(); ++c) {
            if (chargeFree(c)) {
                index.push_back({charges_[c].amount.cents(), sys_days{charges_[c].dueDate}, c});
            }
        }
        sort(index.begin(), index.end());
        for (uint32_t p = 0; p < payments_.size(); ++p) {
            const Payment& payment = payments_[p];
            if (!paymentFree(p) || !payment.payerDocument.empty()) {
                continue;
            }
            const sys_days day{payment.date};
            uint32_t found = kNone;
            int candidates = 0;
            auto it = lower_bound(index.begin(), index.end(), Amounted{payment.amount.cents(), day - days{options_.daysAfter}, 0});
            for (; it != index.end() && it->cents == payment.amount.cents() && it->date <= day + days{options_.daysBefore}; ++it) {
                if (chargeFree(it->index) && expected(it->index, payment.date) == payment.amount) {
                    found = it->index;
                    ++candidates;
                }
            }
            if (candidates == 1) {
                addSingle(Pass::Approximate, Reason::AmountDate, 60, found, p);
            }
        }
    }

    struct Search {
        vector<int64_t> values;
        int64_t target = 0;
        size_t maxSize = 0;
        uint64_t budget = 0;
        vector<size_t> chosen;
        vector<size_t> best;

        bool run(size_t start, int64_t sum) {
            if (budget == 0) {
                return false;
            }
            --budget;
            if (sum == target && chosen.size() >= 2) {
                best = chosen;
                return true;
            }
            if (chosen.size() == maxSize) {
                return false;
            }
            for (size_t i = start; i < values.size(); ++i) {
                if (sum + values[i] > target) {
                    continue;
                }
                chosen.push_back(i);
                if (run(i + 1, sum + values[i])) {
                    return true;
                }
                chosen.pop_back();
            }
            return false;
        }
    };

    template <class T>
    void closest(vector<T>& items, size_t keep) const {
        if (items.size() > keep) {
            items.resize(keep);
        }
        sort(items.begin(), items.end(), [](const T& a, const T& b) { return a.second < b.second; });
    }

    void groupPaymentToCharges() {
        const auto index = chargesByPayer();
        for (uint32_t p = 0; p < payments_.size(); ++p) {
            const Payment& payment = payments_[p];
            if (!paymentFree(p) || payment.payerDocument.empty() || payment.amount.cents() <= 0) {
                continue;
            }
            vector<pair<int64_t, uint32_t>> candidates;
            forCandidates(index, payment.payerDocument, payment.date, [&](uint32_t c) {
                candidates.push_back({absDays(sys_days{charges_[c].dueDate}, sys_days{payment.date}), c});
            });
            if (candidates.size() < 2) {
                continue;
            }
            stable_sort(candidates.begin(), candidates.end());
            closest(candidates, options_.maxGroupCandidates);
            Search search;
            search.target = payment.amount.cents();
            search.maxSize = options_.maxGroupSize;
            search.budget = options_.nodeBudget;
            for (const auto& [distance, c] : candidates) {
                search.values.push_back(expected(c, payment.date).cents());
            }
            if (!search.run(0, 0)) {
                continue;
            }
            vector<uint32_t> cs;
            for (const size_t i : search.best) {
                cs.push_back(candidates[i].second);
            }
            add(Status::Matched, Pass::Grouped, Reason::SubsetSum, 70, payment.amount, payment.amount, std::move(cs), {p});
        }
    }

    void groupChargeToPayments() {
        vector<Keyed> index;
        for (uint32_t p = 0; p < payments_.size(); ++p) {
            if (paymentFree(p) && !payments_[p].payerDocument.empty() && payments_[p].amount.cents() > 0) {
                index.push_back({payments_[p].payerDocument, sys_days{payments_[p].date}, p});
            }
        }
        sort(index.begin(), index.end());
        for (uint32_t c = 0; c < charges_.size(); ++c) {
            const Charge& charge = charges_[c];
            if (!chargeFree(c) || charge.payerDocument.empty()) {
                continue;
            }
            const sys_days due{charge.dueDate};
            vector<pair<int64_t, uint32_t>> candidates;
            const auto first = lower_bound(index.begin(), index.end(), Keyed{charge.payerDocument, due - days{options_.daysBefore}, 0});
            for (auto it = first; it != index.end() && it->key == charge.payerDocument && it->date <= due + days{options_.daysAfter};
                 ++it) {
                if (paymentFree(it->index)) {
                    candidates.push_back({absDays(it->date, due), it->index});
                }
            }
            if (candidates.size() < 2) {
                continue;
            }
            stable_sort(candidates.begin(), candidates.end());
            closest(candidates, options_.maxGroupCandidates);
            year_month_day latest = payments_[candidates[0].second].date;
            for (const auto& [distance, p] : candidates) {
                latest = max(latest, payments_[p].date);
            }
            const Money target = expected(c, latest);
            Search search;
            search.target = target.cents();
            search.maxSize = options_.maxGroupSize;
            search.budget = options_.nodeBudget;
            for (const auto& [distance, p] : candidates) {
                search.values.push_back(payments_[p].amount.cents());
            }
            if (!search.run(0, 0)) {
                continue;
            }
            vector<uint32_t> ps;
            for (const size_t i : search.best) {
                ps.push_back(candidates[i].second);
            }
            add(Status::Matched, Pass::Grouped, Reason::SubsetSum, 70, target, target, {c}, std::move(ps));
        }
    }

    void leftovers() {
        for (uint32_t p = 0; p < payments_.size(); ++p) {
            if (paymentFree(p)) {
                add(Status::Unidentified, Pass::None, Reason::NoCandidate, 0, Money{}, payments_[p].amount, {}, {p});
            }
        }
        for (uint32_t c = 0; c < charges_.size(); ++c) {
            if (chargeFree(c)) {
                const bool overdue = sys_days{options_.referenceDate} > effectiveDue_[c];
                add(overdue ? Status::Overdue : Status::Pending, Pass::None, overdue ? Reason::PastDue : Reason::NotDueYet, 0,
                    expected(c, options_.referenceDate), Money{}, {c}, {});
            }
        }
    }

    span<const Charge> charges_;
    span<const Payment> payments_;
    const Options& options_;
    const rules::Calendar& calendar_;
    vector<uint32_t> chargeGroup_;
    vector<uint32_t> paymentGroup_;
    vector<sys_days> effectiveDue_;
};

string_view describe(Reason reason) noexcept {
    switch (reason) {
    case Reason::Key:
        return "chave exata (txid ou nosso número)";
    case Reason::DuplicateKey:
        return "pagamento repetido da mesma chave";
    case Reason::DuplicateEndToEnd:
        return "endToEndId repetido";
    case Reason::DuplicateBankId:
        return "identificador bancário (FITID) repetido";
    case Reason::DuplicatePayment:
        return "mesmo pagador e valor de uma cobrança já paga";
    case Reason::PayerAmountDate:
        return "mesmo pagador, valor e janela de datas";
    case Reason::ExplainedByRules:
        return "mesmo pagador, valor explicado por juros, multa ou desconto";
    case Reason::WithinTolerance:
        return "mesmo pagador, diferença dentro da tolerância";
    case Reason::AmountDate:
        return "valor e data, candidata única";
    case Reason::SubsetSum:
        return "soma de um grupo de itens do mesmo pagador";
    case Reason::NoCandidate:
        return "nenhuma cobrança candidata";
    case Reason::NotDueYet:
        return "ainda no prazo";
    case Reason::PastDue:
        return "vencida sem pagamento";
    }
    return "";
}

string join(span<const uint32_t> items, auto&& name) {
    string out;
    for (const uint32_t i : items) {
        out += (out.empty() ? "" : ", ") + string(name(i));
    }
    return out;
}

}

struct ReportBuilder {
    static Report build(Engine& engine, size_t chargeCount, size_t paymentCount) {
        Report report;
        report.groups_ = std::move(engine.groups);
        report.chargeGroup_ = std::move(engine.chargeGroup());
        report.paymentGroup_ = std::move(engine.paymentGroup());
        report.groupCharges_.reserve(chargeCount + 16);
        report.groupPayments_.reserve(paymentCount + 16);
        for (size_t g = 0; g < report.groups_.size(); ++g) {
            report.chargeBegin_.push_back(static_cast<uint32_t>(report.groupCharges_.size()));
            report.paymentBegin_.push_back(static_cast<uint32_t>(report.groupPayments_.size()));
            report.groupCharges_.insert(report.groupCharges_.end(), engine.groupCharges[g].begin(), engine.groupCharges[g].end());
            report.groupPayments_.insert(report.groupPayments_.end(), engine.groupPayments[g].begin(), engine.groupPayments[g].end());
        }
        report.chargeBegin_.push_back(static_cast<uint32_t>(report.groupCharges_.size()));
        report.paymentBegin_.push_back(static_cast<uint32_t>(report.groupPayments_.size()));
        return report;
    }
};

span<const uint32_t> Report::charges(size_t group) const noexcept {
    return span<const uint32_t>(groupCharges_).subspan(chargeBegin_[group], chargeBegin_[group + 1] - chargeBegin_[group]);
}

span<const uint32_t> Report::payments(size_t group) const noexcept {
    return span<const uint32_t>(groupPayments_).subspan(paymentBegin_[group], paymentBegin_[group + 1] - paymentBegin_[group]);
}

size_t Report::count(Status status) const noexcept {
    return static_cast<size_t>(count_if(groups_.begin(), groups_.end(), [&](const Group& g) { return g.status == status; }));
}

void PaymentStore::add(const statement::Transaction& t) {
    const bool bankOnly = t.source == statement::Source::Ofx;
    payments_.push_back({bankOnly ? string_view{} : keep(t.id), keep(t.endToEndId), bankOnly ? keep(t.id) : string_view{},
                         keep(t.reference), keep(t.payerDocument), t.amount, t.date});
}

string_view PaymentStore::keep(string_view text) {
    if (text.empty()) {
        return {};
    }
    if (text.size() > capacity_ - used_) {
        const size_t size = max(kBlock, text.size());
        blocks_.push_back(make_unique<char[]>(size));
        used_ = 0;
        capacity_ = size;
    }
    char* target = blocks_.back().get() + used_;
    memcpy(target, text.data(), text.size());
    used_ += text.size();
    return {target, text.size()};
}

Result<Report> run(span<const Charge> charges, span<const Payment> payments, const Options& options,
                         const rules::Calendar& calendar) {
    if (!options.referenceDate.ok() || options.daysBefore < 0 || options.daysAfter < 0 || options.maxGroupSize < 2 ||
        options.tolerance.scaled() < 0) {
        return fail(ErrorCode::InvalidValue, 0, "opções de conciliação inválidas");
    }
    if (charges.size() >= kNone || payments.size() >= kNone) {
        return fail(ErrorCode::ValueTooLong, 0, "itens demais para conciliar");
    }
    Engine engine(charges, payments, options, calendar);
    if (const auto ready = engine.prepare(); !ready) {
        return unexpected(ready.error());
    }
    engine.run();
    return ReportBuilder::build(engine, charges.size(), payments.size());
}

string explain(const Report& report, size_t group, span<const Charge> charges, span<const Payment> payments) {
    const Group& g = report.groups()[group];
    string out = string(toString(g.status)) + " (" + string(toString(g.pass)) + ", confiança " + to_string(g.confidence) + "): " +
                 string(describe(g.reason));
    const auto cs = report.charges(group);
    const auto ps = report.payments(group);
    if (!cs.empty()) {
        out += "; cobranças " + join(cs, [&](uint32_t i) { return charges[i].id; });
    }
    if (!ps.empty()) {
        out += "; pagamentos " + join(ps, [&](uint32_t i) { return !payments[i].id.empty() ? payments[i].id : !payments[i].endToEndId.empty() ? payments[i].endToEndId : payments[i].bankId; });
    }
    if (g.status != Status::Unidentified && g.status != Status::Duplicate) {
        out += "; esperado " + formatBrl(g.expected);
    }
    out += "; pago " + formatBrl(g.paid);
    return out;
}

string_view toString(Status status) noexcept {
    switch (status) {
    case Status::Matched:
        return "Matched";
    case Status::Partial:
        return "Partial";
    case Status::Overpaid:
        return "Overpaid";
    case Status::Duplicate:
        return "Duplicate";
    case Status::Unidentified:
        return "Unidentified";
    case Status::Pending:
        return "Pending";
    case Status::Overdue:
        return "Overdue";
    }
    return "";
}

string_view toString(Pass pass) noexcept {
    switch (pass) {
    case Pass::ExactKey:
        return "ExactKey";
    case Pass::Heuristic:
        return "Heuristic";
    case Pass::Approximate:
        return "Approximate";
    case Pass::Grouped:
        return "Grouped";
    case Pass::None:
        return "None";
    }
    return "";
}

string_view toString(Reason reason) noexcept {
    switch (reason) {
    case Reason::Key:
        return "Key";
    case Reason::DuplicateKey:
        return "DuplicateKey";
    case Reason::DuplicateEndToEnd:
        return "DuplicateEndToEnd";
    case Reason::DuplicateBankId:
        return "DuplicateBankId";
    case Reason::DuplicatePayment:
        return "DuplicatePayment";
    case Reason::PayerAmountDate:
        return "PayerAmountDate";
    case Reason::ExplainedByRules:
        return "ExplainedByRules";
    case Reason::WithinTolerance:
        return "WithinTolerance";
    case Reason::AmountDate:
        return "AmountDate";
    case Reason::SubsetSum:
        return "SubsetSum";
    case Reason::NoCandidate:
        return "NoCandidate";
    case Reason::NotDueYet:
        return "NotDueYet";
    case Reason::PastDue:
        return "PastDue";
    }
    return "";
}

}
