#include <celerpay/reconcile.hpp>
#include <celerpay/statement.hpp>

#include <chrono>
#include <cstdio>
#include <vector>

using namespace std;
using namespace std::chrono;
using namespace celerpay;

int main() {
    const string_view json = R"({"pix": [
        {"endToEndId": "E1", "txid": "PEDIDO42", "valor": "150.00", "horario": "2025-03-19T10:00:00Z"},
        {"endToEndId": "E2", "valor": "77.70", "horario": "2025-03-25T15:30:00Z", "pagador": {"cpf": "11144477735", "nome": "Ciclano"}}
    ]})";
    reconcile::PaymentStore store;
    const auto summary = statement::parsePixJson(json, [&](const statement::Transaction& t) { store.add(t); });
    if (!summary) {
        printf("erro no extrato: %s\n", string(summary.error().detail).c_str());
        return 1;
    }

    vector<reconcile::Charge> charges(2);
    charges[0].id = "C-1";
    charges[0].txid = "PEDIDO42";
    charges[0].amount = Money::fromCents(15000);
    charges[0].dueDate = 2025y / March / 20d;
    charges[1].id = "C-2";
    charges[1].amount = Money::fromCents(9900);
    charges[1].dueDate = 2025y / March / 10d;

    reconcile::Options options;
    options.referenceDate = 2025y / March / 31d;
    const auto calendar = rules::Calendar::brazil();
    const auto report = reconcile::run(charges, store.payments(), options, calendar);
    if (!report) {
        printf("erro: %s\n", string(report.error().detail).c_str());
        return 1;
    }
    for (size_t g = 0; g < report->groups().size(); ++g) {
        printf("%s\n", reconcile::explain(*report, g, charges, store.payments()).c_str());
    }
    return 0;
}
