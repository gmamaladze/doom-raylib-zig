/// DOOM macOS Port - Sound & Music
///
/// SFX: Ports DOOM's 8-channel software mixer, outputs via Raylib AudioStream.
/// Music: Converts MUS (DOOM's music format) to MIDI in memory,
///        plays through macOS AudioToolbox's built-in DLS synthesizer.

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

#include "z_zone.h"
#include "i_system.h"
#include "i_sound.h"
#include "m_argv.h"
#include "m_misc.h"
#include "w_wad.h"
#include "doomdef.h"
#include "doomstat.h"
#include "sounds.h"

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

#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>


// ---------------------------------------------------------------------------
//  SFX - Software Mixer
// ---------------------------------------------------------------------------

#define SAMPLECOUNT     512
#define NUM_CHANNELS    8
#define BUFMUL          4
#define MIXBUFFERSIZE   (SAMPLECOUNT*BUFMUL)
#define SAMPLERATE      11025

static int              lengths[NUMSFX];
static signed short     mixbuffer[MIXBUFFERSIZE];

static unsigned int     channelstep[NUM_CHANNELS];
static unsigned int     channelstepremainder[NUM_CHANNELS];
static unsigned char*   channels[NUM_CHANNELS];
static unsigned char*   channelsend[NUM_CHANNELS];
static int              channelstart[NUM_CHANNELS];
static int              channelhandles[NUM_CHANNELS];
static int              channelids[NUM_CHANNELS];

static int              steptable[256];
static int              vol_lookup[128*256];
static int*             channelleftvol_lookup[NUM_CHANNELS];
static int*             channelrightvol_lookup[NUM_CHANNELS];

static AudioStream      audio_stream;
static int              audio_initialized = 0;


/// Load a sound effect from WAD, pad to mixer boundary.
/// WAD sounds have an 8-byte header (format + samplerate + size)
/// followed by 8-bit unsigned PCM data.
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

    if (W_CheckNumForName(name) == -1)
        sfxlump = W_GetNumForName("dspistol");
    else
        sfxlump = W_GetNumForName(name);

    size = W_LumpLength(sfxlump);
    sfx = (unsigned char*)W_CacheLumpNum(sfxlump, PU_STATIC);

    paddedsize = ((size - 8 + (SAMPLECOUNT - 1)) / SAMPLECOUNT) * SAMPLECOUNT;
    paddedsfx = (unsigned char*)Z_Malloc(paddedsize + 8, PU_STATIC, 0);

    memcpy(paddedsfx, sfx, size);
    for (i = size; i < paddedsize + 8; i++)
        paddedsfx[i] = 128;

    Z_Free(sfx);
    *len = paddedsize;
    return (void *)(paddedsfx + 8);
}


/// Assign a sound to a channel with volume and stereo separation.
/// Evicts the oldest channel if all 8 are in use.
static int
addsfx(int sfxid, int volume, int step, int seperation)
{
    static unsigned short handlenums = 0;
    int i, rc = -1, oldest = gametic, oldestnum = 0, slot;
    int rightvol, leftvol;

    if (sfxid == sfx_sawup || sfxid == sfx_sawidl || sfxid == sfx_sawful
        || sfxid == sfx_sawhit || sfxid == sfx_stnmov || sfxid == sfx_pistol)
    {
        for (i = 0; i < NUM_CHANNELS; i++)
            if (channels[i] && channelids[i] == sfxid)
                { channels[i] = 0; break; }
    }

    for (i = 0; (i < NUM_CHANNELS) && channels[i]; i++)
        if (channelstart[i] < oldest)
            { oldestnum = i; oldest = channelstart[i]; }

    slot = (i == NUM_CHANNELS) ? oldestnum : i;

    channels[slot] = (unsigned char *)S_sfx[sfxid].data;
    channelsend[slot] = channels[slot] + lengths[sfxid];

    if (!handlenums) handlenums = 100;
    channelhandles[slot] = rc = handlenums++;
    channelstep[slot] = step;
    channelstepremainder[slot] = 0;
    channelstart[slot] = gametic;

    /// Quadratic stereo panning from separation value (1-256 range)
    seperation += 1;
    leftvol = volume - ((volume * seperation * seperation) >> 16);
    seperation = seperation - 257;
    rightvol = volume - ((volume * seperation * seperation) >> 16);

    if (rightvol < 0) rightvol = 0;
    if (rightvol > 127) rightvol = 127;
    if (leftvol < 0) leftvol = 0;
    if (leftvol > 127) leftvol = 127;

    channelleftvol_lookup[slot] = &vol_lookup[leftvol * 256];
    channelrightvol_lookup[slot] = &vol_lookup[rightvol * 256];
    channelids[slot] = sfxid;

    return rc;
}


