
#if defined(__ANDROID__) || defined(ANDROID)
#ifndef NOMINIAUDIO
#define NOMINIAUDIO
#endif
#endif

#include <assert.h>
#include <inttypes.h>
#define DR_FLAC_IMPLEMENTATION
#include <dr_libs/dr_flac.h>
#define DR_MP3_IMPLEMENTATION
#include <dr_libs/dr_mp3.h>
#define DR_WAV_IMPLEMENTATION
#include <dr_libs/dr_wav.h>
#ifndef NOMINIAUDIO
#define MINIAUDIO_IMPLEMENTATION 
#include <miniaudio/miniaudio.h>
#endif
#if defined(_WIN32) || defined(_WIN64)
#include <malloc.h>
#else
#include <alloca.h>
#endif
#include <SDL2/SDL.h>
#include <stb/stb_vorbis.c>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "audio.h"
#include "filesys.h"
#include "hash.h"
#include "threading.h"
#include "vfs.h"

//#define AUDIOBUFFERDEBUG
//#define AUDIODECODEDEBUG
#define MAXPLAYINGSOUNDS 256
#define MIXTYPE int16_t

static void h3daudio_FreeSound(h3dsound *s);


typedef struct h3dsound {
    uint64_t id;
    char *audiopath;
    int channels, input_samplerate, output_samplerate;
    int completed, completedwithfailure;
    double volume;
    double panning;
    int loop;

    _Atomic int buffer_play_index, buffer_fill_index, buffer_count;
    char *output_samplebuf;
    double resample_factor;

    int decode_endoffile;
    char *decodeaheadbuf;
    int decodeaheadbuf_fillbytes;
    char *decodeaheadbuf_resampled;
    int decodeaheadbuf_resampled_size,
        decodeaheadbuf_resampled_fillbytes;
    drmp3 *_mp3decode;
    drflac *_flacdecode;
    int _vorbiscachedsamplesbufsize,
        _vorbiscachedsamplesbuffill;
    char *_vorbiscachedsamplesbuf;
    int _vorbisprereadbufsize;
    char *_vorbisprereadbuf;
    stb_vorbis *_vorbisdecode;
    VFSFILE *vfshandle;
    int vfserror;
} h3dsound;



static int _lastdeviceid = -1;
static hashmap *audiodevice_by_id_cache = NULL;
static h3daudiodevice **audiodevice_list = NULL;
static int audiodevice_list_count = 0;
static hashset *failed_sound_id_set = NULL;

typedef struct h3daudiodevice {
    int id;
    char *name;

    int audiobufsize;
    volatile uint64_t last_sound_id;
    int channels;
    int samplerate;

    volatile _Atomic int activesoundrange;
    volatile _Atomic(h3dsound *)* playingsounds;
    mutex *_decoderandmainthreadmutex;

    volatile int decodethreadstop;
    thread *decodethread;

    #ifndef NOMINIAUDIO
    int maudiodevice_opened;
    ma_device maudiodevice;
    #endif
    int sdlaudiodevice_opened;
    SDL_AudioDeviceID sdlaudiodevice;
} h3daudiodevice;


size_t _drmp3drflac_read_cb(void* ud, void* pBufferOut, size_t bytesToRead) {
    h3dsound *s = ud;
    if (!s->vfshandle || s->vfserror)
        return 0;
    return vfs_fread(pBufferOut, 1, bytesToRead, s->vfshandle);
}

uint32_t _drmp3_seek_cb(void* ud, int offset, drmp3_seek_origin origin) {
    h3dsound *s = ud;
    if (!s->vfshandle || s->vfserror)
        return 0;
    if (origin == drmp3_seek_origin_start) {
        return vfs_fseek(s->vfshandle, offset);
    } else {
        int64_t oldoffset = vfs_ftell(s->vfshandle);
        if (oldoffset < 0)
            return 0;
        return vfs_fseek(s->vfshandle, oldoffset + offset);
    }
}

uint32_t _drflac_seek_cb(void* ud, int offset, drflac_seek_origin origin) {
    h3dsound *s = ud;
    if (!s->vfshandle || s->vfserror)
        return 0;
    if (origin == drflac_seek_origin_start) {
        return vfs_fseek(s->vfshandle, offset);
    } else {
        int64_t oldoffset = vfs_ftell(s->vfshandle);
        if (oldoffset < 0)
            return 0;
        return vfs_fseek(s->vfshandle, oldoffset + offset);
    }
}

