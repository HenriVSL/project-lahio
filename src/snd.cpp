/* snd.cpp - DirectSound effects + ambient loops (DESIGN 10). Owner: T07.
   Adapted from SIROCCO's src/snd.cpp (../sandline, T07 there): WAV parser, static master buffers,
   duplicated voice pool with per-sample stealing, dB math. Changes for KAUPUNKI:
   - dsound.dll is loaded at run time (LoadLibrary + GetProcAddress("DirectSoundCreate")); no import, so
     a PC without DirectSound/sound card just runs silently. No PlaySound fallback (banned: blocks 5-9 ms).
   - Ambient layers: one looping static buffer each; snd_tick glides the audible volume towards the target
     set by snd_ambient (linear amplitude, AMB_RAMP_S for the full range) and stops a layer at 0.
   - Integer vol100 API, pan from pan100 / screen x, distance in tiles; group volumes sfx/ambient from INI.

   Voice pool: SND_VOICES slots, each holds a DuplicateSoundBuffer() copy of one sample's master buffer.
   play(id): reuse a finished voice of the same sample; if SND_PER_SAMPLE voices of it are busy steal the
   oldest of those; else take a free slot (re-duplicating if it held another sample); else steal the oldest
   voice. "Busy" is estimated with timeGetTime() + sample length (no COM call per voice per play); the
   self-test cross-checks the estimate with IDirectSoundBuffer::GetStatus. */
#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>
#include <math.h>
#include "common.h"
#include "snd.h"
#include "config.h"
#include "util.h"

#define SND_VOICES      16
#define SND_PER_SAMPLE  3
#define AMB_RAMP_S      1.5f        /* seconds for a 0 -> 100 glide */
#define DIST_FULL       8           /* tiles: full volume within */
#define DIST_MAX        48          /* tiles: silent from */
#define PAN_MAX         2000        /* DS pan units (1/100 dB) at pan100 = +-100 */
#define VOL_MIN_DB      (-5000)     /* quieter than this = skip the play */
#define NUM_SAMPLES     (SND_COUNT + AMB_COUNT)

struct SndInfo { const char *file; int base; };       /* base volume 0..100 */
/* order = SND_* then AMB_* (snd.h) = SOUNDS + LOOPS in tools/make_sfx.py. Base volumes balance the
   peak-normalised WAVs: UI ticks quiet, big events louder, bells softened (sfx/SFX.md). */
static const SndInfo s_info[NUM_SAMPLES] = {
    { "click",      45 }, { "key",       60 }, { "panel_open", 45 }, { "panel_close", 45 },
    { "build",      80 }, { "road",      65 }, { "bulldoze",   75 }, { "cash",        60 },
    { "chime",      50 }, { "warn",      60 }, { "type",       60 }, { "stamp",       75 },
    { "firebell",   55 }, { "paper",     60 }, { "fanfare",    60 }, { "deny",        45 },
    { "amb_birds",  70 }, { "amb_traffic", 70 }, { "amb_industry", 55 }
};

struct Sample {
    unsigned char      *file;       /* whole .wav image (kept for buffer restore) */
    int                 fileLen;
    unsigned char      *pcm;        /* points into file */
    int                 pcmLen;
    WAVEFORMATEX        wf;
    int                 lenMs;
    LPDIRECTSOUNDBUFFER master;     /* effects: never played itself; ambient: the looping buffer */
};
struct Voice {
    LPDIRECTSOUNDBUFFER buf;
    int                 smp;        /* sample id or -1 */
    DWORD               endMs;      /* timeGetTime() when it is expected to be finished */
    unsigned            order;      /* start counter (oldest = smallest) */
};
struct Amb {
    float target, cur;              /* 0..1 linear amplitude (before base/master/group) */
    int   playing;                  /* Play(LOOPING) issued and not stopped */
    int   dsVol;                    /* last SetVolume value (avoid redundant calls) */
};

typedef HRESULT (WINAPI *DSCreateFn)(void *guid, LPDIRECTSOUND *ds, IUnknown *outer);

static HMODULE             s_dll;
static DSCreateFn          s_create;
static LPDIRECTSOUND       s_ds;
static HWND                s_hwnd;
static int                 s_on;                /* DirectSound running */
static Sample              s_smp[NUM_SAMPLES];
static Voice               s_voice[SND_VOICES];
static Amb                 s_amb[AMB_COUNT];
static int                 s_loaded, s_nMissing;
static int                 s_master = 80;
static int                 s_group[SND_GROUP_COUNT] = { 100, 70 };
static unsigned            s_order;
static DWORD               s_lastTick, s_lastCheck;
static const char         *s_sfxDir = "sfx";
static int                 s_testNoDll;         /* self-test: pretend dsound.dll is missing */
/* statistics (self-test, log) */
static int                 s_nPlays, s_nSteals, s_nDups, s_nErrors, s_nSkipped, s_nRestores;
static int                 s_lastVoice = -1;    /* voice used by the last play (self-test) */

