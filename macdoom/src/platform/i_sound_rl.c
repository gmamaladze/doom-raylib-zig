// DOOM macOS Port - Sound System (Raylib)
//
// Ports DOOM's software mixer from the original i_sound.c and replaces
// the Linux /dev/dsp output with Raylib's AudioStream.
//
// The mixer is pure C: 8 channels, volume lookup tables, stereo panning,
// pitch stepping. We keep all of that and just change the audio output.
//

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

// DOOM headers first (before raylib, to avoid true/false conflict)
#include "z_zone.h"
#include "i_system.h"
#include "i_sound.h"
#include "m_argv.h"
#include "m_misc.h"
#include "w_wad.h"
#include "doomdef.h"
#include "doomstat.h"
#include "sounds.h"

// Resolve conflicts with raylib: true/false macros and KEY_* macros
#undef true
#undef false
#undef KEY_RIGHTARROW
#undef KEY_LEFTARROW
#undef KEY_UPARROW
#undef KEY_DOWNARROW
#undef KEY_ESCAPE
#undef KEY_ENTER
#undef KEY_TAB
#undef KEY_F1
#undef KEY_F2
#undef KEY_F3
#undef KEY_F4
#undef KEY_F5
#undef KEY_F6
#undef KEY_F7
#undef KEY_F8
#undef KEY_F9
#undef KEY_F10
#undef KEY_F11
#undef KEY_F12
#undef KEY_BACKSPACE
#undef KEY_PAUSE
#undef KEY_EQUALS
#undef KEY_MINUS
#undef KEY_RSHIFT
#undef KEY_RCTRL
#undef KEY_RALT
#undef KEY_LALT

#include "raylib.h"


//
// Sound constants
//
#define SAMPLECOUNT     512
#define NUM_CHANNELS    8
#define BUFMUL          4       // 2 bytes * 2 channels
#define MIXBUFFERSIZE   (SAMPLECOUNT*BUFMUL)
#define SAMPLERATE      11025
#define SAMPLESIZE      2       // 16-bit

//
// Sound state
//
static int              lengths[NUMSFX];
static signed short     mixbuffer[MIXBUFFERSIZE];

// Channel state
static unsigned int     channelstep[NUM_CHANNELS];
static unsigned int     channelstepremainder[NUM_CHANNELS];
static unsigned char*   channels[NUM_CHANNELS];
static unsigned char*   channelsend[NUM_CHANNELS];
static int              channelstart[NUM_CHANNELS];
static int              channelhandles[NUM_CHANNELS];
static int              channelids[NUM_CHANNELS];

// Pitch stepping lookup
static int              steptable[256];

// Volume lookup: vol_lookup[volume*256 + sample] gives scaled sample
static int              vol_lookup[128*256];

// Per-channel volume lookup pointers (into vol_lookup)
static int*             channelleftvol_lookup[NUM_CHANNELS];
static int*             channelrightvol_lookup[NUM_CHANNELS];

// Raylib audio stream
static AudioStream      audio_stream;
static int              audio_initialized = 0;


//
// Load a sound effect from WAD, pad to SAMPLECOUNT boundary.
// Returns pointer to raw sample data (after 8-byte header).
//
static void*
getsfx(char* sfxname, int* len)
{
    unsigned char*  sfx;
    unsigned char*  paddedsfx;
    int             i;
    int             size;
    int             paddedsize;
    char            name[20];
    int             sfxlump;

    sprintf(name, "ds%s", sfxname);

    // If sound doesn't exist (DOOM II sounds in shareware), use pistol
    if (W_CheckNumForName(name) == -1)
        sfxlump = W_GetNumForName("dspistol");
    else
        sfxlump = W_GetNumForName(name);

    size = W_LumpLength(sfxlump);

    sfx = (unsigned char*)W_CacheLumpNum(sfxlump, PU_STATIC);

    // Pad to SAMPLECOUNT boundary
    paddedsize = ((size - 8 + (SAMPLECOUNT - 1)) / SAMPLECOUNT) * SAMPLECOUNT;

    paddedsfx = (unsigned char*)Z_Malloc(paddedsize + 8, PU_STATIC, 0);

    memcpy(paddedsfx, sfx, size);
    for (i = size; i < paddedsize + 8; i++)
        paddedsfx[i] = 128;

    Z_Free(sfx);

    *len = paddedsize;

    // Return data after 8-byte header (format + samplerate + length)
    return (void *)(paddedsfx + 8);
}


