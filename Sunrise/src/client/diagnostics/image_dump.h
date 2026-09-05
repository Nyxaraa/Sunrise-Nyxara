#pragma once

namespace sunrise::client::diagnostics {

/**
 * Writes the game's mapped image to disk so it can be disassembled offline.
 * `destiny2.exe` is VMProtect-packed: on disk its `.text` is fully encrypted, the retail log
 * strings are absent, and the byte signatures in `patterns/game_signatures.cpp` match nothing.
 * They match at runtime because every scan runs against the mapped image the packer has already
 * decrypted, so that mapped image is the only readable copy of the code and the only thing a
 * disassembler can be pointed at.
 *
 * The dump is one flat file covering the whole `SizeOfImage` span, so a file offset is the image
 * offset and a virtual address is the load base plus that offset. A page the process will not let
 * us read is written as zeroes rather than abandoning the dump, because an unreadable page is
 * normal in a packed image and losing the rest of the file to it helps nobody.
 *
 * Off unless `client.dump_game_image` is set. The file is large — the whole image, about 140 MB —
 * and writing it costs a second or two of boot, so it is a deliberate diagnostic run rather than
 * something every start pays for.
 * @param module Sunrise's own loaded module, used to resolve the artifact directory.
 * @return True when the whole image was written and the manifest beside it was too.
 */
[[nodiscard]] bool dump_game_image(void* module) noexcept;

} // namespace sunrise::client::diagnostics