/* ================================================================ helpers */
static const char *hr_str(HRESULT hr)
{
    static char buf[32];
    switch (hr) {
    case DS_OK:                  return "DS_OK";
    case DSERR_ALLOCATED:        return "DSERR_ALLOCATED";
    case DSERR_BADFORMAT:        return "DSERR_BADFORMAT";
    case DSERR_BUFFERLOST:       return "DSERR_BUFFERLOST";
    case DSERR_CONTROLUNAVAIL:   return "DSERR_CONTROLUNAVAIL";
    case DSERR_INVALIDCALL:      return "DSERR_INVALIDCALL";
    case DSERR_INVALIDPARAM:     return "DSERR_INVALIDPARAM";
    case DSERR_NODRIVER:         return "DSERR_NODRIVER";
    case DSERR_OUTOFMEMORY:      return "DSERR_OUTOFMEMORY";
    case DSERR_PRIOLEVELNEEDED:  return "DSERR_PRIOLEVELNEEDED";
    case DSERR_UNSUPPORTED:      return "DSERR_UNSUPPORTED";
    }
    str_printf(buf, sizeof(buf), "0x%08lX", (unsigned long)hr);
    return buf;
}

static int rd16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static int rd32(const unsigned char *p) { return (int)(p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24)); }

/* Chunk-walking RIFF/WAVE parser: PCM, 1-2 channels, 8/16 bit. Returns 1 if ok. */
static int wav_parse(Sample *s, const char *name)
{
    const unsigned char *p = s->file, *end = s->file + s->fileLen;
    int haveFmt = 0, len;
    if (s->fileLen < 12 || memcmp(p, "RIFF", 4) || memcmp(p + 8, "WAVE", 4)) {
        log_printf("snd: %s: not a RIFF/WAVE file", name);
        return 0;
    }
    p += 12;
    s->pcm = NULL;
    while (p + 8 <= end) {
        len = rd32(p + 4);
        if (len < 0 || p + 8 + len > end) len = (int)(end - p - 8);   /* truncated: take what is there */
        if (!memcmp(p, "fmt ", 4) && len >= 16) {
            memset(&s->wf, 0, sizeof(s->wf));
            s->wf.wFormatTag      = (WORD)rd16(p + 8);
            s->wf.nChannels       = (WORD)rd16(p + 10);
            s->wf.nSamplesPerSec  = (DWORD)rd32(p + 12);
            s->wf.wBitsPerSample  = (WORD)rd16(p + 22);
            s->wf.nBlockAlign     = (WORD)(s->wf.nChannels * s->wf.wBitsPerSample / 8);
            s->wf.nAvgBytesPerSec = s->wf.nSamplesPerSec * s->wf.nBlockAlign;
            haveFmt = 1;
        } else if (!memcmp(p, "data", 4)) {
            s->pcm = (unsigned char *)p + 8;
            s->pcmLen = len;
        }
        p += 8 + len + (len & 1);
    }
    if (!haveFmt || !s->pcm) { log_printf("snd: %s: missing fmt or data chunk", name); return 0; }
    if (s->wf.wFormatTag != WAVE_FORMAT_PCM || s->wf.nChannels < 1 || s->wf.nChannels > 2 ||
        (s->wf.wBitsPerSample != 8 && s->wf.wBitsPerSample != 16) || s->wf.nSamplesPerSec < 4000) {
        log_printf("snd: %s: unsupported format (tag %d, %d ch, %d bit, %lu Hz)", name, s->wf.wFormatTag,
                   s->wf.nChannels, s->wf.wBitsPerSample, (unsigned long)s->wf.nSamplesPerSec);
        return 0;
    }
    s->pcmLen -= s->pcmLen % s->wf.nBlockAlign;
    if (s->pcmLen <= 0) { log_printf("snd: %s: empty data chunk", name); return 0; }
    s->lenMs = (int)((double)s->pcmLen * 1000.0 / s->wf.nAvgBytesPerSec);
    return 1;
}

static int wav_load(int id)
{
    char rel[PATH_LEN];
    FILE *f;
    long n;
    Sample *s = &s_smp[id];
    str_printf(rel, sizeof(rel), "%s\\%s.wav", s_sfxDir, s_info[id].file);
    f = fopen(data_path(rel), "rb");
    if (!f) { if (s_nMissing++ < 3) log_printf("snd: cannot open %s", rel); return 0; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 4 * 1024 * 1024) { fclose(f); log_printf("snd: %s: bad size %ld", rel, n); return 0; }
    s->file = (unsigned char *)malloc(n);
    if (!s->file) { fclose(f); return 0; }
    s->fileLen = (int)fread(s->file, 1, n, f);
    fclose(f);
    if (!wav_parse(s, rel)) { free(s->file); s->file = NULL; return 0; }
    return 1;
}

/* copy the PCM into a (master) buffer */
static int fill_buffer(LPDIRECTSOUNDBUFFER b, const Sample *s)
{
    void *p1, *p2;
    DWORD n1, n2;
    HRESULT hr = b->Lock(0, s->pcmLen, &p1, &n1, &p2, &n2, 0);
    if (hr == DSERR_BUFFERLOST) { b->Restore(); hr = b->Lock(0, s->pcmLen, &p1, &n1, &p2, &n2, 0); }
    if (hr != DS_OK) { log_printf("snd: Lock failed: %s", hr_str(hr)); return 0; }
    memcpy(p1, s->pcm, n1);
    if (p2 && n2) memcpy(p2, s->pcm + n1, n2);
    b->Unlock(p1, n1, p2, n2);
    return 1;
}