//
// Add a sound to an internal channel. Returns handle.
//
static int
addsfx(int sfxid, int volume, int step, int seperation)
{
    static unsigned short handlenums = 0;

    int i;
    int rc = -1;
    int oldest = gametic;
    int oldestnum = 0;
    int slot;
    int rightvol;
    int leftvol;

    // Certain sounds play only one at a time (chainsaw, etc.)
    if (sfxid == sfx_sawup
        || sfxid == sfx_sawidl
        || sfxid == sfx_sawful
        || sfxid == sfx_sawhit
        || sfxid == sfx_stnmov
        || sfxid == sfx_pistol)
    {
        for (i = 0; i < NUM_CHANNELS; i++)
        {
            if (channels[i] && channelids[i] == sfxid)
            {
                channels[i] = 0;
                break;
            }
        }
    }

    // Find oldest channel or first free one
    for (i = 0; (i < NUM_CHANNELS) && channels[i]; i++)
    {
        if (channelstart[i] < oldest)
        {
            oldestnum = i;
            oldest = channelstart[i];
        }
    }

    if (i == NUM_CHANNELS)
        slot = oldestnum;
    else
        slot = i;

    // Set up the channel
    channels[slot] = (unsigned char *)S_sfx[sfxid].data;
    channelsend[slot] = channels[slot] + lengths[sfxid];

    if (!handlenums)
        handlenums = 100;

    channelhandles[slot] = rc = handlenums++;
    channelstep[slot] = step;
    channelstepremainder[slot] = 0;
    channelstart[slot] = gametic;

    // Stereo separation (range 1-256)
    seperation += 1;

    // Quadratic stereo panning
    leftvol = volume - ((volume * seperation * seperation) >> 16);
    seperation = seperation - 257;
    rightvol = volume - ((volume * seperation * seperation) >> 16);

    // Clamp
    if (rightvol < 0 || rightvol > 127)
    {
        fprintf(stderr, "rightvol out of bounds: %d\n", rightvol);
        rightvol = rightvol < 0 ? 0 : 127;
    }
    if (leftvol < 0 || leftvol > 127)
    {
        fprintf(stderr, "leftvol out of bounds: %d\n", leftvol);
        leftvol = leftvol < 0 ? 0 : 127;
    }

    // Volume lookup table pointers
    channelleftvol_lookup[slot] = &vol_lookup[leftvol * 256];
    channelrightvol_lookup[slot] = &vol_lookup[rightvol * 256];

    channelids[slot] = sfxid;

    return rc;
}


//
// I_SetChannels
// Builds pitch step table and volume lookup tables.
//
void I_SetChannels(void)
{
    int i;
    int j;
    int* steptablemid = steptable + 128;

    // Pitch step table (for pitch shifting)
    for (i = -128; i < 128; i++)
        steptablemid[i] = (int)(pow(2.0, (i / 64.0)) * 65536.0);

    // Volume lookup tables: maps (volume, unsigned_sample) → signed_scaled_sample
    for (i = 0; i < 128; i++)
        for (j = 0; j < 256; j++)
            vol_lookup[i * 256 + j] = (i * (j - 128) * 256) / 127;
}


void I_SetSfxVolume(int volume)
{
    snd_SfxVolume = volume;
}


void I_SetMusicVolume(int volume)
{
    snd_MusicVolume = volume;
}


int I_GetSfxLumpNum(sfxinfo_t* sfx)
{
    char namebuf[9];
    sprintf(namebuf, "ds%s", sfx->name);
    return W_GetNumForName(namebuf);
}


int
I_StartSound
( int       id,
  int       vol,
  int       sep,
  int       pitch,
  int       priority )
{
    priority = 0;
    id = addsfx(id, vol, steptable[pitch], sep);
    return id;
}


void I_StopSound(int handle)
{
    handle = 0;
}


int I_SoundIsPlaying(int handle)
{
    return gametic < handle;
}


