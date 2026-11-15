"""Compare SSE4.1 and AVX2 code generation on one captured BOSS task.

Offline only; copied generated source and executables stay under ignored build.
The existing benchmark requires exact RDP output on every iteration.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-build", required=True, type=Path)
    parser.add_argument("--capture", required=True, type=Path)
    parser.add_argument("--iterations", type=int, default=500)
    args = parser.parse_args()
    if not 1 <= args.iterations <= 1000000:
        parser.error("--iterations must be 1..1000000")
    reference = args.reference_build.resolve()
    capture = args.capture.resolve()
    root = Path(__file__).resolve().parents[2] / "build/render-split-prototype/boss-avx2"
    root.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="run-", dir=root))
    manifest = output / "evidence.json"
    manifest.write_text(json.dumps({"status": "running", "iterations": args.iterations}, indent=2))
    print(f"Offline BOSS evidence: {output}", flush=True)
    try:
        benchmark(args, reference, capture, output)
    except BaseException as error:
        evidence = json.loads(manifest.read_text())
        evidence.update(status="failed", error=str(error))
        manifest.write_text(json.dumps(evidence, indent=2))
        raise


def benchmark(args, reference, capture, output):
    cache = (reference / "CMakeCache.txt").read_text()
    compiler = re.search(r"^CMAKE_CXX_COMPILER:[^=]+=(.*)$", cache, re.M)[1]
    env = dict(os.environ)
    env["PATH"] = str(Path(compiler).parent) + os.pathsep + env.get("PATH", "")
    check = output / "check_cpu.cpp"
    check.write_text('int main() { __builtin_cpu_init(); return __builtin_cpu_supports("avx2") ? 0 : 1; }\n')
    check_exe = output / "check_cpu.exe"
    subprocess.run([compiler, str(check), "-o", str(check_exe)], check=True)
    if subprocess.run([str(check_exe)], env=env).returncode:
        raise SystemExit("This experiment requires an AVX2-capable host and OS")

    entries = json.loads((reference / "compile_commands.json").read_text())
    entry = next(e for e in entries if e["file"].replace("\\", "/").endswith("/src/aspMain.cpp"))
    original = Path(entry["file"]).read_bytes()
    copied = output / "boss_avx2.cpp"
    copied.write_bytes(original)
    obj = output / "boss_avx2.o"
    command = shlex.split(entry["command"])
    command[0] = compiler
    command[command.index("-o") + 1] = str(obj)
    command[command.index("-c") + 1] = str(copied)
    command.append("-mavx2")
    result = subprocess.run(command, cwd=entry["directory"], capture_output=True, text=True)
    (output / "build.log").write_text(result.stdout + result.stderr)
    if result.returncode:
        raise SystemExit("AVX2 compilation failed; inspect boss-avx2/build.log")

    ninja = (reference / "build.ninja").read_text()
    block = re.search(r"^build wdc_boss_bench\.exe: .*?(?=\n\n)", ninja, re.M | re.S)[0]
    inputs = block.splitlines()[0].split(": ", 1)[1].split(" |", 1)[0].split(" ", 1)[1]
    if "$" in inputs:
        raise SystemExit("Escaped Ninja object paths are unsupported")
    objects = shlex.split(inputs)
    boss = "CMakeFiles/WdcBoss.dir/src/aspMain.cpp.obj"
    if objects.count(boss) != 1:
        raise SystemExit("BOSS benchmark object seam changed")
    objects[objects.index(boss)] = str(obj)
    exe = output / "boss_avx2_bench.exe"
    libraries = re.search(r"^  LINK_LIBRARIES = (.*)$", block, re.M)[1]
    subprocess.run([compiler, *objects, *shlex.split(libraries), "-o", str(exe)], cwd=reference, check=True)
    evidence = {"status": "running", "source": entry["file"], "source_sha256": hashlib.sha256(original).hexdigest(),
                "capture_sha256": hashlib.sha256(capture.read_bytes()).hexdigest(),
                "compile_command": command, "iterations": args.iterations, "runs": []}
    # Bracket the experimental replay with controls; exclude compilation from timing.
    for name, binary in [("baseline-before", reference / "wdc_boss_bench.exe"),
                         ("avx2", exe), ("baseline-after", reference / "wdc_boss_bench.exe")]:
        result = subprocess.run([str(binary), str(capture), "--iterations", str(args.iterations)],
                                env=env, capture_output=True, text=True)
        log = result.stdout + result.stderr
        (output / f"{name}.log").write_text(log)
        evidence["runs"].append({"name": name, "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
                                 "returncode": result.returncode})
        (output / "evidence.json").write_text(json.dumps(evidence, indent=2))
        print(name + ":\n" + log, flush=True)
        if result.returncode:
            raise SystemExit("BOSS replay failed; reject this code-generation experiment")
    evidence["status"] = "complete"
    (output / "evidence.json").write_text(json.dumps(evidence, indent=2))


if __name__ == "__main__":
    main()
