"""Pin the build wiring of the BOSS DMA seam.

The generated BOSS unit (src/aspMain.cpp) comes from RSPRecomp and is not
tracked, so the seam that routes its RDRAM writes through the port's capture
journal is pinned from the build's side: the unit must be compiled with
src/boss_dma.h force-included, that header must override the runtime's
DO_DMA_WRITE macro, the runtime header in the pinned dependency must still
define that macro, and the generated unit must still use the macro the
override replaces. tests/test_boss_dma_seam.cpp checks the other half, that
the force-include wins over the runtime header.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CMAKE = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
TESTS_CMAKE = (ROOT / "cmake" / "Tests.cmake").read_text(encoding="utf-8")
BOSS_DMA = (ROOT / "src" / "boss_dma.h").read_text(encoding="utf-8")
GENERATED = ROOT / "src" / "aspMain.cpp"
# The runtime header the seam overrides, in the pinned dependency.
RSP_HEADER = ROOT / "lib" / "N64ModernRuntime" / "librecomp" / "include" / "librecomp" / "rsp.hpp"

# The WdcBoss target and the indented block of options that follows it. The
# block's lines start with one space or tab, so each line matches in one way
# and the pattern cannot backtrack.
BOSS_TARGET_RE = re.compile(r"add_library\(WdcBoss[^)]*\)\s*\n((?:[ \t][^\n]*\n)+)")


def boss_target_block() -> str:
    """Return the CMake lines that configure the generated BOSS unit."""
    match = BOSS_TARGET_RE.search(CMAKE)
    if not match:
        raise AssertionError("CMakeLists.txt does not define the WdcBoss target")
    return match.group(1)


class BossSeamTest(unittest.TestCase):
    def test_the_generated_unit_is_compiled_with_the_seam_header(self) -> None:
        block = boss_target_block()
        self.assertIn("-include", block, "the generated BOSS unit is not force-included")
        self.assertIn("WDC_BOSS_DMA_INCLUDE", block, "the generated BOSS unit does not use the shared seam path")

    def test_the_seam_path_is_the_port_header(self) -> None:
        self.assertRegex(
            CMAKE,
            r'set\(WDC_BOSS_DMA_INCLUDE\s+"[^"]*src/boss_dma\.h"\)',
            "the shared seam path does not name src/boss_dma.h",
        )

    def test_the_seam_test_is_compiled_the_same_way(self) -> None:
        # The host test that checks the override only proves something if it
        # is compiled the way the generated unit is.
        self.assertIn("add_executable(wdc_boss_dma_seam_tests", TESTS_CMAKE)
        target = TESTS_CMAKE.split("add_executable(wdc_boss_dma_seam_tests", 1)[1]
        block = target.split("add_test(", 1)[0]
        self.assertIn('-include "${WDC_BOSS_DMA_INCLUDE}"', block,
                      "the seam's host test is not compiled with the shared force-include")

    def test_the_header_overrides_the_runtime_macro(self) -> None:
        self.assertIn("#undef DO_DMA_WRITE", BOSS_DMA)
        self.assertRegex(BOSS_DMA, r"#define DO_DMA_WRITE\(wr_len\)\s+wdc_boss_dma_write\(")

    def test_the_header_includes_the_runtime_before_overriding_it(self) -> None:
        # The override only wins because the runtime's own include is
        # included first: its include guard then makes the generated unit's
        # include a no-op.
        include = BOSS_DMA.find('#include "librecomp/rsp.hpp"')
        undef = BOSS_DMA.find("#undef DO_DMA_WRITE")
        self.assertNotEqual(include, -1, "the seam header no longer includes the runtime header")
        self.assertNotEqual(undef, -1, "the seam header no longer undefines the runtime macro")
        self.assertLess(include, undef, "the seam header overrides the macro before including it")

    def test_the_pinned_runtime_still_defines_the_macro_we_override(self) -> None:
        # The generated unit is not built in CI, so the check on it below
        # never runs there. This one does: the runtime header belongs to a
        # pinned dependency, and if a new pin renamed the macro the seam
        # would be quietly dead, because the override would define a macro
        # the runtime no longer calls.
        if not RSP_HEADER.is_file():
            self.skipTest("the N64ModernRuntime submodule is not checked out")
        self.assertRegex(
            RSP_HEADER.read_text(encoding="utf-8"),
            r"#define DO_DMA_WRITE\(wr_len\)\s+dma_dmem_to_rdram\(",
            "the pinned runtime no longer defines DO_DMA_WRITE the way the seam overrides it")

    def test_the_generated_unit_still_uses_the_overridden_macro(self) -> None:
        # The generated file is not tracked and is not built in CI, so this
        # only applies once a build has produced it. The pinned-runtime
        # check above holds the seam in CI.
        if not GENERATED.is_file():
            self.skipTest("src/aspMain.cpp has not been generated yet")
        self.assertIn("DO_DMA_WRITE(", GENERATED.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