static LPDIRECTSOUNDBUFFER make_buffer(const Sample *s)
{
    DSBUFFERDESC d;
    LPDIRECTSOUNDBUFFER b = NULL;
    HRESULT hr;
    WAVEFORMATEX wf = s->wf;
    memset(&d, 0, sizeof(d));
    d.dwSize = sizeof(d);
    d.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPAN | DSBCAPS_STATIC | DSBCAPS_GETCURRENTPOSITION2;
    d.dwBufferBytes = s->pcmLen;
    d.lpwfxFormat = &wf;
    hr = s_ds->CreateSoundBuffer(&d, &b, NULL);
    if (hr != DS_OK) { log_printf("snd: CreateSoundBuffer failed: %s", hr_str(hr)); return NULL; }
    if (!fill_buffer(b, s)) { b->Release(); return NULL; }
    return b;
}

static void restore_master(int id)
{
    if (!s_smp[id].master) return;
    s_smp[id].master->Restore();
    fill_buffer(s_smp[id].master, &s_smp[id]);
    s_nRestores++;
}

static void voice_free(Voice *v)
{
    if (v->buf) { v->buf->Stop(); v->buf->Release(); }
    v->buf = NULL; v->smp = -1; v->endMs = 0; v->order = 0;
}

/* give voice v a buffer for sample id */
static int voice_bind(Voice *v, int id)
{
    HRESULT hr;
    static int warned;
    if (v->buf && v->smp == id) return 1;
    voice_free(v);
    hr = s_ds->DuplicateSoundBuffer(s_smp[id].master, &v->buf);
    if (hr == DSERR_BUFFERLOST) { restore_master(id); hr = s_ds->DuplicateSoundBuffer(s_smp[id].master, &v->buf); }
    if (hr != DS_OK) {
        if (!warned) { log_printf("snd: DuplicateSoundBuffer failed (%s), using copies", hr_str(hr)); warned = 1; }
        v->buf = make_buffer(&s_smp[id]);
        if (!v->buf) { s_nErrors++; return 0; }
    }
    s_nDups++;
    v->smp = id;
    return 1;
}

/* ================================================================ volume / pan math */
/* linear factor 0..1 -> DirectSound hundredths of dB (0 = full, DSBVOLUME_MIN = silent) */
static int ds_volume(float vol)
{
    double db;
    if (vol <= 0.0001f) return DSBVOLUME_MIN;
    if (vol >= 1.0f) return 0;
    db = 2000.0 * log10((double)vol);
    if (db < DSBVOLUME_MIN) db = DSBVOLUME_MIN;
    return (int)db;
}

/* effect volume factor: base * vol100 * master * sfx group * distance */
static float fx_factor(int id, int vol100, int dist)
{
    float f = s_info[id].base * 0.01f * ICLAMP(vol100, 0, 100) * 0.01f * s_master * 0.01f *
              s_group[SND_GROUP_SFX] * 0.01f;
    if (dist > DIST_FULL) {
        if (dist >= DIST_MAX) return 0.0f;
        f *= (float)(DIST_MAX - dist) / (float)(DIST_MAX - DIST_FULL);
    }
    return f;
}

static int ds_pan(int pan100) { return ICLAMP(pan100, -100, 100) * PAN_MAX / 100; }

static float amb_factor(int layer, float cur)
{
    return s_info[SND_COUNT + layer].base * 0.01f * cur * s_master * 0.01f * s_group[SND_GROUP_AMBIENT] * 0.01f;
}

/* ================================================================ effects */
static void play_ds(int id, int dsVol, int pan)
{
    DWORD now = timeGetTime();
    int i, best = -1, same = 0, oldestSame = -1, freeSlot = -1, oldest = -1;
    Voice *vc;
    HRESULT hr;

    for (i = 0; i < SND_VOICES; i++) {
        Voice *t = &s_voice[i];
        int busy = t->buf && (int)(t->endMs - now) > 0;
        if (t->smp == id && t->buf) {
            if (!busy) { if (best < 0) best = i; }
            else { same++; if (oldestSame < 0 || t->order < s_voice[oldestSame].order) oldestSame = i; }
        } else if (!busy) {
            if (freeSlot < 0 || !t->buf) freeSlot = i;      /* prefer empty slots */
        }
        if (busy && (oldest < 0 || t->order < s_voice[oldest].order)) oldest = i;
    }
    if (best < 0) {
        if (same >= SND_PER_SAMPLE) { best = oldestSame; s_nSteals++; }
        else if (freeSlot >= 0) best = freeSlot;
        else { best = oldest; s_nSteals++; }
    }
    if (best < 0) return;
    vc = &s_voice[best];
    if (!voice_bind(vc, id)) return;
    vc->buf->Stop();
    vc->buf->SetCurrentPosition(0);
    vc->buf->SetVolume(dsVol);
    vc->buf->SetPan(pan);
    hr = vc->buf->Play(0, 0, 0);
    if (hr == DSERR_BUFFERLOST) {
        restore_master(id);
        voice_free(vc);
        if (voice_bind(vc, id)) {
            vc->buf->SetVolume(dsVol);
            vc->buf->SetPan(pan);
            hr = vc->buf->Play(0, 0, 0);
        }
    }
    if (hr != DS_OK) {
        if (s_nErrors < 20) log_printf("snd: Play(%s) failed: %s", s_info[id].file, hr_str(hr));
        s_nErrors++;
        return;
    }
    vc->endMs = now + (DWORD)s_smp[id].lenMs + 20;
    vc->order = ++s_order;
    s_lastVoice = best;
    s_nPlays++;
}

