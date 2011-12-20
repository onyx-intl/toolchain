/*
*  tslib/plugins/hanvon.c
*
*  Copyright (C) 2010 Onyx International.
*
*/


#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>
#include <errno.h>
#include <sys/file.h>
#include <fcntl.h>

#include "config.h"
#include "tslib-private.h"

#define INSTATUS_SYNC            0x80       //A0 or 80 
#define D6                       0x40
#define D5                       0x20
#define D0                       0x01
#define INSTATUS_RESERVED        0x18
#define INWDATA_SYNC             0x8080
#define MAX_SAMPLES              4
#define BUFFER_SIZE              256



struct tslib_hanvon
{
    struct tslib_module_info module;
    int    sane_fd;
};

static const int PACKAGE_SIZE = 7;

// #define DEBUG 1


// This routine is a workaround.  We'd like to do this in mod_init - but because
// tslib opens the fd after module initialization, we can't.  So we set a flag
// to indicate it hasn't been called, and call it on the first read.
// The unfortunate side effect of this is that we can't indicate that the module
// did not load properly if there was a problem.  So we just have to keep
// responding as 'failed' to the data requests.
static int setup_fd(int fd)
{
    struct termios tios;

#ifdef DEBUG
    fprintf(stderr,"Initializing fd for hanvon digitizer.\n");
#endif

    // Let's lock the port - just so nobody messes up our tablet configuration
    // while we are running
    // Always unlock the fd, as it may be called by calibration.
    // flock(fd, LOCK_UN);

    // Maybe not necessary.
    //if (flock(fd, (LOCK_EX|LOCK_NB)) == -1)
    {
        // fprintf(stderr,"Unable to lock port: %s\n",strerror(errno));
        // Can not just return -1, as it may happen during mouse calibration.
        // return -1;
    }

    if (tcgetattr (fd, &tios))
    {
        fprintf(stderr,"Failed to get port params: %s\n",strerror(errno));
        return -1;
    }

    tcflush(fd,TCIOFLUSH);
    cfsetispeed(&tios, B19200);
    if (tcsetattr(fd, TCSANOW, &tios))
    {
        perror("error: tcsetattr failed!");
        return -1;
    }
    tcflush(fd, TCIOFLUSH);

    //cfsetospeed(&tios, B19200);

    if (tcgetattr (fd, &tios))
    {
        fprintf(stderr,"Failed to get port params: %s\n",strerror(errno));
        return -1;
    }
    tios.c_cflag &= ~CSIZE;
    tios.c_cflag |= CS8;
    tios.c_cflag &= ~PARENB;

    tios.c_cflag &= ~CSTOPB;
    tios.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    tios.c_cflag |= CLOCAL | CREAD;
    tcflush(fd,TCIFLUSH);
    tios.c_lflag &= ~(ICANON|ECHO|ECHOE|ISIG);
    if (tcsetattr(fd,TCSANOW, &tios) != 0)
    {
        perror("error set_parity:tcsetattr");
        return -1;
    }

    fcntl(fd, F_SETFL, FNDELAY);            // set the read function to return immediately if no chars are ready
    fprintf(stderr, "setup_fd  with new settings finished.\n");


    return 1;
}

int my_verify(const unsigned char *buffer)
{
    int i = 0;
    if (!(buffer[i] & INSTATUS_SYNC))
    {
        return -1;
    }

    // ignore the first package.
    if (buffer[i] & 0x10)
    {
        return -1;
    }
    for(i = 1; i < PACKAGE_SIZE; ++i)
    {
        if (buffer[i] & 0x80)
        {
            return -1;
        }
    }
    return 0;
}

/* this needs to be it's own routine because the wacom doesn't always give us the right
number of bytes on the first read, so we need to do some funkyness to get it right */

