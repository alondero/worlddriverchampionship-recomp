"""Diagnostic BGRA8 swap-chain capture in a copied Windows RT64 present queue."""


def read_capture(path):
    try:
        magic, dimensions, maximum, pixels = path.read_bytes().split(b"\n", 3)
        width, height = map(int, dimensions.split())
        if (magic == b"P6" and maximum == b"255" and 0 < width <= 4096
                and 0 < height <= 4096 and len(pixels) == width * height * 3):
            return width, height, pixels
    except ValueError:
        pass
    raise ValueError(f"Invalid presentation capture: {path}")


def patch_capture(source):
    end = source.index("    void PresentQueue::skipInterpolation()")
    source, suffix = source[:end], source[end:]
    def replace(old, new):
        nonlocal source
        if source.count(old) != 1:
            raise RuntimeError("RT64 capture seam changed: " + old[:80])
        source = source.replace(old, new)

    source = '#include <cstdio>\n#include <cstdlib>\nextern "C" bool prototype_capture_enabled();\n' + source
    replace("                RenderCommandList *commandList = ext.presentGraphicsWorker->commandList.get();", """                RenderCommandList *commandList = ext.presentGraphicsWorker->commandList.get();
                std::unique_ptr<RenderBuffer> capture;
                const uint32_t captureWidth = ext.swapChain->getWidth();
                const uint32_t captureHeight = ext.swapChain->getHeight();
                // D3D12 BGRA8 row pitch is aligned to 256 bytes. The builder
                // verifies the pinned application's swap-chain format.
                const uint32_t capturePitch = (captureWidth * 4 + 255) & ~255u;
                if (prototype_capture_enabled()) {
                    if (!captureWidth || !captureHeight || captureWidth > 4096 || captureHeight > 4096) std::_Exit(23);
                    capture = ext.device->createBuffer(RenderBufferDesc::ReadbackBuffer(uint64_t(capturePitch) * captureHeight));
                    if (!capture) std::_Exit(23);
                }""")
    replace("                    commandList->barriers(RenderBarrierStage::NONE, RenderTextureBarrier(swapChainTexture, RenderTextureLayout::PRESENT));", """                    if (capture) {
                        commandList->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(swapChainTexture, RenderTextureLayout::COPY_SOURCE));
                        auto destination = RenderTextureCopyLocation::PlacedFootprint(capture.get(), RenderFormat::B8G8R8A8_UNORM, captureWidth, captureHeight, 1, capturePitch / 4);
                        // Pinned Plume D3D12 calls setSamplePositions on the
                        // destination texture even for a buffer footprint.
                        // Supply the single-sample swap texture for that helper;
                        // footprint conversion still selects destination.buffer.
                        destination.texture = swapChainTexture;
                        commandList->copyTextureRegion(destination, RenderTextureCopyLocation::Subresource(swapChainTexture));
                    }
                    commandList->barriers(RenderBarrierStage::NONE, RenderTextureBarrier(swapChainTexture, RenderTextureLayout::PRESENT));""")
    replace("\n                    ext.presentGraphicsWorker->wait();", """
                    ext.presentGraphicsWorker->wait();
                    if (capture) {
                        // The existing fence covers the copy. This local buffer
                        // remains alive through unmap; no guest RAM is touched.
                        const RenderRange range{0, uint64_t(capturePitch) * captureHeight};
                        const auto *pixels = static_cast<const uint8_t *>(capture->map(0, &range));
                        if (!pixels) std::_Exit(23);
                        char name[96];
                        std::snprintf(name, sizeof(name), "present-%llu-%d.ppm", (unsigned long long)present.presentId, i);
                        auto *file = std::fopen(name, "wb");
                        if (!file || std::fprintf(file, "P6\\n%u %u\\n255\\n", captureWidth, captureHeight) < 0) std::_Exit(23);
                        std::vector<uint8_t> row(captureWidth * 3);
                        for (uint32_t y = 0; y < captureHeight; ++y) {
                            for (uint32_t x = 0; x < captureWidth; ++x) {
                                const auto *pixel = pixels + uint64_t(y) * capturePitch + x * 4;
                                row[x*3] = pixel[2]; row[x*3+1] = pixel[1]; row[x*3+2] = pixel[0];
                            }
                            if (std::fwrite(row.data(), 1, row.size(), file) != row.size()) std::_Exit(23);
                        }
                        if (std::fclose(file)) std::_Exit(23);
                        capture->unmap();
                    }""")
    return source + suffix