static void play_raw(int id, int vol100, int pan, int dist)
{
    int v;
    if (!s_on || id < 0 || id >= SND_COUNT || !s_smp[id].master) return;
    v = ds_volume(fx_factor(id, vol100, dist));
    if (v <= VOL_MIN_DB) { s_nSkipped++; return; }
    play_ds(id, v, pan);
}

void snd_play(int id, int vol100) { play_raw(id, vol100, 0, 0); }

void snd_play_at(int id, int vol100, int pan100, int dist_tiles)
{
    play_raw(id, vol100, ds_pan(pan100), dist_tiles);
}

void snd_play_sx(int id, int vol100, int screen_x, int screen_w)
{
    int pan100 = 0;
    if (screen_w > 0) pan100 = (2 * ICLAMP(screen_x, 0, screen_w) - screen_w) * 60 / screen_w;
    play_raw(id, vol100, ds_pan(pan100), 0);
}

const char *snd_name(int id) { return (id >= 0 && id < NUM_SAMPLES) ? s_info[id].file : "?"; }
int  snd_active(void) { return s_on; }

/* ================================================================ ambient */
static void amb_apply(int layer, int force)
{
    Amb *a = &s_amb[layer];
    LPDIRECTSOUNDBUFFER b = s_smp[SND_COUNT + layer].master;
    int v;
    HRESULT hr;
    if (!b) return;
    v = a->cur > 0.0f ? ds_volume(amb_factor(layer, a->cur)) : DSBVOLUME_MIN;
    if (a->cur <= 0.0f && a->target <= 0.0f) {          /* faded out: stop (keeps the loop position) */
        if (a->playing) { b->Stop(); a->playing = 0; }
        a->dsVol = DSBVOLUME_MIN;
        return;
    }
    if (force || v != a->dsVol) { b->SetVolume(v); a->dsVol = v; }
    if (!a->playing) {
        hr = b->Play(0, 0, DSBPLAY_LOOPING);
        if (hr == DSERR_BUFFERLOST) { restore_master(SND_COUNT + layer); b->SetVolume(v); hr = b->Play(0, 0, DSBPLAY_LOOPING); }
        if (hr != DS_OK) {
            if (s_nErrors < 20) log_printf("snd: Play(%s, LOOPING) failed: %s", s_info[SND_COUNT + layer].file, hr_str(hr));
            s_nErrors++;
            a->target = a->cur = 0.0f;                  /* don't retry every frame */
            return;
        }
        a->playing = 1;
    }
}

void snd_ambient(int layer, int vol100)
{
    if (layer < 0 || layer >= AMB_COUNT) return;
    s_amb[layer].target = ICLAMP(vol100, 0, 100) * 0.01f;
}

void snd_set_ambient(int birds, int traffic, int industry)
{
    snd_ambient(AMB_BIRDS, birds);
    snd_ambient(AMB_TRAFFIC, traffic);
    snd_ambient(AMB_INDUSTRY, industry);
}

static void amb_step(float dt)
{
    int i;
    float step = dt / AMB_RAMP_S;
    for (i = 0; i < AMB_COUNT; i++) {
        Amb *a = &s_amb[i];
        if (a->cur == a->target && a->playing == (a->cur > 0.0f)) continue;    /* idle: no COM calls */
        if (a->cur < a->target) { a->cur += step; if (a->cur > a->target) a->cur = a->target; }
        else if (a->cur > a->target) { a->cur -= step; if (a->cur < a->target) a->cur = a->target; }
        amb_apply(i, 0);
    }
}

void snd_tick(void)
{
    DWORD now;
    float dt;
    int i;
    if (!s_on) return;
    now = timeGetTime();
    dt = (now - s_lastTick) * 0.001f;
    if (dt > 0.25f) dt = 0.25f;                         /* a long stall does not jump the fade */
    s_lastTick = now;
    amb_step(dt);
    if (now - s_lastCheck > 1000) {                     /* once a second: lost looping buffers */
        s_lastCheck = now;
        for (i = 0; i < AMB_COUNT; i++) {
            DWORD st = 0;
            LPDIRECTSOUNDBUFFER b = s_smp[SND_COUNT + i].master;
            if (!b || !s_amb[i].playing) continue;
            if (b->GetStatus(&st) == DS_OK && (st & DSBSTATUS_BUFFERLOST)) {
                restore_master(SND_COUNT + i);
                s_amb[i].playing = 0;
                amb_apply(i, 1);
            }
        }
    }
}

/* ================================================================ volumes */
static void amb_reapply_all(void)
{
    int i;
    if (!s_on) return;
    for (i = 0; i < AMB_COUNT; i++) if (s_amb[i].playing) amb_apply(i, 1);
}

void snd_set_volume(int vol100) { s_master = ICLAMP(vol100, 0, 100); amb_reapply_all(); }

void snd_set_group_volume(int group, int vol100)
{
    if (group < 0 || group >= SND_GROUP_COUNT) return;
    s_group[group] = ICLAMP(vol100, 0, 100);
    amb_reapply_all();
}

