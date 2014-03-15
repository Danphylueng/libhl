#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>

#include "rbuf.h"

#define RBUF_DEFAULT_SIZE 4096

struct __rbuf_s {
    u_char *buf;        // the buffer
    int size;           // buffer size
    int rfx;            // read offset
    int wfx;            // write offset
    int mode;           // the ringbuffer mode (blocking/overwrite)
};

rbuf_t *
rbuf_create(int size) {
    rbuf_t *new_rb;
    new_rb = (rbuf_t *)calloc(1, sizeof(rbuf_t));
    if(!new_rb) {
        /* TODO - Error Messaeggs */
        return NULL;
    }
    if(size == 0) 
        new_rb->size = RBUF_DEFAULT_SIZE+1;
    else
        new_rb->size = size+1;
    new_rb->buf = (u_char *)malloc(new_rb->size);
    if(!new_rb->buf) {
        /* TODO - Error Messaeggs */
        free(new_rb);
        return NULL;
    }
    return new_rb;
}

void
rbuf_set_mode(rbuf_t *rbuf, rbuf_mode_t mode)
{
    rbuf->mode = mode;
}

rbuf_mode_t
rbuf_mode(rbuf_t *rbuf)
{
    return rbuf->mode;
}

void
rbuf_skip(rbuf_t *rb, int size) {
    if(size >= rb->size) { // just empty the ringbuffer
        rb->rfx = rb->wfx;
    } else {
        if (size > rb->size-rb->rfx) {
            size -= rb->size-rb->rfx;
            rb->rfx = size;
        } else {
            rb->rfx+=size;
        }
    }
}

int
rbuf_read(rbuf_t *rb, u_char *out, int size) {
    int read_size = rbuf_len(rb); // never read more than available data
    int to_end = rb->size - rb->rfx;
    
    // requested size is less than stored data, return only what has been requested
    if(read_size > size)  
        read_size = size;

    if(read_size > 0) {
        // if the write pointer is beyond the read pointer or the requested read_size is 
        // smaller than the number of octets between the read pointer and the end of the buffer,
        // than we can safely copy all the octets in a single shot
        if(rb->wfx > rb->rfx || to_end >= read_size) {
            memcpy(out, &rb->buf[rb->rfx], read_size);
            rb->rfx += read_size;
        }
        else { // otherwise we have to wrap around the buffer and copy octest in two times
            memcpy(out, &rb->buf[rb->rfx], to_end);
            memcpy(out+to_end, &rb->buf[0], read_size - to_end);
            rb->rfx = read_size - to_end;
        }
    }

    return read_size;
}

int
rbuf_write(rbuf_t *rb, u_char *in, int size) {
    int write_size = rb->size - rbuf_len(rb) - 1; // don't write more than available size

    if(!rb || !in || !size) // safety belt
        return 0;
    // if requested size fits the available space, use that
    if(write_size > size) {
        write_size = size;
    } else if (rb->mode == RBUF_MODE_OVERWRITE) {
        if (size > rb->size -1) {
            // the provided buffer is bigger than the
            // ringbuffer itself. Since we are in overwrite mode,
            // only the last chunk will be actually stored.
            write_size = rb->size - 1;
            in = in + (size - write_size);
            rb->rfx = 0;
            memcpy(rb->buf, in, write_size);
            rb->wfx = write_size;
            return size;
        }
        // we are in overwrite mode, so let's make some space
        // for the new data by advancing the read offset
        int diff = size - write_size;
        rb->rfx += diff;
        write_size += diff;
        if (rb->rfx >= rb->size)
            rb->rfx -= rb->size;
    }

    if(rb->wfx >= rb->rfx) { // write pointer is ahead
        if(write_size <= rb->size - rb->wfx) {
            memcpy(&rb->buf[rb->wfx], in, write_size);
            rb->wfx+=write_size;
        } else { // and we have to wrap around the buffer 
            int to_end = rb->size - rb->wfx;
            memcpy(&rb->buf[rb->wfx], in, to_end);
            memcpy(rb->buf, in+to_end, write_size - to_end);
            rb->wfx = write_size - to_end;
        }
    } else { // read pointer is ahead we can safely memcpy the entire chunk
        memcpy(&rb->buf[rb->wfx], in, write_size);
        rb->wfx+=write_size;
    }

    return write_size;
}

int
rbuf_len(rbuf_t *rb) {
    if(rb->wfx == rb->rfx)
        return 0;
    if(rb->wfx < rb->rfx) 
        return rb->wfx+(rb->size-rb->rfx);
    return rb->wfx-rb->rfx;
}

void
rbuf_clear(rbuf_t *rb) {
    rb->rfx = rb->wfx = 0;
}

void
rbuf_destroy(rbuf_t *rb) {
    if(rb->buf)
        free(rb->buf);
    free(rb);
}


int
rbuf_find(rbuf_t *rb, u_char octet) {
    int i;
    int to_read = rbuf_len(rb);
    if (to_read == 0)
        return -1;

    if(rb->wfx > rb->rfx) {
        for (i = rb->rfx; i < rb->wfx; i++) {
            if(rb->buf[i] == octet)
                return(i-rb->rfx);
        }
    } else {
        for (i = rb->rfx; i < rb->size; i++) {
            if(rb->buf[i] == octet)
                return(i-rb->rfx);
        }
        for (i = 0; i < rb->wfx; i++) {
            if(rb->buf[i] == octet)
                return((rb->size-rb->rfx)+i);
        }
    }
    return -1;
}

int
rbuf_read_until(rbuf_t *rb, u_char octet, u_char *out, int maxsize)
{
    int i;
    int size = rbuf_len(rb);
    int to_read = size;
    int found = 0;
    for (i = rb->rfx; i < rb->size; i++) {
        to_read--;
        if(rb->buf[i] == octet)  {
            found = 1;
            break;
        } else if ((size-to_read) == maxsize) {
            break;
        } else {
            out[i] = rb->buf[i];
        }
    }
    if(!found) {
        for (i = 0; to_read > 0 && (size-to_read) < maxsize; i++) {
            to_read--;
            if(rb->buf[i] == octet) {
                break;
            }
            else {
                out[i] = rb->buf[i];
            }
            
        }
    }
    rbuf_skip(rb, (size - to_read));
    return (size-to_read);
}