static int hanvon_read_and_decode_packet(int fd, struct ts_sample *samp)
{
    unsigned char event_buffer[PACKAGE_SIZE];
    int sync_pos = 0;
    int bytes_read = 0;

    int temp = 0;
    int count = 0;
    unsigned int buttons = 0;

    count = read(fd, &event_buffer[0], PACKAGE_SIZE);
    if (count <= 0)
    {
#ifdef DEBUG
        if (count == -1)
        {
            fprintf(stderr, "tablet read error: %s\n", strerror(errno));
        }
#endif
        sync_pos = 0;
        bytes_read = 0;
        return 0;
    }

    /* search sync byte. */
    // printf("data ready.\n");
    sync_pos = my_verify(&event_buffer[0]);
    if (sync_pos == 0)
    {
            /*
            printf("hanvon raw data:\t");
            for(i = sync_pos; i < sync_pos + PACKAGE_SIZE; ++i)
            {
                printf("%0x ", event_buffer[i]);
            }
            printf("\n");
            */

            // fprintf(stderr, "Proximity: %d", (s[0] & 0x20));
            // fprintf(stderr, "xtilt: %d\n", (s[7] & 0x3F));
            // fprintf(stderr, "ytilt: %d\n", (s[8] & 0x3F));
            samp->y     = ((event_buffer[sync_pos + 6]>> 3) & 0x3) | ((event_buffer[sync_pos + 4] << 2) | (event_buffer[sync_pos + 3] << 9));
            samp->x     = ((event_buffer[sync_pos + 6] & 0x60)>> 5) | ((event_buffer[sync_pos + 2] << 2) | (event_buffer[sync_pos + 1] << 9));
            //samp->x     = (event_buffer[sync_pos + 6]>> 1) | (event_buffer[sync_pos + 2] << 2 | event_buffer[sync_pos + 1] << 9);
            samp->pressure         = ((event_buffer[sync_pos + 6]&0x07) << 7) | event_buffer[sync_pos + 5];

            // for freescale imx31L 9.7 inch
            // samp->y =   1200 - samp->y * 1200 / 0x2000;
            // samp->x =   825 - samp->x * 825 / 0x1800;
            //temp = samp->y;

            // for freescale imx508 9.7 inch
            samp->y =    samp->y * 1200 / 0x2000;
            samp->x =    samp->x * 825 / 0x1800;

            buttons = 0;// s[0] & 0x07;
            //#ifdef DEBUG
            gettimeofday(&samp->tv,NULL);
            //fprintf(stderr, "hanvon decoded data and scaled %dx%d  pressure %d s[0] %d\n",
            //     samp->x, samp->y,  samp->pressure, event_buffer[sync_pos]);
            //#endif

            sync_pos = 0;
            bytes_read = 0;
            return 1;
    }
    else
    {
        /*
        printf("Scale directly, invalid hanvon raw data found:\t");
        for(i = 0; i <  PACKAGE_SIZE; ++i)
        {
            printf("%0x ", event_buffer[i]);
        }
        printf("\n");
        */
    }
    return 0;
}

static int hanvon_read(struct tslib_module_info *inf, struct ts_sample *samp, int nr)
{
    struct tslib_hanvon *i = (struct tslib_hanvon *)inf;
    struct tsdev *dev = i->module.dev;
    int total;

    // check to see if we have initialized the serial communications
    // If we haven't, do so.
    /*  we do this in the mod_init now - makes more sense that way.
    if (i->sane_fd == 0) {
    i->sane_fd = setup_fd(dev->fd);
    }
    */

    // if we got an error back from initializing the tablet we fail on the read
    // Sorry, but what else can we do?
    if (i->sane_fd == -1)
    {
        fprintf(stderr, "fd is not sane. John: I'm freaking out!!\n");
        // return 0;
    }

    total = 0;

    while(total < nr)
    {
        if (hanvon_read_and_decode_packet(dev->fd, samp) == 1)
        {
            samp++;
            total++;
        }
        else
        {
            total = 0;
            break;
        }
    }
#ifdef DEBUG
    fprintf(stderr, "returning %d samples\n", total);
#endif

    return total;
}

static int ts_hanvon_fini(struct tslib_module_info *inf)
{
    fprintf(stderr, "uninitialize idsv module.\n");
    struct tslib_hanvon *i = (struct tslib_hanvon *)inf;

    flock(i->module.dev->fd, LOCK_UN);
    free(inf);
    return 0;
}

static const struct tslib_ops hanvon_ops =
{
    .read    = hanvon_read,
    .fini    = ts_hanvon_fini,
};

TSAPI struct tslib_module_info *mod_init(struct tsdev *dev, const char *params)
{
    (void *)params;
    struct tslib_hanvon *i;

    i = malloc(sizeof(struct tslib_hanvon));
    if (i == NULL)
        return NULL;

    i->module.ops = &hanvon_ops;
    i->sane_fd = 0;


#ifdef DEBUG
    fprintf(stderr, "Initializing hanvon module done now\n");
#endif

    i->sane_fd = setup_fd(dev->fd);

    return &(i->module);
}