int snd_group_volume(int group) { return (group >= 0 && group < SND_GROUP_COUNT) ? s_group[group] : 0; }

void snd_stop_all(void)
{
    int i;
    if (!s_on) return;
    for (i = 0; i < SND_VOICES; i++) if (s_voice[i].buf) { s_voice[i].buf->Stop(); s_voice[i].endMs = 0; }
    for (i = 0; i < AMB_COUNT; i++) { s_amb[i].target = s_amb[i].cur = 0.0f; amb_apply(i, 1); }
}

/* ================================================================ init / shutdown */
static void release_all(void)
{
    int i;
    for (i = 0; i < SND_VOICES; i++) voice_free(&s_voice[i]);
    for (i = 0; i < NUM_SAMPLES; i++) {
        if (s_smp[i].master) { s_smp[i].master->Stop(); s_smp[i].master->Release(); }
        if (s_smp[i].file) free(s_smp[i].file);
        memset(&s_smp[i], 0, sizeof(s_smp[i]));
    }
    memset(s_amb, 0, sizeof(s_amb));
    if (s_ds) s_ds->Release();
    s_ds = NULL;
    s_loaded = 0;
    s_on = 0;
}

static int open_dsound(HWND hwnd)
{
    HRESULT hr;
    DSCAPS caps;
    if (!s_dll && !s_testNoDll) s_dll = LoadLibrary("dsound.dll");
    if (!s_dll || s_testNoDll) { log_printf("snd: dsound.dll not available - running silently"); return 0; }
    if (!s_create) s_create = (DSCreateFn)GetProcAddress(s_dll, "DirectSoundCreate");
    if (!s_create) { log_printf("snd: DirectSoundCreate not found in dsound.dll"); return 0; }
    hr = s_create(NULL, &s_ds, NULL);
    if (hr != DS_OK) { log_printf("snd: DirectSoundCreate failed: %s (no sound device?)", hr_str(hr)); s_ds = NULL; return 0; }
    hr = s_ds->SetCooperativeLevel(hwnd, DSSCL_NORMAL);
    if (hr != DS_OK) {
        log_printf("snd: SetCooperativeLevel failed: %s", hr_str(hr));
        s_ds->Release(); s_ds = NULL;
        return 0;
    }
    memset(&caps, 0, sizeof(caps));
    caps.dwSize = sizeof(caps);
    if (s_ds->GetCaps(&caps) == DS_OK)
        log_printf("snd: caps flags 0x%lX%s, hw mix buffers %lu, sec rate %lu..%lu Hz",
                   (unsigned long)caps.dwFlags, (caps.dwFlags & DSCAPS_EMULDRIVER) ? " (emulated driver)" : "",
                   (unsigned long)caps.dwMaxHwMixingAllBuffers,
                   (unsigned long)caps.dwMinSecondarySampleRate, (unsigned long)caps.dwMaxSecondarySampleRate);
    return 1;
}

int snd_init(void *hwnd)
{
    int i, kb = 0, nbuf = 0;
    if (s_on) snd_shutdown();
    s_hwnd = (HWND)hwnd;
    s_nMissing = 0;
    for (i = 0; i < SND_VOICES; i++) { s_voice[i].buf = NULL; s_voice[i].smp = -1; s_voice[i].endMs = 0; s_voice[i].order = 0; }
    memset(s_amb, 0, sizeof(s_amb));
    s_group[SND_GROUP_SFX] = ICLAMP((int)GetPrivateProfileInt(CONFIG_INI_SECTION, "sfx_volume", 100, config_ini_path()), 0, 100);
    s_group[SND_GROUP_AMBIENT] = ICLAMP((int)GetPrivateProfileInt(CONFIG_INI_SECTION, "ambient_volume", 70, config_ini_path()), 0, 100);

    for (i = 0; i < NUM_SAMPLES; i++) if (wav_load(i)) { s_loaded++; kb += s_smp[i].fileLen; }
    if (!s_loaded) {
        log_printf("warning: snd: no samples found in %s\\ - running silently", s_sfxDir);
        release_all();
        return 0;
    }
    log_printf("snd: %d/%d samples loaded from %s\\ (%d KB)", s_loaded, NUM_SAMPLES, s_sfxDir, kb / 1024);
    if (s_nMissing) log_printf("warning: snd: %d samples missing", s_nMissing);

    if (!open_dsound(s_hwnd)) { release_all(); return 0; }
    for (i = 0; i < NUM_SAMPLES; i++) {
        if (!s_smp[i].file) continue;
        s_smp[i].master = make_buffer(&s_smp[i]);
        if (s_smp[i].master) nbuf++;
        else log_printf("snd: %s: no buffer", s_info[i].file);
    }
    if (!nbuf) { log_printf("snd: no DirectSound buffers - running silently"); release_all(); return 0; }
    s_on = 1;
    s_lastTick = s_lastCheck = timeGetTime();
    log_printf("snd: DirectSound ok: %d buffers, %d voices, master %d, sfx %d, ambient %d", nbuf, SND_VOICES,
               s_master, s_group[SND_GROUP_SFX], s_group[SND_GROUP_AMBIENT]);
    return 1;
}

