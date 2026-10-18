# CelerPay

[![CI](https://github.com/BrianAshihara/CelerPay/actions/workflows/ci.yml/badge.svg)](https://github.com/BrianAshihara/CelerPay/actions/workflows/ci.yml)
[![Licença: MIT](https://img.shields.io/badge/licen%C3%A7a-MIT-blue.svg)](LICENSE)
![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)

Biblioteca C++23 de alta performance para pagamentos brasileiros. Faz parse, montagem e validação de PIX, boleto e CPF/CNPJ, e concilia cobranças esperadas com pagamentos recebidos (retorno CNAB, extrato OFX, PIX recebidos). Rápida, exata e sem dependências.

## Princípios

- **Correto primeiro, depois rápido.** Todo caminho com SIMD tem uma implementação escalar de referência e testes comparando as duas.
- **Erros como valores.** `std::expected`, sem exceções na API pública. Todo erro diz o motivo e a posição.
- **Dinheiro é inteiro em centavos.** Nunca ponto flutuante, nem em juros compostos.
- **Núcleo puro.** Entrada, resultado. I/O só nos leitores de extrato.
- **Zero cópia** com `std::string_view` e sem alocação nos caminhos quentes.
- **Determinístico e explicável.** A mesma entrada gera sempre a mesma saída, e cada conciliação diz por que casou.

## Módulos

| Header | O que faz |
|---|---|
| `celerpay/money.hpp` | `Money` em centavos, aritmética com checagem de estouro, `parseAmount` e `formatBrl` |
| `celerpay/document.hpp` | CPF e CNPJ, incluindo o CNPJ alfanumérico, com validação em lote via SIMD |
| `celerpay/checkdigit.hpp`, `crc16.hpp`, `tlv.hpp` | Módulo 10 e 11, CRC16-CCITT e TLV do EMV |
| `celerpay/pix.hpp` | BR Code PIX estático e dinâmico, chaves PIX |
| `celerpay/qr.hpp` | Encoder de QR Code próprio (versões 1 a 40, níveis L a H) e SVG. Módulo opcional |
| `celerpay/boleto.hpp` | Linha digitável e código de barras (bancário e arrecadação), fator de vencimento com o reinício de 2025, boleto híbrido |
| `celerpay/rules.hpp` | Juros, multa, desconto e calendário bancário com feriados nacionais |
| `celerpay/statement.hpp` | Leitores de CNAB 240, OFX 1.x e 2.x e JSON de PIX recebidos, em streaming |
| `celerpay/reconcile.hpp` | Motor de conciliação em passadas, com status, confiança e explicação |

## Exemplos

### PIX copia e cola e QR Code
```cpp
pix::Payload payload;
payload.key = "fulano@example.com";
payload.merchantName = "Fulano de Tal";
payload.merchantCity = "São Paulo";
payload.amount = Money::fromCents(4990);
payload.txid = "PEDIDO42";

const auto code = pix::build(payload);
const auto svg = qr::pixToSvg(payload);
```
```
00020126400014br.gov.bcb.pix0118fulano@example.com520400005303986540549.905802BR5913Fulano de Tal6009Sao Paulo62120508PEDIDO4263046F32
```

### Boleto
```cpp
const auto barcode = boleto::parse("00190.50095 40144.816069 06809.350314 3 37370000000100");
const auto fields = boleto::bankFields(*barcode);
const auto due = boleto::dueDate(fields.dueFactor, 2008y / January / 1d);
```
```
banco: 001, valor: R$ 1,00, vencimento: 31/12/2007
```

### CPF e CNPJ
```cpp
const auto cnpj = parseCnpj("12abc34501de35");
printf("%s\n", formatCnpj(*cnpj).c_str());

const auto bad = parseCpf("529.982.247-26");
printf("%s na posição %llu\n", string(bad.error().detail).c_str(), static_cast<unsigned long long>(bad.error().offset));
```
```
12.ABC.345/01DE-35
dígito verificador do CPF não confere na posição 13
```

### Juros, multa e desconto
```cpp
rules::ChargeTerms terms;
terms.principal = Money::fromCents(100000);
terms.dueDate = 2025y / March / 1d;
terms.interest = {rules::InterestType::DailyAmount, Money::fromCents(33), {}};
terms.fine = {rules::FineType::Percent, {}, *rules::parsePercent("2")};

const auto due = rules::amountDue(terms, 2025y / March / 6d, rules::Calendar::brazil());
printf("%s\n", rules::explain(*due).c_str());
```
```
principal R$ 1.000,00 + juros R$ 1,65 (5 dias de atraso) + multa R$ 20,00 = R$ 1.021,65, vencimento efetivo 05/03/2025
```

### Conciliação
```cpp
reconcile::PaymentStore store;
statement::parsePixJson(pixRecebidos, [&](const statement::Transaction& t) { store.add(t); });

const auto report = reconcile::run(charges, store.payments(), options, rules::Calendar::brazil());
for (size_t g = 0; g < report->groups().size(); ++g) {
    printf("%s\n", reconcile::explain(*report, g, charges, store.payments()).c_str());
}
```
```
Matched (ExactKey, confiança 100): chave exata (txid ou nosso número); cobranças C-1; pagamentos PEDIDO42; esperado R$ 150,00; pago R$ 150,00
Unidentified (None, confiança 0): nenhuma cobrança candidata; pagamentos E2; pago R$ 77,70
Overdue (None, confiança 0): vencida sem pagamento; cobranças C-2; esperado R$ 99,00; pago R$ 0,00
```

Os programas completos estão em [`examples/`](examples).

## Performance

Windows, 12 CPUs a 3,5 GHz, MSVC Release, SIMD `avx2`. 100 mil itens válidos por conjunto, a mesma entrada para as três bibliotecas.

| Operação | CelerPay | validate-docbr (Python) | brazilian-utils (JS) |
|---|---|---|---|
| CPF | 72 M/s | 195 mil/s (371x) | 4,2 M/s (17x) |
| CNPJ numérico | 74 M/s | 141 mil/s (527x) | 2,0 M/s (36x) |
| CNPJ alfanumérico | 78 M/s | 128 mil/s (609x) | não suporta |
| Linha digitável | 3,2 M/s | não tem | 1,1 M/s (3x) |
| BR Code PIX (parse) | 2,2 M/s | não tem | 307 mil/s (7x) |

- Conciliação de 1,07 M cobranças contra 1,07 M pagamentos: **1,9 s**.
- CNAB 240 de 1 GB (2,2 M transações): 0,7 s mapeado em memória, com pico de **4,4 MB** de memória privada.
- Kernel SIMD das somas de DV: 189 M/s com AVX2, contra 62 M/s no escalar.

Para reproduzir: `python bench/run.py`. O script compila, gera os dados, roda as três bibliotecas e escreve `bench/results.md`.

## Requisitos

- CMake 3.25+ e Ninja
- Compilador C++23: MSVC 19.40+ (VS 2022 17.10+), GCC 13+, Clang 19+ ou Apple Clang 16+
- [vcpkg](https://vcpkg.io) com a variável `VCPKG_ROOT` definida, só para testes e benchmarks (Catch2, Google Benchmark e zxing-cpp)

## Build

```sh
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

| Preset | Plataforma |
|---|---|
| `windows-debug`, `windows-release` | Windows, MSVC |
| `windows-clang-cl-debug` | Windows, clang-cl |
| `windows-fuzz` | Windows, MSVC com ASan e libFuzzer |
| `linux-gcc-debug`, `linux-gcc-release` | Linux, GCC |
| `linux-clang-asan` | Linux, Clang com ASan e UBSan |
| `linux-fuzz` | Linux, Clang com libFuzzer |
| `macos-debug`, `macos-release` | macOS, Apple Clang |

No Windows, rode a partir de um "Developer PowerShell for VS 2022" ou abra a pasta no VS Code com a extensão CMake Tools.

### Opções

| Opção | Padrão | Descrição |
|---|---|---|
| `CELERPAY_BUILD_QR` | `ON` | Módulo de QR Code (`celerpay::qr`) |
| `CELERPAY_BUILD_TESTS` | `ON` se for o projeto principal | Testes com Catch2 e zxing-cpp |
| `CELERPAY_BUILD_BENCH` | `OFF` (`ON` nos presets) | Benchmarks com Google Benchmark |
| `CELERPAY_BUILD_EXAMPLES` | `ON` se for o projeto principal | Exemplos |
| `CELERPAY_BUILD_FUZZ` | `OFF` | Alvos de fuzz (libFuzzer) |
| `CELERPAY_WARNINGS_AS_ERRORS` | `OFF` | Warnings viram erro (ligado no CI) |
| `CELERPAY_SANITIZE` | `OFF` | ASan e UBSan em GCC/Clang |
| `BUILD_SHARED_LIBS` | `OFF` | Gera biblioteca dinâmica |

### Fuzz e carga

```sh
cmake --preset windows-fuzz
cmake --build --preset windows-fuzz
build/windows-fuzz/fuzz/pix_fuzz fuzz/corpus/pix -max_total_time=60
```
Alvos: `tlv_fuzz`, `pix_fuzz`, `boleto_fuzz`, `statement_fuzz`. O teste de carga `celerpay_stress` gera e processa um CNAB de 1 GB.

## Usando em outro projeto

```sh
cmake --install build/windows-release --prefix C:/libs/celerpay
```
```cmake
find_package(celerpay CONFIG REQUIRED)
target_link_libraries(meuApp PRIVATE celerpay::celerpay)
target_link_libraries(meuApp PRIVATE celerpay::qr)
```

## Roadmap

**Parte 1, formatos (v1.0)**
- [x] M0 · Fundação: build, CI, `Error`, `Money`
- [x] M1 · Primitivas: mod 10/11, CRC16, TLV, CPF e CNPJ (inclusive alfanumérico)
- [x] M2 · PIX: BR Code estático e dinâmico, chaves PIX
- [x] M2b · QR Code próprio, saída em SVG
- [x] M3 · Boleto: linha digitável, código de barras, fator de vencimento, híbrido
- [x] M4 · Performance: APIs em lote, SIMD, benchmarks

**Parte 2, conciliação (v2.0)**
- [x] M5 · Regras financeiras: juros, multa, desconto, calendário bancário
- [x] M6 · Extratos: CNAB 240, OFX, PIX recebidos
- [x] M7 · Motor de conciliação

**Depois da v2.0**
- [ ] Bindings Python (pybind11)
- [ ] PIX Automático

## Licença

[MIT](LICENSE)
