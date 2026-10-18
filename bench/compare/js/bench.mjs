import { readFileSync } from "node:fs";
import { join } from "node:path";
import { isValidBoleto, isValidCnpj, isValidCpf, isValidPixPayload } from "@brazilian-utils/brazilian-utils";

const PIX =
  "00020126580014br.gov.bcb.pix0136123e4567-e12b-12d1-a456-4266554400005204000053039865802BR5913Fulano de Tal6008BRASILIA62070503***63041D3D";

function measure(items, fn) {
  const start = process.hrtime.bigint();
  let rounds = 0;
  let valid = 0;
  for (;;) {
    valid = 0;
    for (const item of items) {
      if (fn(item)) valid++;
    }
    rounds++;
    const elapsed = Number(process.hrtime.bigint() - start) / 1e9;
    if (elapsed >= 1.0) {
      return { items_per_second: (rounds * items.length) / elapsed, valid, total: items.length };
    }
  }
}

const dir = process.argv[2];
const load = (name) => readFileSync(join(dir, `${name}.txt`), "utf8").split(/\s+/).filter(Boolean);

const results = {
  cpf: measure(load("cpf"), isValidCpf),
  cnpj: measure(load("cnpj"), isValidCnpj),
  cnpj_alnum: measure(load("cnpj_alnum"), isValidCnpj),
  boleto: measure(load("boleto"), isValidBoleto),
  pix: measure(Array(10000).fill(PIX), isValidPixPayload),
};

console.log(JSON.stringify({ library: "@brazilian-utils/brazilian-utils", results }));