void snd_shutdown(void)
{
    release_all();
    /* dsound.dll stays loaded until exit (FreeLibrary while the mixer thread winds down is not worth the risk) */
}

/* ================================================================ self-test (-test snd) */
static int s_fail;
static void check(int ok, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    if (ok) return;
    s_fail++;
    va_start(ap, fmt);
    str_vprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    log_printf("  FAIL: %s", buf);
}

static int count_playing(int id, int *total)
{
    int i, n = 0, t = 0;
    DWORD st;
    for (i = 0; i < SND_VOICES; i++) {
        if (!s_voice[i].buf) continue;
        if (s_voice[i].buf->GetStatus(&st) == DS_OK && (st & DSBSTATUS_PLAYING)) {
            t++;
            if (s_voice[i].smp == id) n++;
        }
    }
    if (total) *total = t;
    return n;
}

static void stop_voices(void)
{
    int i;
    for (i = 0; i < SND_VOICES; i++) if (s_voice[i].buf) { s_voice[i].buf->Stop(); s_voice[i].endMs = 0; }
}

static void test_math(void)
{
    struct Case { int id, vol, pan100, dist, master, expVol, expPan; };
    /* expected by hand: f = base * vol * master * sfxgroup(100) * dist; 2000*log10(f); pan = pan100*20 */
    static const Case c[] = {
        { SND_BUILD,  100,    0,  0, 100,  -193,     0 },   /* 0.80 */
        { SND_BUILD,  100,  100,  8, 100,  -193,  2000 },   /* 0.80, full right, edge of full zone */
        { SND_BUILD,  100,  -50, 28, 100,  -795, -1000 },   /* 0.80 * 0.5 = 0.40 */
        { SND_CLICK,   50,    0,  0,  80, -1489,     0 },   /* 0.45*0.5*0.8 = 0.18 */
        { SND_BUILD,  100,    0, 48, 100, -10000,    0 },   /* at DIST_MAX: silent */
        { SND_STAMP,  100,  300,  0, 100,  -249,  2000 }    /* pan clamped */
    };
    int i, saveM = s_master, saveG = s_group[SND_GROUP_SFX], dv, pan;
    s_group[SND_GROUP_SFX] = 100;
    for (i = 0; i < COUNTOF(c); i++) {
        s_master = c[i].master;
        dv = ds_volume(fx_factor(c[i].id, c[i].vol, c[i].dist));
        pan = ds_pan(c[i].pan100);
        log_printf("  math %-10s vol %3d pan100 %4d dist %2d master %3d: DS vol %6d pan %5d (expect %6d/%5d)",
                   s_info[c[i].id].file, c[i].vol, c[i].pan100, c[i].dist, c[i].master, dv, pan, c[i].expVol, c[i].expPan);
        check(IABS(dv - c[i].expVol) <= 2 && pan == c[i].expPan, "math case %d", i);
    }
    /* screen-x pan: left edge -60%, centre 0, right edge +60% */
    {
        int pl, pc, pr;
        pl = ds_pan((2 * 0 - 800) * 60 / 800); pc = ds_pan((2 * 400 - 800) * 60 / 800); pr = ds_pan((2 * 800 - 800) * 60 / 800);
        log_printf("  math screen-x pan 0/400/800 of 800: %d %d %d (expect -1200 0 1200)", pl, pc, pr);
        check(pl == -1200 && pc == 0 && pr == 1200, "screen-x pan");
    }
    s_master = saveM;
    s_group[SND_GROUP_SFX] = saveG;
}

static void tick_for(int ms, int logEvery)
{
    DWORD t0 = timeGetTime(), lastLog = t0;
    while ((int)(timeGetTime() - t0) < ms) {
        Sleep(16);
        snd_tick();
        if (logEvery && (int)(timeGetTime() - lastLog) >= logEvery) {
            lastLog = timeGetTime();
            log_printf("    t=%4d ms  birds %.2f/%d  traffic %.2f/%d  industry %.2f/%d", (int)(lastLog - t0),
                       s_amb[0].cur, s_amb[0].dsVol, s_amb[1].cur, s_amb[1].dsVol, s_amb[2].cur, s_amb[2].dsVol);
        }
    }
}

