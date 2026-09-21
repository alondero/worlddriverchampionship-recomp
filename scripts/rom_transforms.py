"""Measured USA startup transformations; no ROM data is stored here."""
import struct


def expand_cache_markers(rom: bytes, starts: list[int], bias: int) -> tuple[bytes, dict[int, int]]:
    """Replay 80037F20, called by 800380A4, before static translation.

    WDC replaces sentinel arithmetic with cache allocation instructions and
    rebases adjacent stores. Host cache operations later become no-ops, but the
    store rebasing is essential. Keep the original cartridge bytes untouched.
    """
    data = bytearray(rom)
    changes = {}

    def read(pc):
        return struct.unpack_from('>I', data, pc - bias)[0]

    def write(pc, word):
        struct.pack_into('>I', data, pc - bias, word)
        changes[pc] = word

    for start in starts:
        pc = start
        limit = start + 0x10000
        while pc < limit:
            word = read(pc)
            if word == 0x03E00008:
                break
            if word & 0xFC000000 != 0x38000000:
                pc += 4
                continue
            count = (word >> 12) & 15
            base = (word & 0x03E00000) | 0xBC0D0000
            done = 0
            while done < count:
                if pc >= limit:
                    raise ValueError('Unterminated WDC cache marker')
                if read(pc) & 0xFFF == 0xBFD:
                    write(pc, base | (done << 4))
                    done += 1
                pc += 4
            move = ((base >> 10) & 0xF800) | 0x25
            window = pc
            for index in range(6):
                if read(pc) & 0xFC1FF8FF == move:
                    write(pc, 0)
                    for slot in range(12):
                        at = window + slot * 4
                        insn = read(at)
                        if insn & 0xA0000000 == 0xA0000000:
                            write(at, (insn & 0xFC1FFFFF) | (word & 0x03E00000))
                    pc = window + 48
                    break
                pc += 4
        else:
            raise ValueError('Unterminated WDC cache-transform routine')
    return bytes(data), changes