//
// I_UpdateSound
// The software mixer. Iterates all active channels, reads samples,
// applies volume/pan, mixes into the stereo mixbuffer.
//
void I_UpdateSound(void)
{
    register unsigned int   sample;
    register int            dl;
    register int            dr;

    signed short*   leftout;
    signed short*   rightout;
    signed short*   leftend;
    int             step;
    int             chan;

    leftout = mixbuffer;
    rightout = mixbuffer + 1;
    step = 2;

    leftend = mixbuffer + SAMPLECOUNT * step;

    while (leftout != leftend)
    {
        dl = 0;
        dr = 0;

        for (chan = 0; chan < NUM_CHANNELS; chan++)
        {
            if (channels[chan])
            {
                sample = *channels[chan];

                dl += channelleftvol_lookup[chan][sample];
                dr += channelrightvol_lookup[chan][sample];

                channelstepremainder[chan] += channelstep[chan];
                channels[chan] += channelstepremainder[chan] >> 16;
                channelstepremainder[chan] &= 65536 - 1;

                if (channels[chan] >= channelsend[chan])
                    channels[chan] = 0;
            }
        }

        // Clamp to 16-bit range
        if (dl > 0x7fff)
            *leftout = 0x7fff;
        else if (dl < -0x8000)
            *leftout = -0x8000;
        else
            *leftout = dl;

        if (dr > 0x7fff)
            *rightout = 0x7fff;
        else if (dr < -0x8000)
            *rightout = -0x8000;
        else
            *rightout = dr;

        leftout += step;
        rightout += step;
    }
}


//
// I_SubmitSound
// Send the mixed audio buffer to Raylib's AudioStream.
//
void I_SubmitSound(void)
{
    if (!audio_initialized) return;

    if (IsAudioStreamProcessed(audio_stream))
    {
        UpdateAudioStream(audio_stream, mixbuffer, SAMPLECOUNT);
    }
}


void
I_UpdateSoundParams
( int       handle,
  int       vol,
  int       sep,
  int       pitch )
{
    // Not used in the original either
    handle = vol = sep = pitch = 0;
}


void I_ShutdownSound(void)
{
    if (audio_initialized)
    {
        StopAudioStream(audio_stream);
        UnloadAudioStream(audio_stream);
        CloseAudioDevice();
        audio_initialized = 0;
    }
}


//
// I_InitSound
// Initialize Raylib audio and pre-cache all sound effects from WAD.
//
void I_InitSound(void)
{
    int i;

    // Initialize Raylib audio subsystem
    InitAudioDevice();
    SetAudioStreamBufferSizeDefault(SAMPLECOUNT);
    audio_stream = LoadAudioStream(SAMPLERATE, 16, 2);  // 11025Hz, 16-bit, stereo
    SetAudioStreamVolume(audio_stream, 1.0f);
    PlayAudioStream(audio_stream);
    audio_initialized = 1;

    fprintf(stderr, "I_InitSound: Raylib audio initialized (%dHz, 16-bit, stereo)\n", SAMPLERATE);

    // Pre-cache all sound effects from WAD
    fprintf(stderr, "I_InitSound: pre-caching sound data...");

    for (i = 1; i < NUMSFX; i++)
    {
        if (!S_sfx[i].link)
        {
            // Load from WAD
            S_sfx[i].data = getsfx(S_sfx[i].name, &lengths[i]);
        }
        else
        {
            // Linked sound (alias) — share data
            S_sfx[i].data = S_sfx[i].link->data;
            // 64-bit fix: pointer subtraction already gives element count
            lengths[i] = lengths[S_sfx[i].link - S_sfx];
        }
    }

    fprintf(stderr, " done\n");

    // Clear mix buffer
    for (i = 0; i < MIXBUFFERSIZE; i++)
        mixbuffer[i] = 0;

    fprintf(stderr, "I_InitSound: sound module ready\n");
}


//
// MUSIC API — stubs (not implemented in original linuxdoom either)
//
void I_InitMusic(void) {}
void I_ShutdownMusic(void) {}

static int  looping = 0;
static int  musicdies = -1;

void I_PlaySong(int handle, int looping_arg)
{
    handle = looping_arg = 0;
    musicdies = gametic + TICRATE * 30;
}

void I_PauseSong(int handle) { handle = 0; }
void I_ResumeSong(int handle) { handle = 0; }

void I_StopSong(int handle)
{
    handle = 0;
    looping = 0;
    musicdies = 0;
}

void I_UnRegisterSong(int handle) { handle = 0; }

int I_RegisterSong(void* data)
{
    data = NULL;
    return 1;
}

int I_QrySongPlaying(int handle)
{
    handle = 0;
    return looping || musicdies > gametic;
}
