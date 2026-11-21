"""Build an isolated runtime presentation experiment; never edit the reference.

Exact substitutions deliberately fail on upstream drift. This is a progressive
single-view experiment, not a replacement for the runtime VI implementation.
"""
import json
import hashlib
from pathlib import Path
import shlex
import subprocess


def build_runtime(reference, output, compiler):
    entries = json.loads((reference / "compile_commands.json").read_text())
    entry = next(e for e in entries if e["file"].replace("\\", "/").endswith("/ultramodern/src/events.cpp"))
    original = Path(entry["file"]).read_bytes()
    source = original.decode("utf-8").replace("\r\n", "\n")

    def replace(old, new):
        nonlocal source
        if source.count(old) != 1:
            raise RuntimeError("Runtime prototype seam changed: " + old[:100])
        source = source.replace(old, new)

    replace("struct ScreenUpdateAction {", "struct ScreenUpdateAction {\n    bool prototype = false;")
    # Keep aggregate initializers source-compatible by moving the new flag last.
    replace("    bool prototype = false;\n    ultramodern::renderer::ViRegs regs;",
            "    ultramodern::renderer::ViRegs regs;\n    bool prototype = false;")
    replace("uint64_t total_vis = 0;", """uint64_t total_vis = 0;
extern "C" void prototype_present_observed(uint32_t);
extern "C" void prototype_screen_begin(bool);
extern "C" void prototype_vi_observed();
// Protected by message_mutex, including the VI thread's state transition.
static bool prototype_present = false;
extern "C" void prototype_set_present(bool enabled) {
    std::lock_guard lock{events_context.message_mutex};
    prototype_present = enabled;
}
""")
    replace("        // If the game hasn't started yet, set a dummy VI mode and origin.",
            "        {\n        std::lock_guard state_lock{events_context.message_mutex};\n"
            "        // If the game hasn't started yet, set a dummy VI mode and origin.")
    replace("        events_context.action_queue.enqueue(ScreenUpdateAction{ events_context.vi.regs });",
            "        if (!prototype_present) events_context.action_queue.enqueue(ScreenUpdateAction{ events_context.vi.regs });")
    replace("        events_context.vi.update_vi();", "        events_context.vi.update_vi();\n"
            "        if (prototype_present) prototype_vi_observed();\n        }")
    replace("    events_context.vi.get_next_state()->framebuffer = frameBufPtr;", """    events_context.vi.get_next_state()->framebuffer = frameBufPtr;
    if (prototype_present) {
        // The game has waited for the prior framebuffer's DP completion.
        // Copy registers into the same serialized graphics queue as SP tasks.
        // VI/AI message delivery keeps its original 60 Hz clock.
        events_context.vi.update_vi();
        events_context.action_queue.enqueue(ScreenUpdateAction{events_context.vi.regs, true});
    }""")
    for name in ("Next", "Current"):
        replace('extern "C" PTR(void) osViGet' + name + 'Framebuffer() {',
                'extern "C" PTR(void) osViGet' + name + 'Framebuffer() {\n'
                '    std::lock_guard lock{events_context.message_mutex};')
    # Observe actual renderer callbacks, not only game-side queue requests.
    replace("                renderer_context->update_screen();", """                prototype_screen_begin(screen_update_action->prototype);
                renderer_context->update_screen();
                prototype_screen_begin(false);
                if (screen_update_action->prototype)
                    prototype_present_observed(screen_update_action->regs.VI_ORIGIN_REG);""")
    copied = output / "prototype_events.cpp"
    copied.write_text(source)
    obj = output / "prototype_events.o"
    command = shlex.split(entry["command"])
    command[0] = compiler
    command[command.index("-o") + 1] = str(obj)
    command[command.index("-c") + 1] = str(copied)
    subprocess.run(command, cwd=entry["directory"], check=True)
    (output / "runtime-source.json").write_text(json.dumps({
        "source": entry["file"],
        "source_sha256": hashlib.sha256(original).hexdigest(),
        "copied_sha256": hashlib.sha256(copied.read_bytes()).hexdigest(),
        "compile_command": command,
    }, indent=2))
    return obj
