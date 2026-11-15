"""Instrument a copied RT64 state object; reference sources remain read-only."""
import hashlib
import json
from pathlib import Path
import shlex
import subprocess


def build_rt64_profile(reference, output, compiler):
    entries = json.loads((reference / "compile_commands.json").read_text())
    entry = next(e for e in entries if e["file"].replace("\\", "/").endswith("/hle/rt64_state.cpp"))
    original = Path(entry["file"]).read_bytes()
    source = original.decode().replace("\r\n", "\n")
    start = source.index("    void State::fullSync() {")
    end = source.index("\n    void State::", start + 1)
    body = source[start:end]
    body = body.replace("    void State::fullSync() {", '    void State::fullSync() {\n        PrototypeRT64Timer full_timer{"full_sync"};')
    for call, count, name in [
        ("ext.textureCache->waitForGPUUploads();", 2, "texture_upload_wait"),
        ("ext.framebufferGraphicsWorker->execute();", 2, "framebuffer_execute"),
        ("ext.rasterShaderCache->shaderUber->waitForPipelineCreation();", 1, "pipeline_creation_wait"),
        ("fullSyncFramebufferPairTiles(workload, workload.fbPairs[f], loadOpCursor, rdpTileCursor);", 1, "pair_tiles"),
        ("fullSyncFramebufferPairTiles(workload, fbPair, loadOpCursor, rdpTileCursor);", 1, "pair_tiles"),
        ("ext.drawDataUploader->commandListCopyResources(ext.framebufferGraphicsWorker);", 1, "draw_copy_commands"),
        ("ext.transformsUploader->commandListCopyResources(ext.framebufferGraphicsWorker);", 1, "transform_copy_commands"),
        ("ext.framebufferGraphicsWorker->wait();", 2, "framebuffer_gpu_wait"),
        ("framebufferRenderer->waitForUploaders();", 1, "framebuffer_upload_wait"),
        ("ext.drawDataUploader->wait();", 1, "draw_upload_wait"),
        ("ext.transformsUploader->wait();", 1, "transform_upload_wait"),
    ]:
        if body.count(call) != count:
            raise RuntimeError("RT64 fullSync profiling seam changed: " + call)
        body = body.replace(call, '{ PrototypeRT64Timer timer{"' + name + '"}; ' + call + ' }')
    # Sequential checkpoints partition fullSync without changing scope or
    # moving dependency calls. Nested wait timers remain inclusive diagnostics.
    boundaries = [
        ("        // Validate all tile copies", "stage_prepare"),
        ("        // Ensure there's as many GPU tiles", "stage_tiles_and_calls"),
        ("        // Make sure pipelines for the ubershader", "stage_upload_setup"),
        ("        // Evict from the texture cache", "stage_framebuffer_processing"),
        ("        // Advance the workload queue", "stage_materials_and_cache"),
    ]
    for marker, name in boundaries:
        if body.count(marker) != 1:
            raise RuntimeError("RT64 fullSync stage seam changed: " + marker)
        body = body.replace(marker, '        stage_timer.checkpoint("' + name + '");\n' + marker)
    body = body.replace('PrototypeRT64Timer full_timer{"full_sync"};',
                        'PrototypeRT64Timer full_timer{"full_sync"};\n        PrototypeRT64Stages stage_timer;')
    source = source[:start] + body + source[end:]
    source = '''#include <chrono>
extern "C" void prototype_rt64_phase(const char*, double);
struct PrototypeRT64Stages {
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    void checkpoint(const char* name) {
        const auto end = std::chrono::steady_clock::now();
        prototype_rt64_phase(name, std::chrono::duration<double, std::milli>(end - start).count());
        start = end;
    }
    ~PrototypeRT64Stages() { checkpoint("stage_queue_and_finish"); }
};
struct PrototypeRT64Timer {
    const char* name;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    ~PrototypeRT64Timer() {
        prototype_rt64_phase(name, std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count());
    }
};
''' + source
    copied = output / "profile_rt64_state.cpp"
    copied.write_text(source)
    obj = output / "profile_rt64_state.o"
    command = shlex.split(entry["command"])
    command[0] = compiler
    command[command.index("-o") + 1] = str(obj)
    command[command.index("-c") + 1] = str(copied)
    command.extend(["-I", str(Path(entry["file"]).parent)])
    result = subprocess.run(command, cwd=entry["directory"], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (output / "rt64-profile-build.log").write_text(result.stdout)
    if result.returncode:
        raise RuntimeError("Copied RT64 compilation failed; inspect rt64-profile-build.log")
    (output / "rt64-profile-source.json").write_text(json.dumps({
        "source": entry["file"], "source_sha256": hashlib.sha256(original).hexdigest(),
        "copied_sha256": hashlib.sha256(copied.read_bytes()).hexdigest(),
        "compile_command": command,
    }, indent=2))
    return str(obj)