static int h3daudio_DecodeAhead(
        h3daudiodevice *dev, h3dsound *s
        ) {
    if (s->decode_endoffile)
        return 1;
    if (s->vfserror)
        return 0;
    if (!s->vfshandle && !s->vfserror) {
        s->vfshandle = vfs_fopen(s->audiopath, "rb");
        if (!s->vfshandle) {
            s->vfserror = 1;
            return 0;
        }
    }
    if (!s->_mp3decode && !s->_flacdecode && !s->_vorbisdecode) {
        s->_mp3decode = malloc(sizeof(*s->_mp3decode));
        if (!s->_mp3decode)
            return 0;
        drmp3_config conf;
        memset(&conf, 0, sizeof(conf));
        conf.outputChannels = dev->channels;
        memset(s->_mp3decode, 0, sizeof(*s->_mp3decode));
        if (s->vfshandle) {
            if (!vfs_fseek(s->vfshandle, 0)) {
                free(s->_mp3decode);
                s->_mp3decode = NULL;
                return 0;
            }
        }
        if (drmp3_init(
                s->_mp3decode, _drmp3drflac_read_cb,
                _drmp3_seek_cb, s, &conf, NULL
                )) {
            #if defined(AUDIODECODEDEBUG) && !defined(NDEBUG)
            printf(
                "horse3d/audio.c: debug: sound id=%d mp3: "
                "opened for decoding: %s\n",
                (int)s->id,
                s->audiopath
            );
            #endif
            if (s->_mp3decode->channels != dev->channels) {
                // FIXME: mono support
                drmp3_uninit(s->_mp3decode);
                free(s->_mp3decode);
                s->_mp3decode = NULL;
                return 0;
            }
            s->input_samplerate = s->_mp3decode->sampleRate;
            if (s->input_samplerate < 10000 ||
                    s->input_samplerate > 100000) {
                drmp3_uninit(s->_mp3decode);
                free(s->_mp3decode);
                s->_mp3decode = NULL;
                return 0;
            }
            s->channels = s->_mp3decode->channels;
        } else {
            free(s->_mp3decode);
            s->_mp3decode = NULL;
        }
    }
    if (!s->_mp3decode && !s->_flacdecode && !s->_vorbisdecode) {
        if (s->vfshandle)
            if (!vfs_fseek(s->vfshandle, 0))
                return 0;
        if ((s->_flacdecode = drflac_open(
                _drmp3drflac_read_cb, _drflac_seek_cb, s, NULL
                )) != NULL) {
            #if defined(AUDIODECODEDEBUG) && !defined(NDEBUG)
            printf(
                "horse3d/audio.c: debug: sound id=%d flac: "
                "opened for decoding: %s\n",
                (int)s->id,
                s->audiopath
            );
            #endif
            if (s->_flacdecode->channels != dev->channels) {
                // FIXME: mono support
                drflac_close(s->_flacdecode);
                s->_flacdecode = NULL;
                return 0;
            }
            s->input_samplerate = s->_flacdecode->sampleRate;
            if (s->input_samplerate < 10000 ||
                    s->input_samplerate > 100000) {
                drflac_close(s->_flacdecode);
                s->_flacdecode = NULL;
                return 0;
            }
            s->channels = s->_flacdecode->channels;
        }
    }
    if (!s->_mp3decode && !s->_flacdecode && !s->_vorbisdecode) {
        if (s->vfshandle)
            if (!vfs_fseek(s->vfshandle, 0))
                return 0;
        uint64_t fsize = 0;
        if (!vfs_Size(s->audiopath, &fsize)) {
            s->vfserror = 1;
            return 0;
        }
        if (fsize <= 0) {
            s->vfserror = 1;
            return 0;
        }
        int input_size = 256;
        while (input_size < 1024 * 1024) {
            if (input_size > fsize)
                input_size = fsize;
            char *readbuf = malloc(input_size);
            if (!readbuf)
                return 0;
            if (!vfs_fseek(s->vfshandle, 0) ||
                    vfs_fread(
                        readbuf, 1, input_size, s->vfshandle
                    ) < input_size) {
                s->vfserror = 1;
                free(readbuf);
                return 0;
            }
            int consumed_bytes = 0;
            int pushdata_error = 0;
            assert(input_size > 0);
            s->_vorbisdecode = stb_vorbis_open_pushdata(
                readbuf, input_size, &consumed_bytes, &pushdata_error,
                NULL
            );
            free(readbuf);
            if (!s->_vorbisdecode) {
                if (pushdata_error != VORBIS_need_more_data ||
                        input_size >= fsize)
                    break;
                input_size *= 2; // try again with more data
                continue;
            }
            if (!vfs_fseek(s->vfshandle, consumed_bytes)) {
                s->vfserror = 1;
                stb_vorbis_close(s->_vorbisdecode);
                s->_vorbisdecode = NULL;
                return 0;
            }
            #if defined(AUDIODECODEDEBUG) && !defined(NDEBUG)
            printf(
                "horse3d/audio.c: debug: sound id=%d ogg: "
                "opened for decoding: %s\n",
                (int)s->id,
                s->audiopath
            );
            #endif
            if (s->_vorbisdecode->channels != dev->channels) {
                // FIXME: mono support
                stb_vorbis_close(s->_vorbisdecode);
                s->_vorbisdecode = NULL;
                return 0;
            }
            s->input_samplerate = s->_vorbisdecode->sample_rate;
            if (s->input_samplerate < 10000 ||
                    s->input_samplerate > 100000) {
                stb_vorbis_close(s->_vorbisdecode);
                s->_vorbisdecode = NULL;
                return 0;
            }
            s->channels = s->_vorbisdecode->channels;
            break;
        }
    }
    if (!s->_mp3decode && !s->_flacdecode && !s->_vorbisdecode) {
        s->vfserror = 1;
        return 0;
    }
    if (!s->decodeaheadbuf) {
        assert(s->input_samplerate > 0);
        assert(s->channels > 0);
        s->decodeaheadbuf = malloc(
            s->input_samplerate * sizeof(MIXTYPE) * s->channels
        );
        if (!s->decodeaheadbuf) {
            s->decodeaheadbuf_fillbytes = 0;
            return 0;
        }
    }
    int want_to_read_bytes = (
        s->input_samplerate * sizeof(MIXTYPE) * s->channels
    ) - s->decodeaheadbuf_fillbytes;
    int want_to_read_frames = (
        want_to_read_bytes / (sizeof(MIXTYPE) * s->channels)
    );
    if (want_to_read_frames <= 0) {
        return 1;
    }
    uint64_t read_frames = 0;
    if (sizeof(MIXTYPE) == sizeof(int16_t) && s->_mp3decode) {
        assert(want_to_read_bytes + s->decodeaheadbuf_fillbytes <=
               s->input_samplerate * sizeof(MIXTYPE) * s->channels);
        assert(want_to_read_frames > 0);
        assert(
            want_to_read_frames * sizeof(MIXTYPE) * s->channels +
            s->decodeaheadbuf_fillbytes <=
            s->input_samplerate * sizeof(MIXTYPE) * s->channels
        );
        read_frames = drmp3_read_pcm_frames_s16(
            s->_mp3decode, want_to_read_frames,
            (drmp3_int16 *)((char*)s->decodeaheadbuf +
            s->decodeaheadbuf_fillbytes)
        );

        #if defined(AUDIODECODEDEBUG) && !defined(NDEBUG)
        printf(
            "horse3d/audio.c: debug: sound id=%d mp3: "
            "frames=%d(%dB) fillbytes(after)=%d/%d\n",
            (int)s->id, read_frames,
            read_frames * sizeof(MIXTYPE) * s->channels,
            s->decodeaheadbuf_fillbytes +
            read_frames * sizeof(MIXTYPE) * s->channels,
            s->input_samplerate * sizeof(MIXTYPE) * s->channels
        );
        #endif
    } else if (sizeof(MIXTYPE) == sizeof(int16_t) && s->_flacdecode) {
        read_frames = drflac_read_pcm_frames_s16(
            s->_flacdecode, want_to_read_frames,
            (drflac_int16 *)((char*)s->decodeaheadbuf +
            s->decodeaheadbuf_fillbytes)
        );

        #if defined(AUDIODECODEDEBUG) && !defined(NDEBUG)
        printf(
            "horse3d/audio.c: debug: sound id=%d flac: "
            "frames=%d(%dB) fillbytes(after)=%d/%d\n",
            (int)s->id, read_frames,
            read_frames * sizeof(MIXTYPE) * s->channels,
            s->decodeaheadbuf_fillbytes +
            read_frames * sizeof(MIXTYPE) * s->channels,
            s->input_samplerate * sizeof(MIXTYPE) * s->channels
        );
        #endif
    } else if (sizeof(MIXTYPE) == sizeof(int16_t) && s->_vorbisdecode) {
        read_frames = 0;
        assert(s->channels == 2);  // FIXME: change for mono
        MIXTYPE *writeto = (MIXTYPE *)(
            ((char*)s->decodeaheadbuf +
             s->decodeaheadbuf_fillbytes)
        );
        while (read_frames < want_to_read_frames &&
                s->_vorbiscachedsamplesbuffill >= sizeof(MIXTYPE) *
                s->channels) {
            memcpy(
                writeto, s->_vorbiscachedsamplesbuf,
                sizeof(MIXTYPE) * s->channels
            );
            writeto += s->channels;
            s->_vorbiscachedsamplesbuffill -= sizeof(MIXTYPE) * s->channels;
            if (s->_vorbiscachedsamplesbuffill > 0)
                memmove(
                    s->_vorbiscachedsamplesbuf,
                    ((char *)s->_vorbiscachedsamplesbuf) +
                        sizeof(MIXTYPE) * s->channels,
                    s->_vorbiscachedsamplesbuffill
                );
            read_frames++;
            assert(
                (char *)s->decodeaheadbuf +
                read_frames * sizeof(MIXTYPE) * s->channels +
                s->decodeaheadbuf_fillbytes == (char *)writeto
            );
        }
        int input_size = s->_vorbisprereadbufsize;
        if (input_size < 1024)
            input_size = 1024;
        while (read_frames < want_to_read_frames) {
            assert(
                (char *)s->decodeaheadbuf +
                read_frames * sizeof(MIXTYPE) * s->channels +
                s->decodeaheadbuf_fillbytes == (char *)writeto
            );
            if (input_size > 1024 * 10) {
                #if defined(AUDIODECODEDEBUG) && !defined(NDEBUG)
                printf(
                    "horse3d/audio.c: warning: sound id=%d ogg: "
                    "couldn't read next packet even with pushdata size %d\n",
                    (int)s->id, (int)(input_size / 2)
                );
                #endif
                // Ok, this is unreasonable. Assume buggy file.
                goto vorbisfilefail;
            }
            char *readbuf = s->_vorbisprereadbuf;
            if (!readbuf || s->_vorbisprereadbufsize != input_size) {
                readbuf = malloc(input_size);
                if (!readbuf)
                    return 0;
                if (s->_vorbisprereadbuf)
                    free(s->_vorbisprereadbuf);
                s->_vorbisprereadbuf = readbuf;
                s->_vorbisprereadbufsize = input_size;
            }
            int64_t offset = vfs_ftell(s->vfshandle);
            if (offset < 0)
                goto vorbisfilefail;
            int result = vfs_fread(readbuf, 1, input_size, s->vfshandle);
            if ((result <= 0 || result < input_size) &&
                    !vfs_feof(s->vfshandle)) {
                vorbisfilefail:
                stb_vorbis_close(s->_vorbisdecode);
                s->_vorbisdecode = NULL;
                s->vfserror = 1;
                free(readbuf);
                return 0;
            } else if (result <= 0) {
                assert(vfs_feof(s->vfshandle));
                break;
            }
            if (!vfs_fseek(s->vfshandle, offset))
                goto vorbisfilefail;
            int channels_found = 0;
            int samples_found = 0;
            float **outputs = NULL;
            stb_vorbis_get_error(s->_vorbisdecode);  // clear error
            int bytes_used = stb_vorbis_decode_frame_pushdata(
                s->_vorbisdecode, readbuf, result,
                &channels_found, &outputs, &samples_found
            );
            int pushdata_error = stb_vorbis_get_error(s->_vorbisdecode);
            if (bytes_used == 0 && samples_found == 0) {
                if (pushdata_error != VORBIS_need_more_data) {
                    #if defined(AUDIODECODEDEBUG) && !defined(NDEBUG)
                    printf(
                        "horse3d/audio.c: warning: sound id=%d ogg: "
                        "failed with pushdata error: %d\n",
                        (int)s->id, pushdata_error
                    );
                    #endif
                    stb_vorbis_close(s->_vorbisdecode);
                    s->_vorbisdecode = NULL;
                    s->vfserror = 1;
                    free(readbuf);
                    return 0;
                }
                if (result < input_size && vfs_feof(s->vfshandle)) {
                    #if defined(AUDIODECODEDEBUG) && !defined(NDEBUG)
                    printf(
                        "horse3d/audio.c: debug: sound id=%d ogg: "
                        "end of file\n", (int)s->id
                    );
                    #endif
                    break;  // hit the maximum block already
                }
                input_size *= 2; // try again with more data
                continue;
            }
            if (samples_found == 0 && bytes_used > 0) {
                // Keep reading as per stb_vorbis documentation.
                // (Block that didn't generate data, apparently can happen)
                continue;
            }
            if (!vfs_fseek(s->vfshandle, offset + (int64_t)bytes_used))
                goto vorbisfilefail;
            if (channels_found != s->channels)
                goto vorbisfilefail;
            MIXTYPE *channelbuf = alloca(
                sizeof(*channelbuf) * channels_found
            );
            if (!channelbuf)
                goto vorbisfilefail;
            int i = 0;
            while (i < samples_found) {
                int k = 0;
                while (k < s->channels) {
                    int64_t value = outputs[k][i] * 32768.0;
                    if (value > 32767)
                        value = 32767;
                    if (value < -32768)
                        value = -32768;
                    channelbuf[k] = value;
                    k++;
                }
                k = 0;
                while (k < s->channels) {
                    if (read_frames < want_to_read_frames) {
                        assert(
                            ((char *)writeto) <
                            (char *)s->decodeaheadbuf +
                            (s->input_samplerate * sizeof(MIXTYPE) *
                             s->channels)
                        );
                        *writeto = channelbuf[k];
                        writeto++;
                        k++;
                        continue;
                    }
                    int newfill = s->_vorbiscachedsamplesbuffill +
                        sizeof(MIXTYPE);
                    if (newfill >
                            s->_vorbiscachedsamplesbufsize) {
                        char *newbuf = realloc(
                            s->_vorbiscachedsamplesbuf,
                            newfill
                        );
                        if (!newbuf)
                            goto vorbisfilefail;
                        s->_vorbiscachedsamplesbuf = newbuf;
                        s->_vorbiscachedsamplesbufsize = newfill;
                    }
                    MIXTYPE *bufptr = (MIXTYPE *)(
                        (char*)s->_vorbiscachedsamplesbuf +
                        s->_vorbiscachedsamplesbuffill
                    );
                    s->_vorbiscachedsamplesbuffill = newfill;
                    *bufptr = channelbuf[k];
                    k++;
                }
                if (read_frames < want_to_read_frames)
                    read_frames++;

                assert(
                    (char *)s->decodeaheadbuf +
                    read_frames * sizeof(MIXTYPE) * s->channels +
                    s->decodeaheadbuf_fillbytes == (char *)writeto
                );

                i++;
            }
        }
        assert((
            (char *)s->decodeaheadbuf +
            read_frames * sizeof(MIXTYPE) * s->channels +
            s->decodeaheadbuf_fillbytes == (char *)writeto
        ) && (read_frames == want_to_read_frames ||
              vfs_feof(s->vfshandle)));

        #if defined(AUDIODECODEDEBUG) && !defined(NDEBUG)
        printf(
            "horse3d/audio.c: debug: sound id=%d ogg: "
            "frames=%d(%dB) fillbytes(after)=%d/%d\n",
            (int)s->id, read_frames,
            read_frames * sizeof(MIXTYPE) * s->channels,
            s->decodeaheadbuf_fillbytes +
            read_frames * sizeof(MIXTYPE) * s->channels,
            s->input_samplerate * sizeof(MIXTYPE) * s->channels
        );
        #endif
    } else {
        fprintf(stderr, "unknown decode type");
        return 0;
    }
    if (read_frames == 0) {
        s->decode_endoffile = 1;
        if (s->_mp3decode) {
            drmp3_uninit(s->_mp3decode);
            free(s->_mp3decode);
        }
        if (s->_flacdecode) {
            drflac_close(s->_flacdecode);
            s->_flacdecode = NULL;
        }
    }
    s->decodeaheadbuf_fillbytes += (
        read_frames * sizeof(MIXTYPE) * s->channels
    );
    return 1;
}