static void test_ambient(void)
{
    int i;
    DWORD st, p0[AMB_COUNT], pc, wc;
    double t0, tickUs;
    log_printf("  ambient: all layers -> 100 (glide %.1f s)", AMB_RAMP_S);
    for (i = 0; i < AMB_COUNT; i++) {
        p0[i] = 0;
        check(s_smp[SND_COUNT + i].master != NULL, "%s: no looping buffer", s_info[SND_COUNT + i].file);
    }
    snd_set_ambient(100, 100, 100);
    tick_for(1800, 300);
    for (i = 0; i < AMB_COUNT; i++) {
        LPDIRECTSOUNDBUFFER b = s_smp[SND_COUNT + i].master;
        if (!b) continue;
        st = 0; b->GetStatus(&st);
        b->GetCurrentPosition(&p0[i], &wc);
        log_printf("  ambient %-12s cur %.2f DS vol %5d status%s%s, cursor %lu", s_info[SND_COUNT + i].file, s_amb[i].cur,
                   s_amb[i].dsVol, (st & DSBSTATUS_PLAYING) ? " PLAYING" : " stopped",
                   (st & DSBSTATUS_LOOPING) ? " LOOPING" : "", (unsigned long)p0[i]);
        check(s_amb[i].cur > 0.999f, "%s did not reach full volume", s_info[SND_COUNT + i].file);
        check((st & DSBSTATUS_PLAYING) && (st & DSBSTATUS_LOOPING), "%s not playing looped", s_info[SND_COUNT + i].file);
    }
    /* idle tick cost (nothing to ramp) */
    t0 = time_ms();
    for (i = 0; i < 100; i++) snd_tick();
    tickUs = (time_ms() - t0) * 10.0;
    log_printf("  snd_tick idle cost: %.1f us", tickUs);

    log_printf("  ambient: crossfade birds -> 20, traffic -> 60, industry -> 0");
    snd_set_ambient(20, 60, 0);
    tick_for(1800, 300);
    check(s_amb[0].cur > 0.199f && s_amb[0].cur < 0.201f, "birds glide to 0.20 (%.3f)", s_amb[0].cur);
    check(s_amb[1].cur > 0.599f && s_amb[1].cur < 0.601f, "traffic glide to 0.60 (%.3f)", s_amb[1].cur);
    check(s_amb[2].cur == 0.0f && !s_amb[2].playing, "industry stopped at 0");
    {   /* birds kept looping across its 8 s loop? cursor must have moved */
        LPDIRECTSOUNDBUFFER b = s_smp[SND_COUNT + AMB_BIRDS].master;
        if (b) {
            b->GetCurrentPosition(&pc, &wc);
            log_printf("  birds cursor %lu -> %lu (moved while ramping)", (unsigned long)p0[0], (unsigned long)pc);
            check(pc != p0[0], "birds cursor stuck");
        }
    }
    log_printf("  ambient: all -> 0");
    snd_set_ambient(0, 0, 0);
    tick_for(1600, 0);
    for (i = 0; i < AMB_COUNT; i++) {
        LPDIRECTSOUNDBUFFER b = s_smp[SND_COUNT + i].master;
        st = 0;
        if (b) b->GetStatus(&st);
        check(!(st & DSBSTATUS_PLAYING) && !s_amb[i].playing, "%s still playing after fade out", s_info[SND_COUNT + i].file);
    }
    log_printf("  ambient: all faded out and stopped");
}

