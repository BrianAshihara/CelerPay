#include <celerpay/statement.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace std;
using namespace std::chrono;
using namespace celerpay;
using namespace celerpay::statement;

namespace {

struct Owned {
    Source source;
    int64_t amount;
    year_month_day date;
    optional<year_month_day> dueDate;
    string id;
    string reference;
    string endToEndId;
    string payerDocument;
    string payerName;
    string description;
    string movement;
    int64_t nominal;
    int64_t interest;
    int64_t discount;
    int64_t fee;
    uint64_t position;
};

struct Collector {
    vector<Owned> items;

    Sink sink() {
        return [this](const Transaction& t) {
            items.push_back({t.source, t.amount.cents(), t.date, t.dueDate, string(t.id), string(t.reference), string(t.endToEndId),
                             string(t.payerDocument), string(t.payerName), string(t.description), string(t.movement),
                             t.nominal.cents(), t.interest.cents(), t.discount.cents(), t.fee.cents(), t.position});
        };
    }
};

class Record {
public:
    explicit Record(char type) { put(8, string(1, type)); }

    Record& put(size_t start, string_view value) {
        text_.replace(start - 1, value.size(), value);
        return *this;
    }

    Record& number(size_t start, size_t length, int64_t value) {
        string digits = to_string(value);
        return put(start, string(length - digits.size(), '0') + digits);
    }

    const string& str() const { return text_; }

private:
    string text_ = string(240, ' ');
};

string segmentT(string_view ourNumber, string_view document, int64_t nominal, string_view payerType, string_view payerDocument,
                string_view payerName, int64_t fee) {
    Record r('3');
    r.put(1, "001").put(14, "T").put(16, "06").put(38, ourNumber).put(59, document).put(74, "20032025");
    r.number(82, 15, nominal).put(133, payerType).put(134, payerDocument).put(149, payerName).number(199, 15, fee);
    return r.str();
}

string segmentU(int64_t paid, int64_t interest, int64_t discount, string_view occurrence, string_view credit) {
    Record r('3');
    r.put(1, "001").put(14, "U").put(16, "06").number(18, 15, interest).number(33, 15, discount).number(48, 15, 0);
    r.number(78, 15, paid).put(138, occurrence).put(146, credit);
    return r.str();
}

string cnabFile(const vector<string>& details, const string& newline = "\r\n") {
    vector<string> lines = {Record('0').put(1, "001").str(), Record('1').put(1, "001").str()};
    lines.insert(lines.end(), details.begin(), details.end());
    lines.push_back(Record('5').put(1, "001").str());
    Record trailer('9');
    trailer.put(1, "001").number(24, 6, static_cast<int64_t>(lines.size() + 1));
    lines.push_back(trailer.str());
    string out;
    for (const string& line : lines) {
        out += line + newline;
    }
    return out;
}

string sampleCnab() {
    return cnabFile({
        segmentT("00000000000000012345", "NF-1001", 100000, "1", "000052998224725", "FULANO DE TAL", 250),
        segmentU(100500, 1000, 500, "21032025", "24032025"),
        segmentT("00000000000000012346", "NF-1002", 5000, "2", "011222333000181", "EMPRESA EXEMPLO LTDA", 0),
        segmentU(5000, 0, 0, "21032025", "00000000"),
    });
}

void requireError(const Result<Summary>& result, ErrorCode code, uint64_t offset) {
    REQUIRE_FALSE(result);
    CHECK(result.error().code == code);
    CHECK(result.error().offset == offset);
}

}

