/* /bin/play — write a short PCM sine wave to /dev/pcm.
 *
 * The plain-b virtio-sound driver is configured as 48 kHz / stereo / S16LE,
 * so this program generates exactly that format.
 */
#include "libc.h"

#define SAMPLE_RATE 48000
#define CHANNELS    2
#define DURATION_MS 800
#define AMPLITUDE   12000

static int16_t samples[SAMPLE_RATE * CHANNELS / 10]; /* 0.1s chunk */

/* 64-entry sine table, amplitude 12000 (avoids libm in the tiny libc). */
static const int16_t sine_table[64] = {
    0, 1176, 2341, 3483, 4592, 5657, 6667, 7613,
    8485, 9276, 9978, 10583, 11087, 11483, 11769, 11942,
    12000, 11942, 11769, 11483, 11087, 10583, 9978, 9276,
    8485, 7613, 6667, 5657, 4592, 3483, 2341, 1176,
    0, -1176, -2341, -3483, -4592, -5657, -6667, -7613,
    -8485, -9276, -9978, -10583, -11087, -11483, -11769, -11942,
    -12000, -11942, -11769, -11483, -11087, -10583, -9978, -9276,
    -8485, -7613, -6667, -5657, -4592, -3483, -2341, -1176,
};

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    int fd = open("/dev/pcm", 1);   /* O_WRONLY */
    if (fd < 0) {
        printf("play: cannot open /dev/pcm\n");
        return 1;
    }

    int total_frames = SAMPLE_RATE * DURATION_MS / 1000;
    int written_frames = 0;
    /* qvos sys_write caps each transfer at 4096 bytes = 1024 stereo S16 frames. */
    int chunk_frames = 1024;

    while (written_frames < total_frames) {
        int n = total_frames - written_frames;
        if (n > chunk_frames)
            n = chunk_frames;
        for (int i = 0; i < n; i++) {
            /* 440 Hz at 48 kHz = 48/440*64 samples per table period. */
            unsigned phase = (unsigned)((written_frames + i) * 64ULL * 440ULL /
                                        SAMPLE_RATE) & 63;
            int16_t v = sine_table[phase];
            samples[i * CHANNELS + 0] = v;
            samples[i * CHANNELS + 1] = v;
        }
        ssize_t w = write(fd, samples, (size_t)n * CHANNELS * 2);
        if (w <= 0) {
            printf("play: write failed\n");
            close(fd);
            return 1;
        }
        written_frames += (int)(w / (CHANNELS * 2));
    }

    close(fd);
    printf("play: wrote %d frames (%d ms) to /dev/pcm\n",
           written_frames, written_frames * 1000 / SAMPLE_RATE);
    return 0;
}