static void h3daudio_ObtainAudioBlock(
        h3daudiodevice *dev, h3dsound *s,
        char *blockoutput
        ) {
    if (!h3daudio_DecodeAhead(
            dev, s
            )) {
        s->completedwithfailure = 1;
        s->completed = 1;
        memset(blockoutput, 0, dev->audiobufsize);
        return;
    }
    assert(s->input_samplerate > 0);
    assert(dev->audiobufsize * 2 <
           s->input_samplerate * sizeof(MIXTYPE) * s->channels);
    if (s->resample_factor == 0) {
        if (s->input_samplerate != s->output_samplerate) {
            s->resample_factor = ((double)s->output_samplerate) /
                ((double)s->input_samplerate);
        } else {
            s->resample_factor = 1;
        }
    }
    int resampling = (s->input_samplerate != s->output_samplerate);
    if (resampling && (
            !s->decodeaheadbuf_resampled ||
            s->decodeaheadbuf_resampled_fillbytes <
            dev->audiobufsize)) {
        assert(sizeof(MIXTYPE) == 2);
        if (!s->decodeaheadbuf_resampled) {
            int buf_size = (
                s->output_samplerate * sizeof(MIXTYPE) * dev->channels * 2
            );
            assert(buf_size > 0);
            while (buf_size < dev->audiobufsize * 4)
                buf_size *= 2;
            s->decodeaheadbuf_resampled_size = buf_size;
            s->decodeaheadbuf_resampled = (
                malloc(s->decodeaheadbuf_resampled_size)
            );
            if (!s->decodeaheadbuf_resampled) {
                memset(blockoutput, 0, dev->audiobufsize);
                return;
            }
        }

        while (s->decodeaheadbuf_resampled_fillbytes <
                dev->audiobufsize) {
            int unresampled_frames = (
                dev->audiobufsize / (sizeof(MIXTYPE) * dev->channels)
            ) * s->resample_factor;
            int unresampled_bytes = (
                unresampled_frames * sizeof(MIXTYPE) * dev->channels
            );
            if (unresampled_bytes <= 0)
                break;

            SDL_AudioCVT cvt;
            memset(&cvt, 0, sizeof(cvt));
            SDL_BuildAudioCVT(
                &cvt, AUDIO_S16, s->channels, s->input_samplerate,
                AUDIO_S16, s->channels, s->output_samplerate
            );
            if (cvt.len * cvt.len_mult > (
                    s->decodeaheadbuf_resampled_size -
                    s->decodeaheadbuf_resampled_fillbytes))
                break;
            cvt.len = unresampled_bytes;
            cvt.buf = (unsigned char *)s->decodeaheadbuf_resampled +
                      (unsigned int)s->decodeaheadbuf_resampled_fillbytes;
            memcpy(cvt.buf, s->decodeaheadbuf, cvt.len);
            SDL_ConvertAudio(&cvt);
            s->decodeaheadbuf_fillbytes -= cvt.len;
            if (s->decodeaheadbuf_fillbytes > 0) {
                memmove(
                    s->decodeaheadbuf,
                    s->decodeaheadbuf + cvt.len,
                    s->input_samplerate * sizeof(MIXTYPE) *
                    dev->channels - cvt.len
                );
            }
            assert(cvt.len_cvt > 0);
            s->decodeaheadbuf_resampled_fillbytes += cvt.len_cvt;
        }
    }

    // Copy over actual bytes result:
    char *next_byte_source = s->decodeaheadbuf;
    int next_block_bytes = s->decodeaheadbuf_fillbytes;
    if (resampling) {
        next_byte_source = s->decodeaheadbuf_resampled;
        next_block_bytes = s->decodeaheadbuf_resampled_fillbytes;
    }
    if (next_block_bytes > dev->audiobufsize) {
        next_block_bytes = dev->audiobufsize;
    }
    if (next_block_bytes < dev->audiobufsize)
        memset(blockoutput, 0, dev->audiobufsize);
    if (next_block_bytes > 0)
        memcpy(blockoutput, next_byte_source, next_block_bytes);
    else
        s->completed = 1;

    // Remove buffer contents we copied out:
    if (resampling) {
        s->decodeaheadbuf_resampled_fillbytes -= next_block_bytes;
        if (s->decodeaheadbuf_resampled_fillbytes > 0 &&
                next_block_bytes > 0) {
            assert(next_block_bytes < s->decodeaheadbuf_resampled_size);
            memmove(
                s->decodeaheadbuf_resampled,
                s->decodeaheadbuf_resampled + next_block_bytes,
                s->decodeaheadbuf_resampled_size - next_block_bytes
            );
        }
    } else {
        s->decodeaheadbuf_fillbytes -= next_block_bytes;
        if (s->decodeaheadbuf_fillbytes > 0 &&
                next_block_bytes > 0) {
            assert(next_block_bytes < (s->input_samplerate *
                                       sizeof(MIXTYPE) * dev->channels));
            memmove(
                s->decodeaheadbuf,
                ((char*)s->decodeaheadbuf) + next_block_bytes,
                (s->input_samplerate * sizeof(MIXTYPE) * dev->channels) -
                next_block_bytes
            );
        }
    }
}

