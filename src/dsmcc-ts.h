#ifndef DSMCC_TS_H
#define DSMCC_TS_H

#define DSMCC_TSPARSER_BUFFER_SIZE (1024 * 8 + 188)

struct dsmcc_tsparser_buffer
{
        unsigned int  pid;
        int           in_section;
        int           cont;
        unsigned char data[DSMCC_TSPARSER_BUFFER_SIZE];

        struct dsmcc_tsparser_buffer *next;
};

#endif