void I_SetChannels(void)
{
    int i, j;
    int* steptablemid = steptable + 128;

    for (i = -128; i < 128; i++)
        steptablemid[i] = (int)(pow(2.0, (i / 64.0)) * 65536.0);

    /// Volume lookup: maps (volume 0-127, unsigned sample 0-255)
    /// to a signed scaled sample value
    for (i = 0; i < 128; i++)
        for (j = 0; j < 256; j++)
            vol_lookup[i * 256 + j] = (i * (j - 128) * 256) / 127;
}


void I_SetSfxVolume(int volume) { snd_SfxVolume = volume; }

int I_GetSfxLumpNum(sfxinfo_t* sfx)
{
    char namebuf[9];
    sprintf(namebuf, "ds%s", sfx->name);
    return W_GetNumForName(namebuf);
}

int I_StartSound(int id, int vol, int sep, int pitch, int priority)
{
    priority = 0;
    return addsfx(id, vol, steptable[pitch], sep);
}

void I_StopSound(int handle) { handle = 0; }
int I_SoundIsPlaying(int handle) { return gametic < handle; }


/// The software mixer: iterates all 8 channels, reads samples with
/// 16.16 fixed-point stepping, applies left/right volume via lookup
/// tables, mixes into interleaved stereo 16-bit mixbuffer with clamping.
void I_UpdateSound(void)
{
    register unsigned int sample;
    register int dl, dr;
    signed short *leftout, *rightout, *leftend;
    int step, chan;

    leftout = mixbuffer;
    rightout = mixbuffer + 1;
    step = 2;
    leftend = mixbuffer + SAMPLECOUNT * step;

    while (leftout != leftend)
    {
        dl = dr = 0;

        for (chan = 0; chan < NUM_CHANNELS; chan++)
        {
            if (channels[chan])
            {
                sample = *channels[chan];
                dl += channelleftvol_lookup[chan][sample];
                dr += channelrightvol_lookup[chan][sample];
                channelstepremainder[chan] += channelstep[chan];
                channels[chan] += channelstepremainder[chan] >> 16;
                channelstepremainder[chan] &= 65535;
                if (channels[chan] >= channelsend[chan])
                    channels[chan] = 0;
            }
        }

        *leftout  = (dl > 0x7fff) ? 0x7fff : (dl < -0x8000) ? -0x8000 : dl;
        *rightout = (dr > 0x7fff) ? 0x7fff : (dr < -0x8000) ? -0x8000 : dr;
        leftout += step;
        rightout += step;
    }
}


void I_SubmitSound(void)
{
    if (!audio_initialized) return;
    if (IsAudioStreamProcessed(audio_stream))
        UpdateAudioStream(audio_stream, mixbuffer, SAMPLECOUNT);
}