static void h3daudio_DecodeThreadDo(void *udata) {
    h3daudiodevice *dev = (h3daudiodevice *)udata;
    if (!dev)
        return;

    int waitms = (double)(
        ((double)dev->audiobufsize /
        (sizeof(MIXTYPE) * dev->channels)) /
        ((double)dev->samplerate)
    ) * 0.5 * 1000.0;
    if (waitms < 5)
        waitms = 5;
    while (!dev->decodethreadstop) {
        mutex_Lock(dev->_decoderandmainthreadmutex);
        int didsomething = 0;
        int i = 0;
        while (i < dev->activesoundrange) {
            if (!dev->playingsounds[i] ||
                    dev->playingsounds[i]->completed) {
                i++;
                continue;
            }
            int gotbuffstofill = 0;
            int end_fill_index = (
                dev->playingsounds[i]->buffer_play_index
            ) - 1;
            const int buffs_count = dev->playingsounds[i]->buffer_count;
            if (end_fill_index < 0)
                end_fill_index += buffs_count;
            if (end_fill_index >= buffs_count)
                end_fill_index -= buffs_count;
            #if defined(AUDIOBUFFERDEBUG) && !defined(NDEBUG)
            printf("horse3d/audio.c: debug: "
                  "sound id=%" PRIu64 " play=%d/%d fill=%d/%d end=%d/%d "
                  "resampled=%s\n",
                  (uint64_t)dev->playingsounds[i]->id,
                  dev->playingsounds[i]->buffer_play_index,
                  dev->playingsounds[i]->buffer_count,
                  dev->playingsounds[i]->buffer_fill_index,
                  dev->playingsounds[i]->buffer_count,
                  end_fill_index,
                  dev->playingsounds[i]->buffer_count,
                  (dev->playingsounds[i]->output_samplerate !=
                   dev->playingsounds[i]->input_samplerate ? "yes" : "no"));
            #endif
            if (dev->playingsounds[i]->buffer_fill_index !=
                    end_fill_index) {
                gotbuffstofill = 1;
            }
            if (!gotbuffstofill) {
                i++;
                continue;
            }
            int fill_index = dev->playingsounds[i]->buffer_fill_index + 1;
            if (fill_index >= buffs_count)
                fill_index -= buffs_count;
            didsomething = 1;
            h3daudio_ObtainAudioBlock(
                dev, dev->playingsounds[i],
                ((char*)dev->playingsounds[i]->output_samplebuf) + (
                    fill_index * dev->audiobufsize
                )
            );
            dev->playingsounds[i]->buffer_fill_index = fill_index;
            i++;
        }
        mutex_Release(dev->_decoderandmainthreadmutex);
        if (!didsomething)
            SDL_Delay(waitms);
    }
}

