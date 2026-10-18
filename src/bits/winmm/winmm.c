#include <screw/bit.h>
#include <screw/sync.h>
#include <screw/unicode.h>
#include <screw/win.h>

#include <stdio.h>
#include <string.h>

#define MMSYSERR_NOERROR 0
#define MMSYSERR_ERROR 1
#define MMSYSERR_BADDEVICEID 2
#define MMSYSERR_NODRIVER 6
#define MMSYSERR_NOTSUPPORTED 8
#define TIMERR_NOERROR 0

typedef UINT MMRESULT;

typedef struct {
    UINT period_min;
    UINT period_max;
} TIMECAPS;

static DWORD WINAPI timeGetTime(void) { return (DWORD)monotonic_ms(); }

static MMRESULT WINAPI timeBeginPeriod(UINT period) { return TIMERR_NOERROR; }
static MMRESULT WINAPI timeEndPeriod(UINT period) { return TIMERR_NOERROR; }

static MMRESULT WINAPI timeGetDevCaps(TIMECAPS *caps, UINT size)
{
    if (!caps || size < sizeof *caps)
        return MMSYSERR_ERROR;
    caps->period_min = 1;
    caps->period_max = 1000000;
    return TIMERR_NOERROR;
}

static MMRESULT WINAPI timeGetSystemTime(void *mmtime, UINT size)
{
    if (mmtime && size >= 8)
        ((DWORD *)mmtime)[1] = (DWORD)monotonic_ms();
    return TIMERR_NOERROR;
}

static UINT WINAPI midiOutGetNumDevs(void) { return 0; }
static UINT WINAPI midiInGetNumDevs(void) { return 0; }
static UINT WINAPI waveOutGetNumDevs(void) { return 0; }
static UINT WINAPI waveInGetNumDevs(void) { return 0; }
static UINT WINAPI auxGetNumDevs(void) { return 0; }
static UINT WINAPI mixerGetNumDevs(void) { return 0; }

static MMRESULT WINAPI midiOutGetDevCapsA(UINT id, void *caps, UINT size) { return MMSYSERR_BADDEVICEID; }
static MMRESULT WINAPI midiOutGetDevCapsW(UINT id, void *caps, UINT size) { return MMSYSERR_BADDEVICEID; }

static MMRESULT WINAPI midiOutGetErrorTextA(MMRESULT err, LPSTR text, UINT size)
{
    if (text && size)
        snprintf(text, size, "MIDI error %u", err);
    return MMSYSERR_NOERROR;
}

static MMRESULT WINAPI midiOutGetErrorTextW(MMRESULT err, LPWSTR text, UINT size)
{
    char buf[64];
    snprintf(buf, sizeof buf, "MIDI error %u", err);
    if (text && size)
        utf8_to_utf16(buf, -1, text, size);
    return MMSYSERR_NOERROR;
}


#define STUB(name) static MMRESULT WINAPI name(void) { return MMSYSERR_NODRIVER; }
STUB(midiStreamOpen)
STUB(midiStreamClose)
STUB(midiStreamOut)
STUB(midiStreamProperty)
STUB(midiStreamRestart)
STUB(midiStreamStop)
STUB(midiStreamPause)
STUB(midiStreamPosition)
STUB(midiOutOpen)
STUB(midiOutClose)
STUB(midiOutReset)
STUB(midiOutPrepareHeader)
STUB(midiOutUnprepareHeader)
STUB(midiOutShortMsg)
STUB(midiOutLongMsg)
STUB(midiOutSetVolume)
STUB(waveOutOpen)
STUB(waveOutClose)
STUB(waveOutPrepareHeader)
STUB(waveOutUnprepareHeader)
STUB(waveOutWrite)
STUB(waveOutReset)
STUB(waveOutGetPosition)
STUB(waveOutSetVolume)
STUB(waveOutGetDevCapsA)

static BOOL WINAPI PlaySoundA(LPCSTR sound, HANDLE mod, DWORD flags) { return FALSE; }
static BOOL WINAPI PlaySoundW(LPCWSTR sound, HANDLE mod, DWORD flags) { return FALSE; }
static BOOL WINAPI sndPlaySoundA(LPCSTR sound, UINT flags) { return FALSE; }
static BOOL WINAPI sndPlaySoundW(LPCWSTR sound, UINT flags) { return FALSE; }

static UINT WINAPI mmsystemGetVersion(void) { return 0x030a; }

BIT_TABLE(winmm_exports,
          BIT_FN(timeGetTime), BIT_FN(timeBeginPeriod), BIT_FN(timeEndPeriod), BIT_FN(timeGetDevCaps),
          BIT_FN(timeGetSystemTime),
          BIT_FN(midiOutGetNumDevs), BIT_FN(midiInGetNumDevs), BIT_FN(waveOutGetNumDevs),
          BIT_FN(waveInGetNumDevs), BIT_FN(auxGetNumDevs), BIT_FN(mixerGetNumDevs),
          BIT_FN(midiOutGetDevCapsA), BIT_FN(midiOutGetDevCapsW),
          BIT_FN(midiOutGetErrorTextA), BIT_FN(midiOutGetErrorTextW),
          BIT_FN(midiStreamOpen), BIT_FN(midiStreamClose), BIT_FN(midiStreamOut), BIT_FN(midiStreamProperty),
          BIT_FN(midiStreamRestart), BIT_FN(midiStreamStop), BIT_FN(midiStreamPause), BIT_FN(midiStreamPosition),
          BIT_FN(midiOutOpen), BIT_FN(midiOutClose), BIT_FN(midiOutReset),
          BIT_FN(midiOutPrepareHeader), BIT_FN(midiOutUnprepareHeader),
          BIT_FN(midiOutShortMsg), BIT_FN(midiOutLongMsg), BIT_FN(midiOutSetVolume),
          BIT_FN(waveOutOpen), BIT_FN(waveOutClose), BIT_FN(waveOutPrepareHeader),
          BIT_FN(waveOutUnprepareHeader), BIT_FN(waveOutWrite), BIT_FN(waveOutReset),
          BIT_FN(waveOutGetPosition), BIT_FN(waveOutSetVolume), BIT_FN(waveOutGetDevCapsA),
          BIT_FN(PlaySoundA), BIT_FN(PlaySoundW), BIT_FN(sndPlaySoundA), BIT_FN(sndPlaySoundW),
          BIT_FN(mmsystemGetVersion));

BIT_DEFINE(bit_winmm, "winmm.dll", &winmm_exports);
