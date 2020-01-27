#ifndef HORSE3D_AUDIO_H_
#define HORSE3D_AUDIO_H_

#include <stdint.h>

typedef struct h3daudiodevice h3daudiodevice;

typedef struct h3dsound h3dsound;


#define H3DAUDIO_BACKEND_DEFAULT 0
#define H3DAUDIO_BACKEND_SDL2 1
#define H3DAUDIO_BACKEND_SDL2_EXCLUSIVELOWLATENCY 2
#define H3DAUDIO_BACKEND_MINIAUDIO 3


h3daudiodevice *h3daudio_OpenDeviceEx(
    int samplerate, int audiobufsize,
    int backend, const char *soundcardname,
    char **error
);

h3daudiodevice *h3daudio_OpenDevice(
    char **error
);

int h3daudio_GetDeviceSoundcardCount(
    int backendtype
);

char *h3daudio_GetDeviceSoundcardName(
    int backendtype, int soundcardindex
);

void h3daudio_DestroyDevice(h3daudiodevice *dev);

const char *h3daudio_GetDeviceName(h3daudiodevice *dev);

uint64_t h3daudio_PlaySoundFromFile(
    h3daudiodevice *dev, const char *path,
    double volume, double panning, int loop
);

int h3daudio_IsSoundPlaying(
    h3daudiodevice *dev, uint64_t id
);

int h3daudio_SoundHadPlaybackError(
    h3daudiodevice *dev, uint64_t id
);

void h3daudio_ChangeSoundVolume(
    h3daudiodevice *dev, uint64_t id,
    double volume, double panning
);

int h3daudio_GetSoundVolume(
    h3daudiodevice *dev, uint64_t id,
    double *volume, double *panning
);

void h3daudio_StopSound(
    h3daudiodevice *dev, uint64_t id
);

int h3daudio_GetDeviceId(h3daudiodevice *dev);

h3daudiodevice *h3daudio_GetDeviceById(int id);

#endif  // #ifndef HORSE3D_AUDIO_H_