TEST_CASE("CNAB 240 FEBRABAN", "[statement][cnab]") {
    Collector c;
    const string file = sampleCnab();
    const auto summary = parseCnab240(file, cnab240::febraban(), c.sink());
    REQUIRE(summary);
    CHECK(summary->records == 8);
    CHECK(summary->transactions == 2);
    CHECK(summary->total.cents() == 105500);
    REQUIRE(c.items.size() == 2);

    const Owned& a = c.items[0];
    CHECK(a.source == Source::Cnab240);
    CHECK(a.amount == 100500);
    CHECK(a.date == 2025y / March / 24d);
    CHECK(a.dueDate == 2025y / March / 20d);
    CHECK(a.id == "00000000000000012345");
    CHECK(a.reference == "NF-1001");
    CHECK(a.payerDocument == "52998224725");
    CHECK(a.payerName == "FULANO DE TAL");
    CHECK(a.movement == "06");
    CHECK(a.nominal == 100000);
    CHECK(a.interest == 1000);
    CHECK(a.discount == 500);
    CHECK(a.fee == 250);
    CHECK(a.position == 3);

    const Owned& b = c.items[1];
    CHECK(b.payerDocument == "11222333000181");
    CHECK(b.date == 2025y / March / 21d);
    CHECK(b.position == 5);
}

TEST_CASE("CNAB 240 por stream igual ao da memoria", "[statement][cnab]") {
    Collector memory;
    Collector stream;
    const string file = sampleCnab();
    REQUIRE(parseCnab240(file, cnab240::febraban(), memory.sink()));
    istringstream input(file);
    const auto summary = parseCnab240(input, cnab240::febraban(), stream.sink());
    REQUIRE(summary);
    CHECK(summary->transactions == 2);
    REQUIRE(stream.items.size() == memory.items.size());
    for (size_t i = 0; i < memory.items.size(); ++i) {
        CHECK(stream.items[i].id == memory.items[i].id);
        CHECK(stream.items[i].payerName == memory.items[i].payerName);
        CHECK(stream.items[i].amount == memory.items[i].amount);
    }

    Collector lf;
    CHECK(parseCnab240(cnabFile({}, "\n"), cnab240::febraban(), lf.sink())->transactions == 0);
}

TEST_CASE("CNAB 240 recusa arquivos quebrados", "[statement][cnab]") {
    Collector c;
    const auto& layout = cnab240::febraban();
    const string t = segmentT("1", "D", 100, "1", "000052998224725", "X", 0);
    const string u = segmentU(100, 0, 0, "21032025", "24032025");

    requireError(parseCnab240(cnabFile({t}), layout, c.sink()), ErrorCode::MissingField, 4);
    requireError(parseCnab240(cnabFile({u}), layout, c.sink()), ErrorCode::MissingField, 3);
    requireError(parseCnab240(cnabFile({t, t, u}), layout, c.sink()), ErrorCode::MissingField, 4);
    requireError(parseCnab240(cnabFile({t.substr(0, 239), u}), layout, c.sink()), ErrorCode::InvalidLength, 3);
    requireError(parseCnab240(cnabFile({t, segmentU(100, 0, 0, "32032025", "24032025")}), layout, c.sink()), ErrorCode::InvalidValue, 4);
    requireError(parseCnab240(cnabFile({t, segmentU(100, 0, 0, "00000000", "00000000")}), layout, c.sink()), ErrorCode::MissingField, 4);

    string badAmount = u;
    badAmount[80] = 'X';
    requireError(parseCnab240(cnabFile({t, badAmount}), layout, c.sink()), ErrorCode::InvalidCharacter, 4);

    string file = cnabFile({t, u});
    requireError(parseCnab240(file.substr(0, file.size() - 242), layout, c.sink()), ErrorCode::MissingField, 5);
    requireError(parseCnab240(file + Record('3').str(), layout, c.sink()), ErrorCode::InvalidValue, 7);

    string wrongCount = file;
    wrongCount.replace(242 * 5 + 23, 6, "000099");
    requireError(parseCnab240(wrongCount, layout, c.sink()), ErrorCode::InvalidValue, 6);

    string unknown = file;
    unknown[242 * 1 + 7] = '7';
    requireError(parseCnab240(unknown, layout, c.sink()), ErrorCode::InvalidValue, 2);
}

