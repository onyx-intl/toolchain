/*
 *  tslib/plugins/offset.c
 *
 *  Copyright (C) 2007 Jason Kuri
 *
 * This file is placed under the LGPL.  Please see the file
 * COPYING for more details.
 *
 * offset touchscreen values - in order to adjust for pen angle
 * on the fly.  Reads default from offset file, can be updated by
 * writing x and y offset values to a pipe - by default /var/tmp/offsetpipe
 * the format for the file and for what is written to the pipe is the same.
 * It should be a single string terminated by a null or newline with
 * x offset first, followed by y offset.  Negative offsets are allowed.
 * This example would create a +10 x offset and a -20 y offset (exclude quotes):
 * "10 -20\n"
 */
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>


#include <stdio.h>

#include "tslib.h"
#include "tslib-filter.h"

struct tslib_offset {
	struct tslib_module_info module;
    int x_offset;
    int y_offset;
    int fifo_fd;
    int sample_count;
};



static int offset_fini(struct tslib_module_info *info)
{
    struct tslib_offset *off = (struct tslib_offset *)info;
	
    close(off->fifo_fd);
	free(info);
	return 0;
}



void parse_offset(struct tslib_offset *off, char *offsetinfo, int len) {
    
    char buffer[512];
    int o[2];
    int pos = 0;
    int index = 0;
    char *valueptr;
    
    if (len >= 512) { 
        len = 511;
    }
    strncpy(buffer, offsetinfo, len);
    valueptr = buffer;
    while(pos <= len && index < 2 && buffer[pos] != 0) {
        if (buffer[pos] == ' ' || buffer[pos] == '\n' || pos == len) {
            buffer[pos] = '\0';
            o[index] = atoi(valueptr);
            index++;
            valueptr = buffer + pos + 1;
        }
        pos++;
    }
	if (index == 2) {
        off->x_offset = o[0];
        off->y_offset = o[1];
        #ifdef DEBUG
        fprintf(stderr,"offset changed: x: %d  y: %d\n", off->x_offset, off->y_offset);
        #endif /*DEBUG*/
	}
}

static int
offset_read(struct tslib_module_info *info, struct ts_sample *samp, int nr)
{
	struct tslib_offset *off = (struct tslib_offset *)info;
	struct ts_sample cur;
	int ret;
    int bytes_read;
    int count;
	int xtemp,ytemp;
    char buffer[512];

    //fprintf(stderr, "Reading %d samples\n", nr);
    if (off->sample_count > 50) {
        off->sample_count = 0;
        // check the fifo.  it's in nonblocking - so we can read as much as we want
        // it will just give back what it had in the buffer
        bytes_read = read(off->fifo_fd, buffer, 512);
        if (bytes_read > 0) {
            parse_offset(off, buffer, bytes_read);
        }
    }
    //fprintf(stderr, "past fifo check\n");
    
    ret = info->next->ops->read(info->next, samp, nr);
	if (ret >= 0) {
		int nr;

		for (nr = 0; nr < ret; nr++, samp++) {
            samp->x += off->x_offset;
            samp->y += off->y_offset;
            #ifdef DEBUG
                fprintf(stderr, "------> adjusted:  %d\t%d\n", samp->x, samp->y);
            #endif
            off->sample_count++;
        }
    }
	
	return ret;
}

static const struct tslib_ops offset_ops =
{
	.read	= offset_read,
	.fini	= offset_fini,
};

#define NR_VARS (sizeof(offset_vars) / sizeof(offset_vars[0]))

TSAPI struct tslib_module_info *mod_init(struct tsdev *dev, const char *params)
{

	struct tslib_offset *off;
	
	struct stat sbuf;
	int offset_fd;
	char offsetbuf[200];
	int bytes,ret;
	char *offsetfile=NULL;
	char *defaultoffsetfile = "/etc/pointeroffset";

	char *fifofile=NULL;
	char *defaultfifofile = "/var/tmp/offsetpipe";


    off = malloc(sizeof(struct tslib_offset));
	if (off == NULL) {
		return NULL;
	}

	off->module.ops = &offset_ops;
    off->x_offset = 0;
    off->y_offset = 0;

    off->fifo_fd = 0;
    off->sample_count = 0;
    
	/*
	 * Check calibration file
	 */
	//fprintf(stderr, "Loading offsetfile\n");
     
	if( (offsetfile = getenv("TSLIB_OFFSETFILE")) == NULL) offsetfile = defaultoffsetfile;
	if(stat(offsetfile,&sbuf)==0) {
		offset_fd = open(offsetfile,O_RDONLY);
		bytes = read(offset_fd,offsetbuf,sbuf.st_size);
        //fprintf(stderr, "Loaded %d bytes from offsetfile\n", bytes);
        parse_offset(off, offsetbuf, bytes);
		close(offset_fd);
	}
    
	if( (fifofile = getenv("TSLIB_OFFSETFIFO")) == NULL) fifofile = defaultfifofile;
    //fprintf(stderr, "making fifo file\n");
    ret = mknod(fifofile, S_IFIFO | 0660, 0);
    if (ret == 0 || (ret == -1 && errno == EEXIST)) {
        off->fifo_fd = open(fifofile, O_RDONLY|O_NONBLOCK|O_NDELAY);
    }

	return &off->module;
}
