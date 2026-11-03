/* save.h - binary chunked save files, slots, autosave. PORTABLE. Owner: T10 (format API defined by T00
   so wave-1 modules can write their own chunks). DESIGN 8.4.

   File: "KAUP" magic, u32 format version, then chunks: 4-char tag, u32 chunk version, u32 byte size,
   data. Unknown chunks are skipped; a missing chunk leaves that module at its reset defaults, so adding
   a chunk never breaks old saves. Integers are little-endian (VC6/x86 and clang/arm64 both are).
   Each module serialises ITSELF: save_game() calls world_save, sim_save, budget_save, fields_save,
   politics_save (in that order); load_game() resets every module, then calls the *_load functions, then
   net_reset/traffic_reset (recomputed state) and world_notify_all. Module save functions write explicit
   fields or fixed-layout structs guarded by STATIC_ASSERT(sizeof...) so layouts match on both compilers.
   Large arrays (tiles, fields) go through the RLE writer. Save files are 50-150 KB. */
#ifndef SAVE_H
#define SAVE_H
#include "common.h"

#define SAVE_MAGIC    "KAUP"
#define SAVE_VERSION  1
#define SAVE_SLOTS    8
#define SAVE_AUTOS    3                    /* yearly autosave rotation AUTO1 (newest) .. AUTO3 */
#define SAVE_THUMB    64                   /* thumbnail: 64x64 cells, one THUMB_* class each */

typedef struct SaveFile SaveFile;          /* opaque (save.cpp): in-memory buffer + cursor + error flag */

typedef struct {                           /* "META" chunk, readable without loading the game */
    char president[NAME_LEN];
    char town[NAME_LEN];
    s32  year, month;
    s32  population;
    Money money;
    u32  seed;
    s32  cam_x, cam_y, zoom;               /* camera centre tile + zoom (game.cpp restores it) */
    /* T10 additions (filled by save_meta_from_game; the caller only sets the camera) */
    u32  saved_time;                       /* time() when written (load menu: "saved ..."), 0 = unknown */
    s32  town_status;                      /* TS_* */
    s32  start_year;
    s32  map_w, map_h;
} SaveMeta;

/* ---- whole-game save/load (game.cpp / menu.cpp call these) */
int  save_game(const char *path, const SaveMeta *meta);  /* 1 ok; writes to path.tmp then renames.
                                                            meta may be NULL; only its camera fields are
                                                            used, the rest comes from the game */
int  load_game(const char *path, SaveMeta *meta_out);    /* 1 ok; on failure the old game is untouched
                                                            only if the file failed validation early */
int  save_read_meta(const char *path, SaveMeta *out);    /* 1 ok (load menu) */
void save_slot_path(int slot, char *buf, int size);      /* slot 0..7 -> "SAVE1.KAU" .. "SAVE8.KAU", prefixed
                                                            with the save_set_dir() directory (absolute
                                                            then, so data_path() leaves it unchanged) */
void save_auto_path(int n, char *buf, int size);         /* yearly autosave rotation 0..2 -> AUTO1..3.KAU
                                                            (0 = newest) */
void save_quick_path(char *buf, int size);               /* QUICK.KAU */

/* ---- chunk writer (module *_save functions) */
int  save_begin_chunk(SaveFile *sf, const char *tag4, int version);
void save_end_chunk(SaveFile *sf);
void save_bytes(SaveFile *sf, const void *p, int n);
void save_rle(SaveFile *sf, const void *p, int n);       /* byte-run RLE */
void save_u8(SaveFile *sf, u32 v);
void save_u16(SaveFile *sf, u32 v);
void save_u32(SaveFile *sf, u32 v);
void save_s32(SaveFile *sf, s32 v);
void save_str(SaveFile *sf, const char *s);              /* u16 length + bytes */

/* ---- chunk reader (module *_load functions). Reads past the chunk end set the error flag and
   return zeros, so loaders don't need checks on every call; check load_ok() at the end. */
int  load_find_chunk(SaveFile *sf, const char *tag4, int *version); /* 1 = positioned at its data */
int  load_bytes(SaveFile *sf, void *p, int n);
int  load_rle(SaveFile *sf, void *p, int n);
u32  load_u8(SaveFile *sf);
u32  load_u16(SaveFile *sf);
u32  load_u32(SaveFile *sf);
s32  load_s32(SaveFile *sf);
void load_str(SaveFile *sf, char *buf, int size);
int  load_ok(SaveFile *sf);

/* ---- in-memory save (tests: round trip without files; undo snapshots if ever needed) */
SaveFile *save_mem_open(void);             /* new empty buffer */
SaveFile *save_mem_reopen(SaveFile *sf);   /* rewind for reading (same object) */
void      save_mem_close(SaveFile *sf);
int       save_mem_size(SaveFile *sf);
int  save_game_to(SaveFile *sf, const SaveMeta *meta);   /* save_game without the file */
int  load_game_from(SaveFile *sf, SaveMeta *meta_out);

/* ---- T10 additions */
void save_set_dir(const char *dir);        /* directory for slot/auto/quick paths ("" = current dir);
                                              menu_init() sets g_cfg.exe_dir */
void save_meta_from_game(SaveMeta *m);     /* president, town, date, population, money, seed, status,
                                              time from the running game (camera fields untouched) */
int  save_autosave(const SaveMeta *meta);  /* rotate AUTO3 <- AUTO2 <- AUTO1, write AUTO1 (on SE_YEAR) */
int  save_quicksave(const SaveMeta *meta); /* QUICK.KAU (F5) */
const char *save_last_error(void);         /* why the last load failed ("save file is corrupt ...") */
int  save_file_exists(const char *path);
/* Load-menu list: index 0..7 slots, 8 quicksave, 9..11 autosaves (newest first). */
#define SAVE_LIST_COUNT (SAVE_SLOTS + 1 + SAVE_AUTOS)
void save_any_path(int idx, char *buf, int size);
/* Thumbnail (chunk "THMB", right after META): one class per cell, 64x64, map scaled to fit. */
enum { THUMB_NONE, THUMB_GRASS, THUMB_FOREST, THUMB_ROCK, THUMB_FIELD, THUMB_WATER, THUMB_ROAD, THUMB_RAIL,
       THUMB_HOUSES, THUMB_APARTMENTS, THUMB_COMMERCIAL, THUMB_INDUSTRY, THUMB_CIVIC, THUMB_ZONED,
       THUMB_COUNT };
int  save_read_thumb(const char *path, u8 *out);   /* out[SAVE_THUMB * SAVE_THUMB]; 1 ok */
void save_make_thumb(u8 *out);             /* from the current world */
/* raw buffer access (tests: byte-exact comparisons, corruption) */
const u8 *save_mem_data(SaveFile *sf);
SaveFile *save_mem_from(const void *data, int n);   /* copy of a buffer, positioned for reading */
int  save_write_file(SaveFile *sf, const char *path);
/* -test save (VM) and Mac test: town, round trip, determinism, slot file, corrupt files; logs PASS/FAIL
   lines; returns the number of failures. Writes and deletes SAVE8.KAU in the save dir. */
int  save_selftest(void);

#endif