TEST_CASE("OFX 1.x SGML", "[statement][ofx]") {
    const string ofx = "OFXHEADER:100\r\nDATA:OFXSGML\r\nVERSION:102\r\n\r\n<OFX>\r\n<BANKMSGSRSV1><STMTTRNRS><STMTRS><BANKTRANLIST>\r\n"
                       "<STMTTRN>\r\n<TRNTYPE>CREDIT\r\n<DTPOSTED>20250324120000[-3:BRT]\r\n<TRNAMT>1005,00\r\n<FITID>202503240001\r\n"
                       "<CHECKNUM>778899\r\n<MEMO>PIX RECEBIDO FULANO\r\n</STMTTRN>\r\n"
                       "<STMTTRN>\r\n<TRNTYPE>DEBIT\r\n<DTPOSTED>20250325\r\n<TRNAMT>-12.5\r\n<FITID>202503250002\r\n<NAME>TARIFA\r\n</STMTTRN>\r\n"
                       "</BANKTRANLIST></STMTRS></STMTTRNRS></BANKMSGSRSV1></OFX>\r\n";
    Collector c;
    const auto summary = parseOfx(ofx, c.sink());
    REQUIRE(summary);
    CHECK(summary->transactions == 2);
    CHECK(summary->total.cents() == 99250);
    REQUIRE(c.items.size() == 2);
    CHECK(c.items[0].source == Source::Ofx);
    CHECK(c.items[0].amount == 100500);
    CHECK(c.items[0].date == 2025y / March / 24d);
    CHECK(c.items[0].id == "202503240001");
    CHECK(c.items[0].reference == "778899");
    CHECK(c.items[0].description == "PIX RECEBIDO FULANO");
    CHECK(c.items[0].movement == "CREDIT");
    CHECK(c.items[0].position == ofx.find("<STMTTRN>"));
    CHECK(c.items[1].amount == -1250);
    CHECK(c.items[1].payerName == "TARIFA");
}

TEST_CASE("OFX 2.x XML", "[statement][ofx]") {
    const string ofx = R"(<?xml version="1.0" encoding="UTF-8"?>
<?OFX OFXHEADER="200" VERSION="211" SECURITY="NONE"?>
<OFX><BANKMSGSRSV1><STMTTRNRS><STMTRS><BANKTRANLIST>
<STMTTRN><TRNTYPE>CREDIT</TRNTYPE><DTPOSTED>20251231</DTPOSTED><TRNAMT>+250.75</TRNAMT><FITID>X1</FITID><REFNUM>NF-9</REFNUM><NAME>CLIENTE</NAME></STMTTRN>
</BANKTRANLIST></STMTRS></STMTTRNRS></BANKMSGSRSV1></OFX>)";
    Collector c;
    const auto summary = parseOfx(ofx, c.sink());
    REQUIRE(summary);
    REQUIRE(c.items.size() == 1);
    CHECK(c.items[0].amount == 25075);
    CHECK(c.items[0].date == 2025y / December / 31d);
    CHECK(c.items[0].reference == "NF-9");
    CHECK(c.items[0].payerName == "CLIENTE");
}

TEST_CASE("OFX recusa transacoes invalidas", "[statement][ofx]") {
    Collector c;
    CHECK(parseOfx("sem ofx", c.sink()).error().code == ErrorCode::InvalidValue);
    CHECK(parseOfx("<OFX><STMTTRN><TRNAMT>1", c.sink()).error().code == ErrorCode::UnexpectedEnd);
    const string missing = "<OFX><STMTTRN><TRNAMT>1.00<FITID>1</STMTTRN></OFX>";
    requireError(parseOfx(missing, c.sink()), ErrorCode::MissingField, 5);
    const string badDate = "<OFX><STMTTRN><DTPOSTED>20251301<TRNAMT>1.00<FITID>1</STMTTRN></OFX>";
    requireError(parseOfx(badDate, c.sink()), ErrorCode::InvalidValue, static_cast<uint32_t>(badDate.find("20251301")));
    const string badAmount = "<OFX><STMTTRN><DTPOSTED>20251201<TRNAMT>1.0x<FITID>1</STMTTRN></OFX>";
    requireError(parseOfx(badAmount, c.sink()), ErrorCode::InvalidCharacter, static_cast<uint32_t>(badAmount.find("1.0x") + 3));
}