int snd_selftest(void)
{
    int i, id, total, n, initHere = 0;
    DWORD st, wc, p0, pc, pmax, t0, lat;
    int everPlaying, advanced, finished;

    s_fail = 0;
    if (!s_on && !s_loaded) {                    /* -fast: main did not init sound */
        log_printf("snd_selftest: sound not initialised by main, trying with the foreground window");
        snd_init(GetForegroundWindow());
        initHere = 1;
    }
    log_printf("snd_selftest: DirectSound %s, master %d, sfx %d, ambient %d", s_on ? "on" : "OFF", s_master,
               s_group[SND_GROUP_SFX], s_group[SND_GROUP_AMBIENT]);

    /* 1. loading: every WAV, its format, duration and buffer */
    for (i = 0; i < NUM_SAMPLES; i++) {
        Sample *s = &s_smp[i];
        int isLoop = i >= SND_COUNT;
        if (s->file)
            log_printf("  load %-12s %7d bytes, %lu Hz %d bit %d ch, %5d ms, buffer %s", s_info[i].file, s->pcmLen,
                       (unsigned long)s->wf.nSamplesPerSec, s->wf.wBitsPerSample, s->wf.nChannels, s->lenMs,
                       s->master ? "created" : "-");
        check(s->file != NULL, "%s not loaded", s_info[i].file);
        if (!s->file) continue;
        check(s->wf.nSamplesPerSec == 22050 && s->wf.wBitsPerSample == 16 && s->wf.nChannels == 1,
              "%s: expected 22050 Hz 16-bit mono", s_info[i].file);
        if (isLoop) check(s->lenMs >= 4000 && s->lenMs <= 8100, "%s: loop length %d ms", s_info[i].file, s->lenMs);
        else check(s->lenMs > 20 && s->lenMs <= 2000, "%s: effect length %d ms", s_info[i].file, s->lenMs);
        if (s_on) check(s->master != NULL, "%s has no buffer", s_info[i].file);
    }
    if (!s_on) {
        log_printf("  FAIL: DirectSound not active: only the load test ran");
        s_fail++;
        goto fallback;
    }

    /* 2. math */
    test_math();

    /* 3. every effect once: Play ok, GetStatus playing, play cursor advancing */
    for (id = 0; id < SND_COUNT; id++) {
        int errs = s_nErrors, waitMs = IMIN(s_smp[id].lenMs + 60, 700);
        double c0 = time_ms(), us;
        s_lastVoice = -1;
        snd_play(id, 100);
        us = (time_ms() - c0) * 1000.0;
        check(s_lastVoice >= 0 && s_nErrors == errs, "%s: play failed", s_info[id].file);
        if (s_lastVoice < 0) { Sleep(waitMs); continue; }
        {
            LPDIRECTSOUNDBUFFER b = s_voice[s_lastVoice].buf;
            everPlaying = 0; advanced = 0; finished = 0; pmax = 0; lat = 0;
            st = 0; b->GetStatus(&st);
            if (st & DSBSTATUS_PLAYING) everPlaying = 1;
            b->GetCurrentPosition(&p0, &wc);
            t0 = timeGetTime();
            pc = p0;
            while ((int)(timeGetTime() - t0) < waitMs) {
                Sleep(10);
                if (b->GetCurrentPosition(&pc, &wc) != DS_OK) break;
                b->GetStatus(&st);
                if (st & DSBSTATUS_PLAYING) everPlaying = 1;
                if (pc != p0) { if (!advanced) lat = timeGetTime() - t0; advanced = 1; }
                if (pc > pmax) pmax = pc;
                if (!(st & DSBSTATUS_PLAYING) && everPlaying) { finished = (int)(timeGetTime() - t0); break; }
            }
            log_printf("  play %-12s %4d ms voice %2d vol %5d: %s, cursor %lu -> max %lu (first move %lu ms)%s, play() %.0f us%s",
                       s_info[id].file, s_smp[id].lenMs, s_lastVoice, ds_volume(fx_factor(id, 100, 0)),
                       everPlaying ? "PLAYING" : "never-playing", (unsigned long)p0, (unsigned long)pmax,
                       (unsigned long)lat, finished ? ", then stopped" : "", us,
                       (advanced || finished) ? "" : " STUCK");
            if (finished && !advanced) advanced = 1;     /* too short for the cursor granularity, but it ran out */
            check(everPlaying, "%s never reported DSBSTATUS_PLAYING", s_info[id].file);
            check(advanced, "%s play cursor did not advance", s_info[id].file);
            while ((int)(timeGetTime() - t0) < waitMs) Sleep(5);
        }
    }

    /* 4. estimated busy state matches GetStatus once everything ended */
    Sleep(1600);
    count_playing(-1, &total);
    log_printf("  after 1.6 s: %d voices still playing (expect 0)", total);
    check(total == 0, "%d voices still playing after all samples ended", total);

    /* 5. burst: 20 clicks in one frame -> <= SND_PER_SAMPLE voices of it, oldest stolen */
    stop_voices();
    {
        int steals0 = s_nSteals, held = 0;
        double c0 = time_ms(), us;
        for (i = 0; i < 20; i++) snd_play_sx(SND_ROAD, 80, i * 40, 800);
        us = (time_ms() - c0) * 1000.0 / 20.0;
        n = count_playing(SND_ROAD, &total);
        for (i = 0; i < SND_VOICES; i++) if (s_voice[i].smp == SND_ROAD && s_voice[i].buf) held++;
        log_printf("  burst 20x road: %d playing, %d voices hold it, %d steals, %.1f us per play", n, held,
                   s_nSteals - steals0, us);
        check(n >= 1 && n <= SND_PER_SAMPLE && held <= SND_PER_SAMPLE, "burst: %d playing / %d held", n, held);
        check(s_nSteals - steals0 == 20 - SND_PER_SAMPLE, "burst: expected %d steals, got %d", 20 - SND_PER_SAMPLE,
              s_nSteals - steals0);
    }
    /* positional: beyond DIST_MAX nothing plays */
    {
        int p0n = s_nPlays, sk0 = s_nSkipped;
        snd_play_at(SND_BUILD, 100, 0, DIST_MAX + 5);
        snd_play_at(SND_BUILD, 100, -80, 20);
        log_printf("  positional: far play skipped %d, near play started %d", s_nSkipped - sk0, s_nPlays - p0n);
        check(s_nSkipped - sk0 == 1 && s_nPlays - p0n == 1, "positional skip/play");
    }
    Sleep(700);
    stop_voices();

    /* 6. ambient layers */
    test_ambient();

    log_printf("  stats: %d plays, %d steals, %d duplicates, %d skipped (inaudible), %d restores, %d errors",
               s_nPlays, s_nSteals, s_nDups, s_nSkipped, s_nRestores, s_nErrors);
    check(s_nErrors == 0, "%d DirectSound errors", s_nErrors);

fallback:
    /* 7. silent paths: missing sfx dir, missing dsound.dll; every call must be a harmless no-op. */
    {
        int r;
        HWND hw = s_hwnd;
        snd_shutdown();
        s_sfxDir = "sfx_missing_dir";
        r = snd_init(hw);
        snd_play(SND_CLICK, 100); snd_play_at(SND_BUILD, 100, 50, 3); snd_ambient(AMB_BIRDS, 100); snd_tick();
        log_printf("  missing sfx dir: init %d active %d (silent no-ops ok)", r, s_on);
        check(r == 0 && !s_on, "missing sfx dir");
        s_sfxDir = "sfx";

        s_testNoDll = 1;
        r = snd_init(hw);
        snd_play(SND_CLICK, 100); snd_set_ambient(50, 50, 50); snd_tick(); snd_stop_all();
        log_printf("  no dsound.dll: init %d active %d (silent no-ops ok)", r, s_on);
        check(r == 0 && !s_on, "no dsound.dll");
        s_testNoDll = 0;

        if (initHere) log_printf("  (sound was initialised by the test; leaving it off)");
        else {
            r = snd_init(hw);
            log_printf("  re-init: %d active %d", r, s_on);
            check(r == 1, "re-init");
        }
    }
    log_printf("snd_selftest: %d failures", s_fail);
    return s_fail;
}
