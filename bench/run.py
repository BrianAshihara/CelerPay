"""Compila em Release, roda os benchmarks do CelerPay, do validate-docbr e do brazilian-utils e escreve bench/results.md."""

import json
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
COMPARE = ROOT / "bench" / "compare"
DATA = ROOT / "build" / "bench-data"
WINDOWS = platform.system() == "Windows"
PRESET = {"Windows": "windows-release", "Linux": "linux-gcc-release", "Darwin": "macos-release"}[platform.system()]

ROWS = [
    ("cpf", "CPF", "cpfBatch", "cpfSingle"),
    ("cnpj", "CNPJ numérico", "cnpjBatch", "cnpjSingle"),
    ("cnpj_alnum", "CNPJ alfanumérico", "cnpjAlnumBatch", "cnpjAlnumSingle"),
    ("boleto", "Linha digitável", None, "boletoLine"),
    ("pix", "BR Code PIX (parse)", None, "pixParse"),
]


def run(command, env=None, cwd=ROOT):
    print("+", " ".join(str(c) for c in command), flush=True)
    return subprocess.run(command, cwd=cwd, env=env, check=True, capture_output=True, text=True).stdout


def msvc_env():
    vswhere = Path(os.environ.get("ProgramFiles(x86)", "")) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    path = run([vswhere, "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                "-property", "installationPath"]).strip()
    vcvars = Path(path) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
    output = subprocess.run(f'"{vcvars}" >nul && set', shell=True, check=True, capture_output=True, text=True).stdout
    env = dict(line.split("=", 1) for line in output.splitlines() if "=" in line)
    env.setdefault("VCPKG_ROOT", str(Path(path) / "VC" / "vcpkg"))
    return env


def build():
    env = msvc_env() if WINDOWS else dict(os.environ)
    run(["cmake", "--preset", PRESET], env=env)
    run(["cmake", "--build", "--preset", PRESET, "--target", "celerpay_bench"], env=env)
    return env


def celerpay(env):
    exe = ROOT / "build" / PRESET / "bench" / ("celerpay_bench.exe" if WINDOWS else "celerpay_bench")
    run([exe, f"--dump-data={DATA}"], env=env)
    env = dict(env, CELERPAY_BENCH_DATA=str(DATA))
    output = run([exe, "--benchmark_format=json", "--benchmark_repetitions=3", "--benchmark_report_aggregates_only=true"], env=env)
    report = json.loads(output)
    medians = {b["run_name"]: b.get("items_per_second", 0) for b in report["benchmarks"] if b.get("aggregate_name") == "median"}
    for b in report["benchmarks"]:
        if b.get("aggregate_name") == "median" and b["run_name"].startswith("reconcileSynthetic/1000000"):
            medians["reconcile_ms"] = b["real_time"]
            medians["reconcile_charges"] = b.get("charges", 0)
            medians["reconcile_payments"] = b.get("payments", 0)
    return report["context"].get("celerpay_simd", "?"), report["context"], medians


def python_bench():
    venv = COMPARE / ".venv"
    python = venv / ("Scripts/python.exe" if WINDOWS else "bin/python")
    if not python.exists():
        run([sys.executable, "-m", "venv", venv])
    run([python, "-m", "pip", "install", "-q", "-r", COMPARE / "python" / "requirements.txt"])
    return json.loads(run([python, COMPARE / "python" / "bench.py", DATA]))


def js_bench():
    js = COMPARE / "js"
    npm = shutil.which("npm") or "npm"
    run([npm, "ci", "--silent"], cwd=js)
    return json.loads(run(["node", js / "bench.mjs", DATA], cwd=js))


def rate(value):
    if value >= 1e6:
        return f"{value / 1e6:.1f} M/s"
    if value >= 1e3:
        return f"{value / 1e3:.0f} mil/s"
    return f"{value:.0f}/s"


def other(result, key):
    entry = result["results"].get(key)
    if entry is None:
        return "não tem", None
    if entry["valid"] != entry["total"]:
        return f"não suporta ({entry['valid']}/{entry['total']} válidos)", None
    return rate(entry["items_per_second"]), entry["items_per_second"]


def main():
    env = build()
    simd, context, cpp = celerpay(env)
    python = python_bench()
    js = js_bench()

    lines = [
        "# Resultados dos benchmarks",
        "",
        f"Máquina: {context.get('num_cpus')} CPUs a {context.get('mhz_per_cpu')} MHz, {platform.system()}, preset `{PRESET}`, SIMD `{simd}`.",
        "100 mil itens válidos por conjunto, mesma entrada para as três bibliotecas. Mediana de 3 repetições.",
        "",
        "| Operação | CelerPay lote | CelerPay simples | validate-docbr (Python) | brazilian-utils (JS) |",
        "|---|---|---|---|---|",
    ]
    for key, label, batch, single in ROWS:
        best = max(cpp.get(batch or "", 0), cpp.get(single, 0))
        cells = []
        for result in (python, js):
            text, value = other(result, key)
            cells.append(f"{text} ({best / value:.0f}x)" if value else text)
        lines.append(f"| {label} | {rate(cpp[batch]) if batch else '-'} | {rate(cpp[single])} | {cells[0]} | {cells[1]} |")
    lines += [
        "",
        "Entre parênteses: quantas vezes o CelerPay (melhor entre lote e simples) é mais rápido.",
        "",
        f"Kernel de somas dos DVs: escalar {rate(cpp.get('kernelScalar', 0))}, SIMD {rate(cpp.get('kernelAvx2') or cpp.get('kernelNeon') or 0)}.",
        "",
        f"Conciliação de {cpp.get('reconcile_charges', 0) / 1e6:.2f} M cobranças contra {cpp.get('reconcile_payments', 0) / 1e6:.2f} M pagamentos: "
        f"{cpp.get('reconcile_ms', 0) / 1000:.2f} s.",
        "",
        "Gerado por `python bench/run.py`.",
        "",
    ]
    (ROOT / "bench" / "results.md").write_text("\n".join(lines), encoding="utf-8")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