TEST_CASE("JSON de PIX recebidos da API Pix", "[statement][pix]") {
    const string_view raw = R"({
  "parametros": {"inicio": "2025-03-01T00:00:00Z", "fim": "2025-03-31T23:59:59Z", "paginacao": {"paginaAtual": 0, "itensPorPagina": 100}},
  "pix": [
    {"endToEndId": "E12345678202503241200abcdef12345", "txid": "PEDIDO42", "valor": "110.00", "horario": "2025-03-24T12:00:00.000Z",
     "pagador": {"cpf": "52998224725", "nome": "Jo@@o da Silva"}, "infoPagador": "pedido 42", "devolucoes": [], "chave": "a@b.com"},
    {"endToEndId": "E98765432202503251300fedcba54321", "valor": "5.5", "horario": "2025-03-25T13:00:00-03:00",
     "pagador": {"cnpj": "11222333000181", "nome": "Empresa"}, "componentesValor": {"original": {"valor": 5.5e0}}, "ok": true, "x": null}
  ]
})";
    const string json = string(raw).replace(string(raw).find("@@"), 2, "\\u00e3");
    Collector c;
    const auto summary = parsePixJson(json, c.sink());
    REQUIRE(summary);
    CHECK(summary->transactions == 2);
    CHECK(summary->total.cents() == 11550);
    REQUIRE(c.items.size() == 2);
    CHECK(c.items[0].source == Source::PixJson);
    CHECK(c.items[0].endToEndId == "E12345678202503241200abcdef12345");
    CHECK(c.items[0].id == "PEDIDO42");
    CHECK(c.items[0].amount == 11000);
    CHECK(c.items[0].date == 2025y / March / 24d);
    CHECK(c.items[0].payerDocument == "52998224725");
    CHECK(c.items[0].payerName == "Jo\xC3\xA3o da Silva");
    CHECK(c.items[0].reference == "pedido 42");
    CHECK(c.items[1].payerDocument == "11222333000181");
    CHECK(c.items[1].amount == 550);
    CHECK(c.items[1].id.empty());

    Collector empty;
    CHECK(parsePixJson(R"({"pix": []})", empty.sink())->transactions == 0);
}

TEST_CASE("JSON decodifica escapes e pares substitutos", "[statement][pix]") {
    const string json = "{\"pix\": [{\"endToEndId\": \"E1\", \"valor\": \"1.00\", \"horario\": \"2025-01-01\", "
                        "\"pagador\": {\"nome\": \"A\\\"B\\\\C\\/D\\nE \\ud83d\\ude00 \\ud800x\"}}]}";
    Collector c;
    REQUIRE(parsePixJson(json, c.sink()));
    REQUIRE(c.items.size() == 1);
    CHECK(c.items[0].payerName == "A\"B\\C/D\nE \xF0\x9F\x98\x80 \xEF\xBF\xBDx");
}