void h3daudio_DestroyDevice(h3daudiodevice *dev) {
    if (!dev) {
        return;
    }
    uint64_t queryid = dev->id;
    hash_BytesMapUnset(
        audiodevice_by_id_cache,
        (char*)&queryid, sizeof(queryid)
    );
    if (dev->decodethread) {  // first, stop the thread.
        dev->decodethreadstop = 1;
        thread_Join(dev->decodethread);
    }
    if (dev->name)
        free(dev->name);
    #ifndef NOMINIAUDIO
    if (dev->maudiodevice_opened) {
        ma_device_uninit(&dev->maudiodevice);
    }
    #endif
    if (dev->sdlaudiodevice_opened) {
        SDL_CloseAudioDevice(dev->sdlaudiodevice);
    }
    if (dev->playingsounds) {
        int i = 0;
        while (i < MAXPLAYINGSOUNDS) {
            if (dev->playingsounds[i])
                h3daudio_FreeSound(dev->playingsounds[i]);
            i++;
        }
        free((void*)dev->playingsounds);
    }
    if (dev->_decoderandmainthreadmutex) {
        mutex_Destroy(dev->_decoderandmainthreadmutex);
    }
    int i = 0;
    while (i < (int)audiodevice_list_count) {
        if (audiodevice_list[i] == dev) {
            audiodevice_list[i] = NULL;
            int entriesfollow = 0;
            int k = i;
            while (k < (int)audiodevice_list_count) {
                if (audiodevice_list[i] != NULL) {
                    entriesfollow = 1;
                    break;
                }
                k++;
            }
            if (!entriesfollow) {
                audiodevice_list_count = i;
                break;
            }
        }
        i++;
    }
    free(dev);
}

static void h3daudio_DoMix(
        h3daudiodevice *dev, int samples, uint8_t *stream
        ) {
    memset(stream, 0, samples * sizeof(MIXTYPE) * dev->channels);
    if (samples * sizeof(MIXTYPE) * dev->channels !=
            dev->audiobufsize) {
        fprintf(
            stderr, "h3daudio.c: error: audio backend "
            "requested invalid size of %d, "
            "our audio buffer size is %d\n",
            (int)(samples * sizeof(MIXTYPE) * dev->channels),
            (int)dev->audiobufsize
        );
        fflush(stderr);
        return;
    }

    int i = 0;
    while (i < dev->activesoundrange) {
        if (!dev->playingsounds[i] || dev->playingsounds[i]->completed ||
                dev->playingsounds[i]->volume <= 0.0001 ||
                dev->playingsounds[i]->buffer_count <= 0 ||
                dev->playingsounds[i]->buffer_play_index ==
                    dev->playingsounds[i]->buffer_fill_index) {
            i++;
            continue;
        }
        if (dev->playingsounds[i]->buffer_play_index == -1) {
            if (dev->playingsounds[i]->buffer_fill_index == 0) {
                // Need to wait for content first.
                i++;
                continue;
            }
            dev->playingsounds[i]->buffer_play_index = 0;
        }
        const int bufid = dev->playingsounds[i]->buffer_play_index;
        assert(bufid >= 0 && bufid < dev->playingsounds[i]->buffer_count);
        const char *output_samplebuf = (
            ((char*)dev->playingsounds[i]->output_samplebuf) +
            bufid * dev->audiobufsize
        );
        assert(bufid * dev->audiobufsize + dev->audiobufsize <=
               dev->audiobufsize * dev->playingsounds[i]->buffer_count);
        assert(sizeof(MIXTYPE) * dev->channels * samples ==
               dev->audiobufsize);
        dev->playingsounds[i]->buffer_play_index++;
        if (dev->playingsounds[i]->buffer_play_index >=
                dev->playingsounds[i]->buffer_count) {
            dev->playingsounds[i]->buffer_play_index -=
                dev->playingsounds[i]->buffer_count;
        }
        const int _samples_bothchannels = samples * dev->channels;
        double left_factor = 1.0;
        double right_factor = 1.0;
        if (dev->playingsounds[i]->panning > 0.0)
            right_factor = 1.0 - fmax(0.0,
                dev->playingsounds[i]->panning
            );
        if (dev->playingsounds[i]->panning < 0.0)
            left_factor = 1.0 - fmax(0.0,
                -dev->playingsounds[i]->panning
            );
        const double vol_left = fmin(fmax(
            dev->playingsounds[i]->volume, 0.0
        ), 1.0) * left_factor;
        const double vol_right = fmin(fmax(
            dev->playingsounds[i]->volume, 0.0
        ), 1.0) * right_factor;
        const int64_t _volume_divider_left = (
            vol_left > 0.0001 ? ((int)(100.0 / vol_left)) : 1000000
        );
        const int64_t _volume_divider_right = (
            vol_right > 0.0001 ? ((int)(100.0 / vol_right)) : 1000000
        );
        int z = 0;
        while (z < _samples_bothchannels) {
            int c = 0;
            while (c < dev->channels) {
                int64_t new_sample_val = ((MIXTYPE*)stream)[z] + ((100 * (
                    ((int64_t)(((MIXTYPE*)output_samplebuf)
                        [z])))) /
                    (c == 0 ? _volume_divider_left : _volume_divider_right)
                );
                if (new_sample_val > 32767) {
                    new_sample_val = 32767;
                } else if (new_sample_val < -32767) {
                    new_sample_val = -32767;
                }
                ((MIXTYPE*)stream)[z] = new_sample_val;
                c++;
                z++;
            }
        }
        assert(z == samples * dev->channels);
        i++;
    }
}

