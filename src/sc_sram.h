/* The cartridge's battery-backed save memory (SRAM) on disk.
 *
 * The game keeps saved cities and the scenario win marks in 32 KB of
 * cartridge SRAM. The cart model holds it in memory only, so without this a
 * city saved in the game was gone when the window closed.
 *
 * The file is the raw SRAM, the same layout other SNES emulators use for
 * .srm files. Hidden City 3 adds a versioned, hashed host trailer after
 * those original bytes. It is written whenever the SRAM has changed and stayed
 * unchanged for half a second (the game writes a save over several frames),
 * through a temporary file so a crash cannot leave half a save, and once more
 * at exit. The file as it was at start is kept as <file>.bak.
 *
 * Only the windowed game uses this; --qualify runs never touch the file. */
#ifndef SC_SRAM_H_INCLUDED
#define SC_SRAM_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>

/* Starts persisting `ram` (the cart model's SRAM, `size` bytes) to `path`,
 * loading the file into it first when there is one. False, and nothing is
 * ever written, when the file exists but does not fit this cartridge. */
bool ScSram_Open(uint8_t *ram, uint32_t size, const char *path);
bool ScSram_Active(void);

/* Once per host frame. */
void ScSram_Tick(void);

/* Writes a pending change now; for exit. */
void ScSram_Flush(void);
/* True only when current SRAM and host records reached disk successfully. */
bool ScSram_Stored(void);

/* Around a save-state load. A state carries SRAM as well, but the saved
 * cities on disk are the player's: Hold() before the load and Release()
 * after it put the SRAM that was in effect back. */
void ScSram_Hold(void);
void ScSram_Release(void);
/* Versioned, hashed host record appended after unchanged cartridge SRAM.
 * Unknown/corrupt trailers disable writes; no existing file is truncated. */
const uint8_t *ScSram_Extra(uint32_t *size);
bool ScSram_SetExtra(const uint8_t *data,uint32_t size);
/* Native hidden-slot codec temporarily uses private cartridge bytes. */
void ScSram_Suspend(bool suspended);

#endif /* SC_SRAM_H_INCLUDED */
