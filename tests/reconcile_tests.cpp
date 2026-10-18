#include <celerpay/reconcile.hpp>

#include "support/synthetic.hpp"

#include <catch2/catch_test_macros.hpp>

#include <deque>
#include <string>
#include <vector>

using namespace std;
using namespace std::chrono;
using namespace celerpay;
using namespace celerpay::reconcile;

namespace {

Options optionsFor(const synthetic::Dataset& d) {
    Options options;
    options.referenceDate = d.reference;
    return options;
}

}

TEST_CASE("gerador sintetico: cada anomalia plantada cai no status e passada esperados", "[reconcile]") {
    const auto calendar = rules::Calendar::brazil();
    const auto data = synthetic::generate(synthetic::kKinds * 40, calendar);
    const auto report = run(data.charges, data.payments, optionsFor(data), calendar);
    REQUIRE(report);

    for (size_t c = 0; c < data.charges.size(); ++c) {
        const Group& g = report->groups()[report->groupOfCharge(c)];
        INFO("cobrança " << c << " tipo " << static_cast<int>(data.chargeTruth[c].kind) << ": "
                         << explain(*report, report->groupOfCharge(c), data.charges, data.payments));
        REQUIRE(g.status == data.chargeTruth[c].status);
        REQUIRE(g.pass == data.chargeTruth[c].pass);
    }
    for (size_t p = 0; p < data.payments.size(); ++p) {
        const Group& g = report->groups()[report->groupOfPayment(p)];
        INFO("pagamento " << p << " tipo " << static_cast<int>(data.paymentTruth[p].kind) << ": "
                          << explain(*report, report->groupOfPayment(p), data.charges, data.payments));
        REQUIRE(g.status == data.paymentTruth[p].status);
        REQUIRE(g.pass == data.paymentTruth[p].pass);
    }
}

TEST_CASE("resultado deterministico", "[reconcile]") {
    const auto calendar = rules::Calendar::brazil();
    const auto data = synthetic::generate(synthetic::kKinds * 20, calendar);
    const auto a = run(data.charges, data.payments, optionsFor(data), calendar);
    const auto b = run(data.charges, data.payments, optionsFor(data), calendar);
    REQUIRE(a);
    REQUIRE(b);
    REQUIRE(a->groups().size() == b->groups().size());
    for (size_t g = 0; g < a->groups().size(); ++g) {
        REQUIRE(explain(*a, g, data.charges, data.payments) == explain(*b, g, data.charges, data.payments));
    }
}

TEST_CASE("explain e contagem por status", "[reconcile]") {
    Charge charge;
    charge.id = "C-1";
    charge.txid = "PEDIDO42";
    charge.amount = Money::fromCents(10000);
    charge.dueDate = 2025y / March / 20d;
    Payment payment;
    payment.id = "PEDIDO42";
    payment.amount = Money::fromCents(10000);
    payment.date = 2025y / March / 19d;
    Payment stray;
    stray.endToEndId = "E-XYZ";
    stray.amount = Money::fromCents(777);
    stray.date = 2025y / March / 19d;

    const vector<Charge> charges = {charge};
    const vector<Payment> payments = {payment, stray};
    Options options;
    options.referenceDate = 2025y / March / 31d;
    const auto report = run(charges, payments, options, rules::Calendar::brazil());
    REQUIRE(report);
    CHECK(report->count(Status::Matched) == 1);
    CHECK(report->count(Status::Unidentified) == 1);
    CHECK(explain(*report, report->groupOfCharge(0), charges, payments) ==
          "Matched (ExactKey, confiança 100): chave exata (txid ou nosso número); cobranças C-1; pagamentos PEDIDO42; esperado R$ 100,00; "
          "pago R$ 100,00");
    CHECK(explain(*report, report->groupOfPayment(1), charges, payments) ==
          "Unidentified (None, confiança 0): nenhuma cobrança candidata; pagamentos E-XYZ; pago R$ 7,77");
}

TEST_CASE("orcamento de nos limita a busca agrupada", "[reconcile]") {
    vector<Charge> charges;
    deque<string> ids;
    int64_t total = 0;
    for (int i = 0; i < 20; ++i) {
        Charge c;
        c.id = ids.emplace_back("C" + to_string(i));
        c.payerDocument = "52998224725";
        c.amount = Money::fromCents(1000 + i * 7);
        c.dueDate = 2025y / March / 20d;
        if (i >= 15) {
            total += c.amount.cents();
        }
        charges.push_back(c);
    }
    Payment p;
    p.payerDocument = "52998224725";
    p.amount = Money::fromCents(total);
    p.date = 2025y / March / 20d;
    const vector<Payment> payments = {p};

    Options options;
    options.referenceDate = 2025y / March / 31d;
    const auto found = run(charges, payments, options, rules::Calendar::brazil());
    REQUIRE(found);
    CHECK(found->groups()[found->groupOfPayment(0)].pass == Pass::Grouped);

    options.nodeBudget = 10;
    const auto limited = run(charges, payments, options, rules::Calendar::brazil());
    REQUIRE(limited);
    CHECK(limited->groups()[limited->groupOfPayment(0)].status == Status::Unidentified);
}

TEST_CASE("PaymentStore copia transacoes do extrato", "[reconcile]") {
    PaymentStore store;
    {
        string id = "NOSSO123";
        string doc = "52998224725";
        statement::Transaction t;
        t.id = id;
        t.payerDocument = doc;
        t.amount = Money::fromCents(500);
        t.date = 2025y / March / 20d;
        store.add(t);
        id.assign(id.size(), 'X');
        doc.assign(doc.size(), 'X');
    }
    REQUIRE(store.payments().size() == 1);
    CHECK(store.payments()[0].id == "NOSSO123");
    CHECK(store.payments()[0].payerDocument == "52998224725");
    CHECK(store.payments()[0].amount.cents() == 500);
}

TEST_CASE("PaymentStore guarda o FITID do OFX como bankId", "[reconcile]") {
    PaymentStore store;
    statement::Transaction t;
    t.source = statement::Source::Ofx;
    t.id = "FITID-1";
    t.amount = Money::fromCents(100);
    t.date = 2025y / March / 20d;
    store.add(t);
    CHECK(store.payments()[0].id.empty());
    CHECK(store.payments()[0].bankId == "FITID-1");

    t.source = statement::Source::PixJson;
    t.id = "TXID-1";
    store.add(t);
    CHECK(store.payments()[1].id == "TXID-1");
    CHECK(store.payments()[1].bankId.empty());
}

TEST_CASE("reconcile valida entradas", "[reconcile]") {
    Charge bad;
    bad.amount = Money::fromCents(-1);
    bad.dueDate = 2025y / March / 20d;
    Options options;
    options.referenceDate = 2025y / March / 31d;
    vector<Charge> valid = {Charge{}};
    valid[0].dueDate = 2025y / March / 20d;
    vector<Charge> invalid = valid;
    invalid.push_back(bad);
    const auto result = run(invalid, {}, options, rules::Calendar::brazil());
    REQUIRE_FALSE(result);
    CHECK(result.error().code == ErrorCode::InvalidValue);
    CHECK(result.error().offset == 1);

    options.referenceDate = 2025y / February / 30d;
    CHECK(run(valid, {}, options, rules::Calendar::brazil()).error().code == ErrorCode::InvalidValue);
}
