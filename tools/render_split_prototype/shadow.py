"""Use compiled renderer and queue boundaries, adapting older reference builds as needed."""
import hashlib
import json
from pathlib import Path
import shlex
import subprocess


def runtime_receive_archive(reference, nm):
    archive = reference / "lib/N64ModernRuntime/ultramodern/libultramodern.a"
    symbols = subprocess.check_output([nm, "-g", "--defined-only", str(archive)], text=True)
    if any(line.split()[-2:] == ["T", "ultramodern_recv_mesg"] for line in symbols.splitlines()):
        return archive
    return None


def build_shadow_renderer(reference, output, compiler, audit_present, receive_archive, nm):
    entries = json.loads((reference / "compile_commands.json").read_text())
    entry = next(e for e in entries if e["file"].replace("\\", "/").endswith("/src/renderer.cpp"))
    object_path = Path(entry["output"])
    original_object = object_path.relative_to(reference).as_posix()
    undefined = subprocess.check_output([nm, "-u", str(object_path)], text=True)
    symbols = {line.split()[-1] for line in undefined.splitlines() if line.split()}
    if {"wdc_render_frame_source", "wdc_render_frame_callbacks"} & symbols:
        replacement = None
        provenance = {"renderer": "player", "task_memory": "queued" if "wdc_render_frame_callbacks" in symbols else "frame-source",
                      "object_sha256": hashlib.sha256(object_path.read_bytes()).hexdigest()}
    else:
        replacement, provenance = build_renderer_copy(entry, output, compiler, audit_present)
    if receive_archive:
        provenance["queue"] = {"boundary": "runtime", "symbol": "ultramodern_recv_mesg",
                               "archive_sha256": hashlib.sha256(receive_archive.read_bytes()).hexdigest()}
        (output / "shadow-source.json").write_text(json.dumps(provenance, indent=2))
        return replacement, original_object, None

    queue_entry = next(e for e in entries if e["file"].replace("\\", "/").endswith("/ultramodern/src/mesgqueue.cpp"))
    queue_original = Path(queue_entry["file"]).read_bytes()
    queue_source = queue_original.decode("utf-8").replace("\r\n", "\n")
    old_store = "        *TO_PTR(OSMesg, msg_) = TO_PTR(OSMesg, mq->msg)[mq->first];"
    if queue_source.count(old_store) != 1:
        raise RuntimeError("Runtime receive-result seam changed")
    queue_source = ('#include <cstdint>\nextern "C" bool prototype_store_message(uint8_t*, int32_t, uint32_t);\n' +
                    queue_source.replace(old_store, '''        const auto value = TO_PTR(OSMesg, mq->msg)[mq->first];
        if (!prototype_store_message(rdram, msg_, uint32_t(value)))
            *TO_PTR(OSMesg, msg_) = value;'''))
    queue_copy = output / "shadow_mesgqueue.cpp"
    queue_copy.write_text(queue_source)
    queue_obj = output / "shadow_mesgqueue.o"
    queue_command = shlex.split(queue_entry["command"])
    queue_command[0] = compiler
    queue_command[queue_command.index("-o") + 1] = str(queue_obj)
    queue_command[queue_command.index("-c") + 1] = str(queue_copy)
    subprocess.run(queue_command, cwd=queue_entry["directory"], check=True)
    provenance["queue"] = {"boundary": "reference-copy", "source": queue_entry["file"],
                           "source_sha256": hashlib.sha256(queue_original).hexdigest(),
                           "copied_sha256": hashlib.sha256(queue_copy.read_bytes()).hexdigest(),
                           "compile_command": queue_command}
    (output / "shadow-source.json").write_text(json.dumps(provenance, indent=2))
    return replacement, original_object, str(queue_obj)


def build_renderer_copy(entry, output, compiler, audit_present):
    original = Path(entry["file"]).read_bytes()
    source = original.decode("utf-8").replace("\r\n", "\n")

    def replace(old, new):
        nonlocal source
        if source.count(old) != 1:
            raise RuntimeError("Shadow renderer seam changed: " + old[:100])
        source = source.replace(old, new)

    source = ('#include <mutex>\n#include <cstdint>\n'
              'extern "C" uint8_t* prototype_task_memory();\n'
              'extern "C" void prototype_renderer_finished();\n'
              'extern "C" bool prototype_disable_readback(uint8_t*);\n'
              'extern "C" std::mutex& prototype_memory_mutex();\n' + source)
    replace('    void send_dl(const OSTask* task) override {', '''    void send_dl(const OSTask* task) override {
        std::lock_guard memory_lock(prototype_memory_mutex());
        rdram = prototype_task_memory();
        app->core.RDRAM = rdram;
        app->state->RDRAM = rdram;
        const auto saved_render_to_ram = app->state->extended.renderToRAM;
        const bool override_readback = prototype_disable_readback(rdram);
        if (override_readback) app->state->setRenderToRAM(0);''')
    replace('        wdc::rdp_capture::end_task();', '''        wdc::rdp_capture::end_task();
        if (override_readback) app->state->setRenderToRAM(saved_render_to_ram);
        prototype_renderer_finished();''')
    replace('    void update_screen() override {', '''    void update_screen() override {
        std::lock_guard memory_lock(prototype_memory_mutex());''')
    if audit_present:
        source = '#include <cstdint>\nextern "C" void prototype_present_bound(uint64_t, uint64_t);\n' + source
        replace("        app->updateScreen();", "        const auto previous_present_id = app->state->presentId;\n        app->updateScreen();\n        prototype_present_bound(previous_present_id, app->state->presentId);")
    copied = output / "shadow_renderer.cpp"
    copied.write_text(source)
    obj = output / "shadow_renderer.o"
    command = shlex.split(entry["command"])
    command[0] = compiler
    command[command.index("-o") + 1] = str(obj)
    command[command.index("-c") + 1] = str(copied)
    # Quoted includes were originally resolved beside the reference source.
    command.extend(["-I", str(Path(entry["file"]).parent)])
    result = subprocess.run(command, cwd=entry["directory"], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (output / "shadow-build.log").write_text(result.stdout)
    if result.returncode:
        raise RuntimeError("Copied renderer compilation failed; inspect shadow-build.log")
    return str(obj), {
        "renderer": "reference-copy",
        "source": entry["file"], "source_sha256": hashlib.sha256(original).hexdigest(),
        "copied_sha256": hashlib.sha256(copied.read_bytes()).hexdigest(),
        "compile_command": command,
    }