void I_UpdateSoundParams(int handle, int vol, int sep, int pitch)
{
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


void I_InitSound(void)
{
    int i;

    InitAudioDevice();
    SetAudioStreamBufferSizeDefault(SAMPLECOUNT);
    audio_stream = LoadAudioStream(SAMPLERATE, 16, 2);
    SetAudioStreamVolume(audio_stream, 1.0f);
    PlayAudioStream(audio_stream);
    audio_initialized = 1;

    fprintf(stderr, "I_InitSound: %dHz 16-bit stereo\n", SAMPLERATE);

    for (i = 1; i < NUMSFX; i++)
    {
        if (!S_sfx[i].link)
        {
            S_sfx[i].data = getsfx(S_sfx[i].name, &lengths[i]);
        }
        else
        {
            S_sfx[i].data = S_sfx[i].link->data;
            lengths[i] = lengths[S_sfx[i].link - S_sfx];
        }
    }

    for (i = 0; i < MIXBUFFERSIZE; i++)
        mixbuffer[i] = 0;
}


// ---------------------------------------------------------------------------
//  Music - MUS to MIDI conversion + AudioToolbox playback
// ---------------------------------------------------------------------------

static MusicPlayer      music_player = NULL;
static MusicSequence    music_sequence = NULL;
static int              music_looping = 0;
static unsigned char*   midi_data = NULL;
static int              midi_len = 0;

/// MUS controller number to MIDI CC mapping.
/// Index 0 is special: triggers a MIDI program change instead of CC.
static const int mus_cc_to_midi[] = {
    -1, 0, 1, 7, 10, 11, 91, 93, 64, 67
};

static int write_vlq(unsigned int value, unsigned char *buf)
{
    unsigned char tmp[4];
    int count = 0;
    tmp[count++] = value & 0x7F;
    while (value >>= 7)
        tmp[count++] = (value & 0x7F) | 0x80;
    for (int i = count - 1; i >= 0; i--)
        *buf++ = tmp[i];
    return count;
}


/// Convert DOOM MUS data to Standard MIDI Format 0.
///
/// MUS is a compact MIDI variant: events encode channel in the descriptor
/// byte, volumes are "sticky" per channel, and timing uses 140 ticks/sec.
/// Channel 15 maps to MIDI drum channel 9; others are assigned dynamically.
///
/// Returns malloc'd MIDI buffer (caller frees). Sets *out_len.
static unsigned char*
mus_to_midi(const unsigned char *mus, int mus_len, int *out_len)
{
    if (mus_len < 16 || mus[0] != 'M' || mus[1] != 'U' ||
        mus[2] != 'S' || mus[3] != 0x1A)
        return NULL;

    int score_len   = mus[4] | (mus[5] << 8);
    int score_start = mus[6] | (mus[7] << 8);
    if (score_start >= mus_len)
        return NULL;

    int buf_size = mus_len * 4 + 256;
    unsigned char *out = malloc(buf_size);
    if (!out) return NULL;
    int pos = 0;
    int mpos = score_start;

    unsigned char midi_header[] = {
        'M','T','h','d',  0,0,0,6,  0,0,  0,1,  0,70
    };
    memcpy(out + pos, midi_header, 14); pos += 14;

    unsigned char trk_header[] = { 'M','T','r','k',  0,0,0,0 };
    memcpy(out + pos, trk_header, 8); pos += 8;
    int track_start = pos;

    /// Tempo: 500000 usec/quarter at 70 ticks/quarter = 140 ticks/sec
    unsigned char tempo[] = { 0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 };
    memcpy(out + pos, tempo, 7); pos += 7;

    int ch_map[16];
    memset(ch_map, -1, sizeof(ch_map));
    ch_map[15] = 9;
    int next_ch = 0;

    int ch_vol[16];
    for (int i = 0; i < 16; i++) ch_vol[i] = 127;

    unsigned int queued_delay = 0;

    while (mpos < mus_len && mpos < score_start + score_len)
    {
        unsigned char desc = mus[mpos++];
        int last       = (desc >> 7) & 1;
        int event_type = (desc >> 4) & 7;
        int mus_ch     = desc & 0x0F;

        if (event_type == 5)
            goto done;

        if (ch_map[mus_ch] == -1) {
            if (next_ch == 9) next_ch++;
            ch_map[mus_ch] = next_ch++;
        }
        int midi_ch = ch_map[mus_ch];

        if (pos + 32 > buf_size) {
            buf_size *= 2;
            out = realloc(out, buf_size);
            if (!out) return NULL;
        }

        /// Save position before writing delta. If the event produces no
        /// MIDI output (skipped system events), we rewind to avoid
        /// orphaned deltas which corrupt the MIDI stream.
        int saved_pos = pos;
        pos += write_vlq(queued_delay, out + pos);
        int wrote_event = 1;

        switch (event_type) {
        case 0: {
            int note = mus[mpos++] & 0x7F;
            out[pos++] = 0x80 | midi_ch;
            out[pos++] = note;
            out[pos++] = 0;
            break;
        }
        case 1: {
            int val = mus[mpos++];
            int note = val & 0x7F;
            int vol = ch_vol[mus_ch];
            if (val & 0x80) {
                vol = mus[mpos++] & 0x7F;
                ch_vol[mus_ch] = vol;
            }
            out[pos++] = 0x90 | midi_ch;
            out[pos++] = note;
            out[pos++] = vol;
            break;
        }
        case 2: {
            int bend = mus[mpos++];
            int midi_bend = bend * 64;
            out[pos++] = 0xE0 | midi_ch;
            out[pos++] = midi_bend & 0x7F;
            out[pos++] = (midi_bend >> 7) & 0x7F;
            break;
        }
        case 3: {
            int ctrl = mus[mpos++] & 0x7F;
            int midi_ctrl = -1;
            if (ctrl == 10) midi_ctrl = 120;
            else if (ctrl == 11) midi_ctrl = 123;
            else if (ctrl == 14) midi_ctrl = 121;
            if (midi_ctrl >= 0) {
                out[pos++] = 0xB0 | midi_ch;
                out[pos++] = midi_ctrl;
                out[pos++] = 0;
            } else {
                wrote_event = 0;
            }
            break;
        }
        case 4: {
            int mus_ctrl = mus[mpos++] & 0x7F;
            int value    = mus[mpos++] & 0x7F;
            if (mus_ctrl == 0) {
                out[pos++] = 0xC0 | midi_ch;
                out[pos++] = value;
            } else if (mus_ctrl < 10) {
                out[pos++] = 0xB0 | midi_ch;
                out[pos++] = mus_cc_to_midi[mus_ctrl];
                out[pos++] = value;
            } else {
                wrote_event = 0;
            }
            break;
        }
        default:
            wrote_event = 0;
            break;
        }

        if (!wrote_event)
            pos = saved_pos;
        else
            queued_delay = 0;

        if (last) {
            unsigned int delay = 0;
            unsigned char b;
            do {
                b = mus[mpos++];
                delay = (delay << 7) | (b & 0x7F);
            } while (b & 0x80);
            queued_delay += delay;
        }
    }

done:
    pos += write_vlq(queued_delay, out + pos);
    out[pos++] = 0xFF;
    out[pos++] = 0x2F;
    out[pos++] = 0x00;

    int track_len = pos - track_start;
    out[track_start - 4] = (track_len >> 24) & 0xFF;
    out[track_start - 3] = (track_len >> 16) & 0xFF;
    out[track_start - 2] = (track_len >> 8)  & 0xFF;
    out[track_start - 1] =  track_len        & 0xFF;

    *out_len = pos;
    return out;
}


void I_InitMusic(void) {}

void I_ShutdownMusic(void)
{
    if (music_player) {
        MusicPlayerStop(music_player);
        DisposeMusicPlayer(music_player);
        music_player = NULL;
    }
    if (music_sequence) {
        DisposeMusicSequence(music_sequence);
        music_sequence = NULL;
    }
    if (midi_data) {
        free(midi_data);
        midi_data = NULL;
    }
}

/// Set music volume by reaching into AudioToolbox's AUGraph
/// and adjusting the output AudioUnit volume.
/// Scaled to ~30% of DOOM's 0-15 range so SFX stays prominent.
void I_SetMusicVolume(int volume)
{
    snd_MusicVolume = volume;
    if (!music_sequence) return;

    AUGraph graph;
    if (MusicSequenceGetAUGraph(music_sequence, &graph) != noErr) return;

    UInt32 count;
    AUGraphGetNodeCount(graph, &count);

    for (UInt32 i = 0; i < count; i++) {
        AUNode node;
        AUGraphGetIndNode(graph, i, &node);
        AudioComponentDescription desc;
        AUGraphNodeInfo(graph, node, &desc, NULL);
        if (desc.componentType == kAudioUnitType_Output) {
            AudioUnit au;
            AUGraphNodeInfo(graph, node, NULL, &au);
            float vol = ((float)volume / 15.0f) * 0.3f;
            AudioUnitSetParameter(au, kHALOutputParam_Volume,
                                  kAudioUnitScope_Global, 0, vol, 0);
            break;
        }
    }
}

void I_PauseSong(int handle)  { if (music_player) MusicPlayerStop(music_player); }
void I_ResumeSong(int handle) { if (music_player) MusicPlayerStart(music_player); }

/// Convert MUS lump data to MIDI, load into AudioToolbox's
/// MusicSequence via temp file, prepare MusicPlayer for playback.
int I_RegisterSong(void* data)
{
    I_ShutdownMusic();

    int mus_score_len = ((unsigned char*)data)[4] | (((unsigned char*)data)[5] << 8);
    int mus_score_start = ((unsigned char*)data)[6] | (((unsigned char*)data)[7] << 8);
    int mus_total = mus_score_start + mus_score_len;

    midi_data = mus_to_midi((unsigned char*)data, mus_total, &midi_len);
    if (!midi_data) return 0;

    OSStatus err;
    err = NewMusicSequence(&music_sequence);
    if (err != noErr) return 0;

    const char* tmp_path = "/tmp/doom_music.mid";
    FILE* tmp = fopen(tmp_path, "wb");
    if (!tmp) { DisposeMusicSequence(music_sequence); music_sequence = NULL; return 0; }
    fwrite(midi_data, 1, midi_len, tmp);
    fclose(tmp);

    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault, (const UInt8*)tmp_path, strlen(tmp_path), false);
    err = MusicSequenceFileLoad(music_sequence, url, kMusicSequenceFile_MIDIType, 0);
    CFRelease(url);
    if (err != noErr) { DisposeMusicSequence(music_sequence); music_sequence = NULL; return 0; }

    err = NewMusicPlayer(&music_player);
    if (err != noErr) return 0;

    MusicPlayerSetSequence(music_player, music_sequence);
    MusicPlayerPreroll(music_player);
    I_SetMusicVolume(snd_MusicVolume);

    return 1;
}

void I_PlaySong(int handle, int looping_arg)
{
    if (!music_player) return;
    music_looping = looping_arg;

    if (looping_arg && music_sequence) {
        MusicTrack track;
        if (MusicSequenceGetIndTrack(music_sequence, 0, &track) == noErr) {
            MusicTrackLoopInfo loop_info;
            loop_info.loopDuration = 0;
            loop_info.numberOfLoops = 0;
            MusicTrackSetProperty(track, kSequenceTrackProperty_LoopInfo,
                                  &loop_info, sizeof(loop_info));
        }
    }
    MusicPlayerStart(music_player);
}

void I_StopSong(int handle)
{
    if (music_player) MusicPlayerStop(music_player);
    music_looping = 0;
}

void I_UnRegisterSong(int handle) { I_ShutdownMusic(); }

int I_QrySongPlaying(int handle)
{
    if (!music_player) return 0;
    Boolean is_playing = false;
    MusicPlayerIsPlaying(music_player, &is_playing);
    return is_playing ? 1 : 0;
}
