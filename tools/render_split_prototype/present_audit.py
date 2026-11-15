"""Observe actual RT64 swap calls in a read-only-reference copied object."""
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
from capture_present import patch_capture


def build_present_audit(reference, output, compiler, capture=False):
    entries = json.loads((reference / "compile_commands.json").read_text())
    entry = next(e for e in entries if e["file"].replace("\\", "/").endswith("/hle/rt64_present_queue.cpp"))
    original = Path(entry["file"]).read_bytes()
    source = original.decode().replace("\r\n", "\n")
    seam = "                swapChainValid = ext.swapChain->present(swapChainIndex, &waitSemaphore, 1);"
    if source.count(seam) != 1:
        raise RuntimeError("RT64 present audit seam changed")
    source = '#include <cstdint>\nextern "C" void prototype_swap_observed(uint64_t, uint32_t, bool);\nextern "C" void prototype_swap_batch_finished(uint64_t);\nextern "C" void prototype_present_skipped(uint64_t, bool, bool);\n' + source.replace(
        seam, seam + "\n                prototype_swap_observed(present.presentId, i, swapChainValid);")
    skip = "                skipPresent = skipPresent || ext.swapChain->isEmpty();"
    branch = "                if (skipPresent) {\n                    skipInterpolation();"
    if source.count(skip) != 1 or source.count(branch) != 1:
        raise RuntimeError("RT64 present skip seam changed")
    source = source.replace(skip, """                const bool prototype_backlog = skipPresent;
                const bool prototype_empty = ext.swapChain->isEmpty();
                skipPresent = skipPresent || prototype_empty;""").replace(branch, """                if (skipPresent) {
                    prototype_present_skipped(present.presentId, prototype_backlog, prototype_empty);
                    skipInterpolation();""")
    end = "        }\n    }\n\n    void PresentQueue::skipInterpolation()"
    if source.count(end) != 1:
        raise RuntimeError("RT64 present completion seam changed")
    source = source.replace(end, "        }\n        prototype_swap_batch_finished(present.presentId);\n    }\n\n    void PresentQueue::skipInterpolation()")
    application_evidence = None
    if capture:
        application = Path(entry["file"]).with_name("rt64_application.cpp")
        application_bytes = application.read_bytes()
        application_source = application_bytes.decode()
        if application_source.count("swapChainDesc.format =") != 1 or "swapChainDesc.format = RenderFormat::B8G8R8A8_UNORM;" not in application_source:
            raise RuntimeError("Capture requires the verified BGRA8 swap-chain format")
        application_evidence = {"source": str(application), "sha256": hashlib.sha256(application_bytes).hexdigest()}
        backend = application.parent.parent / "contrib/plume/plume_d3d12.cpp"
        backend_bytes = backend.read_bytes()
        backend_source = backend_bytes.decode()
        if backend_source.count("setSamplePositions(dstLocation.texture);") != 1 or "static_cast<const D3D12Buffer *>(location.buffer)" not in backend_source:
            raise RuntimeError("Capture D3D12 footprint compatibility seam changed")
        application_evidence["backend"] = {"source": str(backend), "sha256": hashlib.sha256(backend_bytes).hexdigest()}

        source = patch_capture(source)
    copied = output / "audit_rt64_present.cpp"
    copied.write_text(source)
    obj = output / "audit_rt64_present.o"
    command = shlex.split(entry["command"])
    command[0] = compiler
    command[command.index("-o") + 1] = str(obj)
    command[command.index("-c") + 1] = str(copied)
    command.extend(["-I", str(Path(entry["file"]).parent)])
    result = subprocess.run(command, cwd=entry["directory"], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (output / "present-audit-build.log").write_text(result.stdout)
    if result.returncode:
        raise RuntimeError("Copied present queue compilation failed; inspect present-audit-build.log")
    (output / "present-audit-source.json").write_text(json.dumps({
        "capture": capture, "application_format_evidence": application_evidence,
        "source": entry["file"], "source_sha256": hashlib.sha256(original).hexdigest(),
        "copied_sha256": hashlib.sha256(copied.read_bytes()).hexdigest(),
        "compile_command": command,
    }, indent=2))
    return str(obj)


def validate_swaps(bindings, swaps, callbacks):
    """Require one successful swap per callback, with the same ordered labels."""
    labels = [(p["frame"], p["draw"]) for p in callbacks]
    if not labels or [(b["frame"], b["draw"]) for b in bindings] != labels:
        raise ValueError("Presentation ID bindings do not match callbacks")
    ids = [b["id"] for b in bindings]
    if any(b["id"] <= b["previous"] for b in bindings):
        raise ValueError("Renderer callback did not enqueue a new presentation")
    if len(set(ids)) != len(ids):
        raise ValueError("Presentation ID reused")
    wanted = set(ids)
    selected = [s for s in swaps if s["id"] in wanted]
    if [s["id"] for s in selected] != ids or any(s["index"] != 0 or not s["valid"] for s in selected):
        raise ValueError("Missing, duplicated, reordered or failed RT64 swap")
    if any(a["us"] >= b["us"] for a, b in zip(selected, selected[1:])):
        raise ValueError("Non-increasing swap timestamps")
    return selected
