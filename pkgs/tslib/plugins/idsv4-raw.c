/*
*  tslib/plugins/idsv4.c
*
*  Copyright (C) 2007 Jason Kuri.
*
* This file is placed under the LGPL.  Please see the file
* COPYING for more details.
*
* This raw input supports the wacom Tablet-PC serial interface.
* It was written to support the wacom digitizer pad for the
* iRex iLiad eBook reader.
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

struct tslib_idsv4 {
    struct tslib_module_info module;
    int    sane_fd;
};

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
    int bytes_read=0;
    int bytes_needed=0;
    int offset = 0;
    int stored_offset = 0;
    int tries = 0;
    char s[12];
    char buffer[12];

#ifdef DEBUG
    fprintf(stderr,"Initializing fd for idsv4\n");
#endif

    // Let's lock the port - just so nobody messes up our tablet configuration
    // while we are running
    // Always unlock the fd, as it may be called by calibration.
    // fprintf(stderr, "always unlock fd.\n");
    flock(fd, LOCK_UN);

    if (flock(fd, (LOCK_EX|LOCK_NB)) == -1)
    {
        fprintf(stderr,"Unable to lock port: %s\n",strerror(errno));
        // Can not just return -1, as it may happen during mouse calibration.
        // return -1;
    }

    if (tcgetattr (fd, &tios))
    {
        fprintf(stderr,"Failed to get port params: %s",strerror(errno));
        return -1;
    }

    tios.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON);
    tios.c_oflag &= ~OPOST;
    tios.c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
    tios.c_cflag &= ~(CSIZE|PARENB);
    tios.c_cflag |= CS8|CLOCAL;
    tios.c_cflag &= ~(CSTOPB); /* 1 stop bit */
    tios.c_cflag &= ~(CSIZE); /* 8 data bits */
    tios.c_cflag |= CS8;
    tios.c_cflag &= ~(PARENB); /* no parity */
    tios.c_iflag |= IXOFF;        /* flow control XOff */
    tios.c_cc[VMIN] = 1;        /* vmin value */
    tios.c_cc[VTIME] = 0;        /* vtime value */

    cfsetispeed(&tios, B19200);
    cfsetospeed(&tios, B19200);

    if (tcsetattr (fd, TCSANOW, &tios))
    {
        fprintf(stderr, "Failed to set port params: %s", strerror(errno));
        return -1;
    }

    // Tell the wacom to stop sampling
    // write(fd, "0", 1);

    // sleep for 200ms for wacom to relax.
    usleep(200000);

    // query the tablet.  It seems to want to tell us it's capabilities.
    // if we don't ask it it tells us in the first packet - which is no good,
    // and confuses everything... so we ask it.
    // write(fd, "1", 1);
    fprintf(stderr, "Ingore Enable wacom now\n\n\n");
    return(1);
}

/* this needs to be it's own routine because the wacom doesn't always give us the right
number of bytes on the first read, so we need to do some funkyness to get it right */

static int idsv4_read_and_decode_packet(int fd, struct ts_sample *samp)
{
    unsigned char event_buffer[9];
    unsigned char s[9];
    int stored_offset = 0;
    int offset = 0;
    int bytes_needed = 9;
    int bytes_read = 0;
    unsigned int buttons = 0;

    memset(s, 0, 9);

    while(bytes_needed > 0)
    {
        offset = 0;
        bytes_read = read(fd, event_buffer, bytes_needed);
        if (bytes_read <= 0)
        {
#ifdef DEBUG
            if (bytes_read == -1)
            {
                fprintf(stderr, "tablet read error: %s\n", strerror(errno));
            }
#endif
            return 0;
        }
        else
        {
            if (stored_offset == 0)
            {
                while ( offset < bytes_read)
                {
                    /* 0x20 - docs say yes.  Tablet says no.  0xc0 is the only one that works.
                    *  if (!((event_buffer[offset] & 0x20) && (event_buffer[offset] & 0x80))) {
                    */
                    if (!((event_buffer[offset] & 0xc0) && (event_buffer[offset] & 0x80)))
                    {
                        offset++;
                    }
                    else
                    {
                        memcpy(s, event_buffer+offset, bytes_read-offset);
                        stored_offset = bytes_read-offset;
                        bytes_needed -= stored_offset;
                        offset = bytes_read;
                    }
                }
            }
            else
            {
                memcpy(s+stored_offset, event_buffer, bytes_read);
                stored_offset += bytes_read;
                bytes_needed -= bytes_read;
            }
        }
    }

    int prox = (s[0] & 0x20);

    // fprintf(stderr, "Proximity: %d", (s[0] & 0x20));
    // fprintf(stderr, "xtilt: %d\n", (s[7] & 0x3F));
    // fprintf(stderr, "ytilt: %d\n", (s[8] & 0x3F));
    samp->y     = (s[1]<<9) | (s[2]<<2) | ((s[6]>>5)&0x03);
    samp->x     = (s[3]<<9) | (s[4]<<2) | ((s[6]>>3)&0x03);
    samp->pressure         = ((s[6]&0x07)<<7) | s[5];
    // samp->y     = (12416 - samp->y ) * 800 / 12320;
    // samp->x     = (samp->x - 20) * 600 / 9080;
    //samp->pressure  = samp->pressure;
    gettimeofday(&samp->tv,NULL);
    buttons = s[0] & 0x07;
    // buttons & 0x04 = eraser, buttons & 0x02 = button
    // samp->flags = (buttons>>2) & TSFLAG_ERASER | buttons & TSFLAG_BUTTON1;
//#ifdef DEBUG
    fprintf(stderr, "raw data %dx%d  pressure %d s[0] %d\n",
           samp->x, samp->y,  samp->pressure, s[0]);
//#endif
    return 1;
}

static int idsv4_read(struct tslib_module_info *inf, struct ts_sample *samp, int nr)
{
    struct tslib_idsv4 *i = (struct tslib_idsv4 *)inf;
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
        if (idsv4_read_and_decode_packet(dev->fd, samp) == 1)
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

static int ts_idsv4_fini(struct tslib_module_info *inf)
{
    fprintf(stderr, "uninitialize idsv module.\n");
    struct tslib_idsv4 *i = (struct tslib_idsv4 *)inf;

    flock(i->module.dev->fd, LOCK_UN);
    free(inf);
    return 0;
}

static const struct tslib_ops idsv4_ops =
{
    .read    = idsv4_read,
    .fini    = ts_idsv4_fini,

};

TSAPI struct tslib_module_info *mod_init(struct tsdev *dev, const char *params)
{
    struct tslib_idsv4 *i;

    i = malloc(sizeof(struct tslib_idsv4));
    if (i == NULL)
        return NULL;

    i->module.ops = &idsv4_ops;
    i->sane_fd = 0;


#ifdef DEBUG
    fprintf(stderr, "Initializing IDSV4 module done now\n");
#endif

    i->sane_fd = setup_fd(dev->fd);

    return &(i->module);
}