void _audiocb_SDL2(void *udata, uint8_t *stream, int len) {
    h3daudiodevice *dev = (h3daudiodevice *)udata;
    if (!dev) {
        memset(stream, 0, len);
        return;
    }

    int samples = len / (sizeof(MIXTYPE) * dev->channels);
    h3daudio_DoMix(dev, samples, stream);
}

#ifndef NOMINIAUDIO
void _audiocb_miniaudio(
        ma_device* udata, void *stream, const void *input,
        ma_uint32 frames
        ) {
    h3daudiodevice *dev = (h3daudiodevice *)udata;
    if (!dev) {
        memset(stream, 0, frames * sizeof(MIXTYPE));
        return;
    }

    int samples = frames;
    h3daudio_DoMix(dev, samples, stream);
}
#endif


static int process_backendtype_default(int backendtype) {
    if (backendtype == H3DAUDIO_BACKEND_DEFAULT) {
        #ifndef NOMINIAUDIO
        backendtype = H3DAUDIO_BACKEND_MINIAUDIO;
        #else
        backendtype = H3DAUDIO_BACKEND_SDL2;
        #endif
    }
    return backendtype;
}

static int usingsdlaudiofallback = 0;

static void _set_SDL2_audiodriverhints(
        int backendtype
        ) {
    backendtype = process_backendtype_default(backendtype);
    if (backendtype == H3DAUDIO_BACKEND_SDL2_EXCLUSIVELOWLATENCY) {
        #if defined(__linux__) || defined(__LINUX__)
        SDL_SetHintWithPriority(
            "SDL_AUDIODRIVER", "jack", SDL_HINT_OVERRIDE
        );
        #elif defined(_WIN32) || defined(_WIN64)
        SDL_SetHintWithPriority(
            "SDL_AUDIODRIVER", "wasapi", SDL_HINT_OVERRIDE
        );
        #endif
    } else {
        #if defined(__linux__) || defined(__LINUX__)
        SDL_SetHintWithPriority(
            "SDL_AUDIODRIVER", "pulseaudio", SDL_HINT_OVERRIDE
        );
        #elif defined(_WIN32) || defined(_WIN64)
        SDL_SetHintWithPriority(
            "SDL_AUDIODRIVER", "directsound", SDL_HINT_OVERRIDE
        );
        #endif
    }
}

int h3daudio_GetDeviceSoundcardCount(
        int backendtype
        ) {
    backendtype = process_backendtype_default(backendtype);
    if (backendtype == H3DAUDIO_BACKEND_MINIAUDIO) {
        return 1;
    }
    _set_SDL2_audiodriverhints(backendtype);
    int result = 0;
    if ((result = SDL_GetNumAudioDevices(0)) < 0) {
        if (!usingsdlaudiofallback) {
            usingsdlaudiofallback = 1;
            #if defined(__linux__) || defined(__LINUX__)
            SDL_SetHintWithPriority(
                "SDL_AUDIODRIVER", "alsa", SDL_HINT_OVERRIDE
            );
            #elif defined(_WIN32) || defined(_WIN64)
            SDL_SetHintWithPriority(
                "SDL_AUDIODRIVER", "winmm", SDL_HINT_OVERRIDE
            );
            #endif
            if ((result = SDL_GetNumAudioDevices(0)) >= 0) {
                if (result < 0)
                    return 0;
                return result;
            }
        }
        fprintf(stderr,
            "horse3d/audio.c: warning: "
            "failed to get SDL2 audio devices\n"
        );
    }
    if (result < 0)
        result = 0;
    return result;
}

char *h3daudio_GetDeviceSoundcardName(
        int backendtype, int soundcardindex
        ) {
    backendtype = process_backendtype_default(backendtype);
    if (backendtype == H3DAUDIO_BACKEND_MINIAUDIO) {
        return strdup("default");
    }
    if (soundcardindex < 0)
        return NULL;
    _set_SDL2_audiodriverhints(backendtype);
    const char *s = SDL_GetAudioDeviceName(soundcardindex, 0);
    if (s)
        return strdup(s);
    return NULL;
}

