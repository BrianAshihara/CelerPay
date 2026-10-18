import json
import sys
import time
from pathlib import Path

from validate_docbr import CNPJ, CPF


def measure(items, fn):
    start = time.perf_counter()
    rounds = 0
    valid = 0
    while True:
        valid = sum(1 for item in items if fn(item))
        rounds += 1
        elapsed = time.perf_counter() - start
        if elapsed >= 1.0:
            return {"items_per_second": rounds * len(items) / elapsed, "valid": valid, "total": len(items)}


def main():
    data = Path(sys.argv[1])
    load = lambda name: (data / f"{name}.txt").read_text().split()
    cpf = CPF()
    cnpj = CNPJ()
    results = {
        "cpf": measure(load("cpf"), cpf.validate),
        "cnpj": measure(load("cnpj"), cnpj.validate),
        "cnpj_alnum": measure(load("cnpj_alnum"), cnpj.validate),
    }
    print(json.dumps({"library": "validate-docbr", "results": results}))


if __name__ == "__main__":
    main()