TEST_CASE("OFX decodifica entidades e Windows-1252", "[statement][ofx]") {
    const string ofx = "OFXHEADER:100\r\nENCODING:USASCII\r\nCHARSET:1252\r\n\r\n<OFX><STMTTRN><DTPOSTED>20250101<TRNAMT>1.00<FITID>A&amp;B"
                       "<NAME>Jo\xE3o &lt;&#233;&#xE9;&gt; \x80<MEMO>R&D &unknown;</STMTTRN></OFX>";
    Collector c;
    REQUIRE(parseOfx(ofx, c.sink()));
    REQUIRE(c.items.size() == 1);
    CHECK(c.items[0].id == "A&B");
    CHECK(c.items[0].payerName == "Jo\xC3\xA3o <\xC3\xA9\xC3\xA9> \xE2\x82\xAC");
    CHECK(c.items[0].description == "R&D &unknown;");

    const string utf8 = "<OFX><STMTTRN><DTPOSTED>20250101<TRNAMT>1.00<FITID>1<NAME>Jo\xC3\xA3o</STMTTRN></OFX>";
    Collector u;
    REQUIRE(parseOfx(utf8, u.sink()));
    CHECK(u.items[0].payerName == "Jo\xC3\xA3o");
}

TEST_CASE("JSON de PIX recusa entradas invalidas", "[statement][pix]") {
    Collector c;
    CHECK(parsePixJson("", c.sink()).error().code == ErrorCode::UnexpectedEnd);
    CHECK(parsePixJson("{}", c.sink()).error().code == ErrorCode::MissingField);
    CHECK(parsePixJson(R"({"pix": [{"valor": "1.00", "horario": "2025-01-01"}]})", c.sink()).error().code == ErrorCode::MissingField);
    CHECK(parsePixJson(R"({"pix": [{"endToEndId": "E1", "valor": "1,00", "horario": "2025-01-01"}]})", c.sink()).error().code ==
          ErrorCode::InvalidCharacter);
    CHECK(parsePixJson(R"({"pix": [{"endToEndId": "E1", "valor": "1.00", "horario": "2025-13-01"}]})", c.sink()).error().code ==
          ErrorCode::InvalidValue);
    CHECK(parsePixJson(R"({"pix": []} x)", c.sink()).error().code == ErrorCode::InvalidCharacter);
    CHECK(parsePixJson(R"({"pix": [}})", c.sink()).error().code == ErrorCode::InvalidCharacter);
    const string badEscape = R"({"a": "\x"})";
    CHECK(parsePixJson(badEscape, c.sink()).error().code == ErrorCode::InvalidCharacter);
    CHECK(parsePixJson(R"({"a": 01})", c.sink()).error().code == ErrorCode::InvalidCharacter);
    CHECK(parsePixJson(R"({"a": "sem fim)", c.sink()).error().code == ErrorCode::UnexpectedEnd);
    CHECK(parsePixJson(string(100, '[') , c.sink()).error().code == ErrorCode::InvalidCharacter);
    CHECK(parsePixJson("{\"a\": " + string(100, '['), c.sink()).error().code == ErrorCode::InvalidValue);
    const string bad = R"({"pix": [{"endToEndId": "E1", "valor": "1.0a", "horario": "2025-01-01"}]})";
    CHECK(parsePixJson(bad, c.sink()).error().offset == bad.find("1.0a") + 3);
}

TEST_CASE("MappedFile", "[statement][file]") {
    const auto path = filesystem::temp_directory_path() / "celerpay_mapped_test.txt";
    {
        ofstream out(path, ios::binary);
        out << sampleCnab();
    }
    {
        auto file = MappedFile::open(path);
        REQUIRE(file);
        CHECK(file->data() == sampleCnab());
        Collector c;
        CHECK(parseCnab240(file->data(), cnab240::febraban(), c.sink())->transactions == 2);
        MappedFile moved = std::move(*file);
        CHECK(moved.data().size() == sampleCnab().size());
        CHECK(file->data().empty());
    }
    filesystem::remove(path);

    const auto emptyPath = filesystem::temp_directory_path() / "celerpay_mapped_empty.txt";
    { ofstream out(emptyPath, ios::binary); }
    {
        const auto empty = MappedFile::open(emptyPath);
        REQUIRE(empty);
        CHECK(empty->data().empty());
    }
    filesystem::remove(emptyPath);

    CHECK(MappedFile::open(filesystem::temp_directory_path() / "celerpay_nao_existe.txt").error().code == ErrorCode::IoError);
}