h3daudiodevice *h3daudio_OpenDeviceEx(
        int samplerate, int audiobufsize,
        int backendtype, const char *soundcardname,
        char **error
        ) {
    *error = NULL;
    if (samplerate != 44100 && samplerate != 48000 &&
            samplerate != 22050) {
        if (error)
            *error = strdup("unsupported sample rate");
        return NULL;
    }
    backendtype = process_backendtype_default(backendtype);
    if (backendtype != H3DAUDIO_BACKEND_SDL2 &&
            backendtype != H3DAUDIO_BACKEND_SDL2_EXCLUSIVELOWLATENCY
            #ifndef NOMINIAUDIO
            && backendtype != H3DAUDIO_BACKEND_MINIAUDIO
            #endif
            ) {
        if (error)
            *error = strdup("unknown or unavailable backend");
        return NULL;
    }
    if (audiobufsize < 512)
        audiobufsize = 512;
    int mixchannels = 2;
    audiobufsize = (
        (int)(audiobufsize / (sizeof(MIXTYPE) * mixchannels))
    ) * sizeof(MIXTYPE) * mixchannels;

    h3daudiodevice *dev = malloc(sizeof(*dev));
    if (!dev) {
        if (error)
            *error = strdup("memory allocation failed");
        return NULL;
    }
    memset(dev, 0, sizeof(*dev));
    _lastdeviceid++;
    {
        int foundslot = 0;
        int i = 0;
        while (i < audiodevice_list_count) {
            if (audiodevice_list[i] == NULL) {
                audiodevice_list[i] = dev;
                foundslot = 1;
                break;
            }
            i++;
        }
        if (!foundslot) {
            h3daudiodevice **new_list = realloc(
                audiodevice_list,
                sizeof(*audiodevice_list) *
                (audiodevice_list_count + 1)
            );
            if (!new_list) {
                h3daudio_DestroyDevice(dev);
                return NULL;
            }
            audiodevice_list = new_list;
            audiodevice_list[audiodevice_list_count] = dev;
            audiodevice_list_count++;
        }
    }
    dev->name = strdup(soundcardname);
    dev->id = _lastdeviceid;
    dev->audiobufsize = audiobufsize;
    dev->samplerate = samplerate;
    dev->channels = mixchannels;
    dev->playingsounds = malloc(
        sizeof(*dev->playingsounds) * MAXPLAYINGSOUNDS
    );
    if (!dev->playingsounds) {
        if (error)
            *error = strdup("memory allocation failed");
        h3daudio_DestroyDevice(dev);
        return NULL;
    }
    dev->_decoderandmainthreadmutex = mutex_Create();
    if (!dev->_decoderandmainthreadmutex) {
        if (error)
            *error = strdup("memory allocation failed");
        h3daudio_DestroyDevice(dev);
        return NULL;
    }

    if (backendtype == H3DAUDIO_BACKEND_SDL2 ||
            backendtype == H3DAUDIO_BACKEND_SDL2_EXCLUSIVELOWLATENCY) {
        SDL_AudioSpec wanted;
        memset(&wanted, 0, sizeof(wanted));
        wanted.freq = samplerate;
        wanted.format = AUDIO_S16;
        wanted.channels = dev->channels;
        wanted.samples = audiobufsize / (sizeof(MIXTYPE) * 2);
        wanted.callback = _audiocb_SDL2;
        wanted.userdata = dev;

        _set_SDL2_audiodriverhints(backendtype);
        if (strcmp(soundcardname, "default unknown device") == 0)
            soundcardname = NULL;
        if (soundcardname) {
            int found = 0;
            int c = h3daudio_GetDeviceSoundcardCount(backendtype);
            int i = 0;
            while (i < c) {
                char *name = (
                    h3daudio_GetDeviceSoundcardName(backendtype, i)
                );
                if (!name) {
                    i++;
                    continue;
                }
                if (strcmp(soundcardname, name) == 0)
                    found = 1;
                free(name);
                i++;
            }
            if (!found) {
                h3daudio_DestroyDevice(dev);
                return NULL;
            }
        } else {
            if (h3daudio_GetDeviceSoundcardCount(backendtype) > 0) {
                h3daudio_DestroyDevice(dev);
                return NULL;
            }
        }
        SDL_AudioDeviceID sdldev = SDL_OpenAudioDevice(
            soundcardname, 0, &wanted, NULL, 0
        );
        if (sdldev <= 0) {
            char buf[512] = "";
            snprintf(
                buf, sizeof(buf) - 1,
                "failed to open audio device: %s",
                SDL_GetError()
            );
            if (error)
                *error = strdup(buf);
            h3daudio_DestroyDevice(dev);
            return NULL;
        }
        dev->sdlaudiodevice = sdldev;
        dev->sdlaudiodevice_opened = 1;
        SDL_PauseAudioDevice(sdldev, 0);
    }
    #ifndef NOMINIAUDIO
        else if (backendtype == H3DAUDIO_BACKEND_MINIAUDIO) {
        ma_device_config deviceConfig;
        ma_device device;

        deviceConfig = ma_device_config_init(ma_device_type_playback);
        deviceConfig.playback.format = ma_format_s16;
        deviceConfig.playback.channels = dev->channels;
        deviceConfig.sampleRate = samplerate;
        deviceConfig.dataCallback = _audiocb_miniaudio;
        deviceConfig.pUserData = dev;
        deviceConfig.bufferSizeInFrames = (
            audiobufsize / (sizeof(MIXTYPE) * dev->channels)
        );

        if (ma_device_init(NULL, &deviceConfig, &device) != MA_SUCCESS) {
            if (error)
                *error = strdup("failed to open audio device");
            h3daudio_DestroyDevice(dev);
            return NULL;
        }

        if (ma_device_start(&device) != MA_SUCCESS) {
            if (error)
                *error = strdup("failed to open audio device");
            ma_device_uninit(&device);
            h3daudio_DestroyDevice(dev);
            return NULL;
        }

        dev->maudiodevice = device;
        dev->maudiodevice_opened = 1;
    }
    #endif

    dev->decodethread = thread_Spawn(
        h3daudio_DecodeThreadDo, dev
    );
    if (!dev->decodethread) {
        h3daudio_DestroyDevice(dev);
        return NULL;
    }

    if (!audiodevice_by_id_cache) {
        audiodevice_by_id_cache = hash_NewBytesMap(128);
        if (!audiodevice_by_id_cache) {
            audiodeviceidcachefail: ;
            h3daudio_DestroyDevice(dev);
            return NULL;
        }
    }
    uint64_t id = dev->id;
    if (!hash_BytesMapSet(
            audiodevice_by_id_cache,
            (char*)&id, sizeof(id),
            (uint64_t)(uintptr_t)dev)) {
        goto audiodeviceidcachefail;
    }

    return dev;
}

h3daudiodevice *h3daudio_GetDeviceById(int id) {
    if (!audiodevice_by_id_cache)
        return NULL;
    uint64_t queryid = id;
    uintptr_t hashptrval = 0;
    if (!hash_BytesMapGet(
            audiodevice_by_id_cache,
            (char*)&queryid, sizeof(queryid),
            (uint64_t*)&hashptrval))
        return NULL;
    return (h3daudiodevice*)(void*)hashptrval;
}

static void h3daudio_FreeSound(h3dsound *s) {
    if (!s)
        return;
    if (s->vfshandle)
        vfs_fclose(s->vfshandle);
    if (s->vfserror) {
        if (!failed_sound_id_set)
            failed_sound_id_set = hashset_New(64);
        if (failed_sound_id_set)
            hashset_Add(failed_sound_id_set, &s->id, sizeof(s->id));
    }
    if (s->_mp3decode) {
        drmp3_uninit(s->_mp3decode);
        free(s->_mp3decode);
    }
    if (s->_flacdecode)
        drflac_close(s->_flacdecode);
    if (s->_vorbisdecode)
        stb_vorbis_close(s->_vorbisdecode);
    if (s->_vorbisprereadbuf)
        free(s->_vorbisprereadbuf);
    if (s->audiopath)
        free(s->audiopath);
    if (s->output_samplebuf)
        free(s->output_samplebuf);
    free(s);
}

