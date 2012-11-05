#ifndef DSMCC_TS_H
#define DSMCC_TS_H

#include "libdsmcc.h"

#define DSMCC_PID_BUFFER_SIZE (1024 * 8 + 188)

struct dsmcc_pid_buffer
{
        int           pid;
        int           in_section;
        int           cont;
        unsigned char data[DSMCC_PID_BUFFER_SIZE];

        struct dsmcc_pid_buffer *next;
};

#endif