uint64_t h3daudio_PlaySoundFromFile(
        h3daudiodevice *dev, const char *path,
        double volume, double panning, int loop
        ) {
    if (!dev->_decoderandmainthreadmutex)
        return 0;
    mutex_Lock(dev->_decoderandmainthreadmutex);
    h3dsound *s = malloc(sizeof(*s));
    if (!s) {
        mutex_Release(dev->_decoderandmainthreadmutex);
        return 0;
    }
    memset(s, 0, sizeof(*s));
    s->audiopath = filesys_Normalize(
        path
    );
    if (!s->audiopath) {
        free(s);
        mutex_Release(dev->_decoderandmainthreadmutex);
        return 0;
    }
    s->loop = (loop != 0);
    s->buffer_count = 10;
    s->output_samplebuf = malloc(
        dev->audiobufsize * s->buffer_count
    );
    if (!s->output_samplebuf) {
        free(s->audiopath);
        free(s);
        mutex_Release(dev->_decoderandmainthreadmutex);
        return 0;
    }

    dev->last_sound_id++;
    if (dev->last_sound_id == 0)
        dev->last_sound_id++;
    s->id = dev->last_sound_id;
    s->buffer_fill_index = -1;
    s->buffer_play_index = -1;
    s->output_samplerate = dev->samplerate;
    s->channels = 2;
    s->volume = fmax(0.0, fmin(volume, 1.0));
    s->panning = fmax(-1.0, fmin(panning, 1.0));

    int wasadded = 0;
    int i = 0;
    while (i < dev->activesoundrange) {
        if (!dev->playingsounds[i] ||
                dev->playingsounds[i]->completed) {
            if (dev->playingsounds[i]) {
                h3dsound *sold = dev->playingsounds[i];
                dev->playingsounds[i] = NULL;
                h3daudio_FreeSound(sold);
            }
            dev->playingsounds[i] = s;
            wasadded = 1;
            break;
        }
        i++;
    }
    if (!wasadded) {
        if (dev->activesoundrange >= MAXPLAYINGSOUNDS) {
            h3daudio_FreeSound(s);
            mutex_Release(dev->_decoderandmainthreadmutex);
            return 0;
        }
        dev->playingsounds[dev->activesoundrange] = s;
        dev->activesoundrange++;
    }
    uint64_t resultid = s->id;
    mutex_Release(dev->_decoderandmainthreadmutex);
    return resultid;
}

h3daudiodevice *h3daudio_OpenDevice(
        char **error
        ) {
    return h3daudio_OpenDeviceEx(
        48000,
        #if defined(__ANDROID__) || defined(ANDROID)
        2048
        #else
        1024
        #endif
        ,
        H3DAUDIO_BACKEND_DEFAULT, NULL, error
    );
}

int h3daudio_GetSoundVolume(
        h3daudiodevice *dev, uint64_t id,
        double *volume, double *panning
        ) {
    mutex_Lock(dev->_decoderandmainthreadmutex);
    int i = 0;
    while (i < dev->activesoundrange) {
        if (!dev->playingsounds[i] ||
                dev->playingsounds[i]->completed ||
                dev->playingsounds[i]->id != id) {
            i++;
            continue;
        }
        *volume = dev->playingsounds[i]->volume;
        *panning = dev->playingsounds[i]->panning;
        mutex_Release(dev->_decoderandmainthreadmutex);
        return 1;
    }
    mutex_Release(dev->_decoderandmainthreadmutex);
    return 0;
}

void h3daudio_ChangeSoundVolume(
        h3daudiodevice *dev, uint64_t id, double volume, double panning
        ) {
    mutex_Lock(dev->_decoderandmainthreadmutex);
    int i = 0;
    while (i < dev->activesoundrange) {
        if (!dev->playingsounds[i] ||
                dev->playingsounds[i]->completed ||
                dev->playingsounds[i]->id != id) {
            i++;
            continue;
        }
        dev->playingsounds[i]->volume = fmax(0.0, fmin(1.0, volume));
        dev->playingsounds[i]->panning = fmax(-1.0, fmin(1.0, panning));
        i++;
    }
    mutex_Release(dev->_decoderandmainthreadmutex);
}

int h3daudio_IsSoundPlaying(
        h3daudiodevice *dev, uint64_t id
        ) {
    mutex_Lock(dev->_decoderandmainthreadmutex);
    int i = 0;
    while (i < dev->activesoundrange) {
        if (!dev->playingsounds[i] || dev->playingsounds[i]->id != id) {
            i++;
            continue;
        }
        if (!dev->playingsounds[i]->completed) {
            mutex_Release(dev->_decoderandmainthreadmutex);
            return 1;
        }
        break;
    }
    mutex_Release(dev->_decoderandmainthreadmutex);
    return 0;
}

int h3daudio_SoundHadPlaybackError(
        h3daudiodevice *dev, uint64_t id
        ) {
    mutex_Lock(dev->_decoderandmainthreadmutex);
    int i = 0;
    while (i < dev->activesoundrange) {
        if (!dev->playingsounds[i] || dev->playingsounds[i]->id != id) {
            i++;
            continue;
        }
        if (dev->playingsounds[i]->vfserror) {
            mutex_Release(dev->_decoderandmainthreadmutex);
            return 1;
        }
        break;
    }
    mutex_Release(dev->_decoderandmainthreadmutex);
    if (failed_sound_id_set)
        return hashset_Contains(
            failed_sound_id_set, &id, sizeof(id)
        );
    return 0;
}

void h3daudio_StopSound(h3daudiodevice *dev, uint64_t id) {
    mutex_Lock(dev->_decoderandmainthreadmutex);
    int i = 0;
    while (i < dev->activesoundrange) {
        if (!dev->playingsounds[i] ||
                dev->playingsounds[i]->completed ||
                dev->playingsounds[i]->id != id) {
            i++;
            continue;
        }
        dev->playingsounds[i]->completed = 1;
        i++;
    }
    mutex_Release(dev->_decoderandmainthreadmutex);
}

int h3daudio_GetDeviceId(h3daudiodevice *dev) {
    if (!dev)
        return -1;
    return dev->id;
}

const char *h3daudio_GetDeviceName(h3daudiodevice *dev) {
    if (!dev)
        return NULL;
    return dev->name;
}
