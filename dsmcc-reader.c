#include <signal.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "libdsmcc.h" 

static int running = 1;

static void sigint_handler(int signal)
{
    fprintf(stderr, "sigint\n");
    running = 0;
}

static int process_stream(FILE *ts, struct dsmcc_status *dsmcc_handle)
{
    char buf[188];
    int ret = 0;
    int rc;
    int i = 0;

    while(running)
    {
        rc = fread(buf, 1, 188, ts);
        if(rc < 1)
        {
            if(ferror(ts))
            {
                fprintf(stderr, "read error : %s\n", strerror(errno));
                ret = -1;
		break;
            }
            else
            { // hack: parse what remain in the buffer
                struct pid_buffer *hbuf = dsmcc_handle->buffers;
                dsmcc_process_section(dsmcc_handle, hbuf->data, hbuf->in_section, hbuf->pid);
            }
            // else EOF
            break;
        }
        else
        {
            DSMCC_DEBUG("[main] read ts #%d\n", i++);
            dsmcc_receive(dsmcc_handle, (unsigned char*)buf, rc); 
        }
    }
    
    return ret;
}

int main(int argc, char **argv)
{
    struct dsmcc_status *dsmcc_handle;
    int status = 0; 
    char *channel_name;
    FILE *ts;
    struct pid_buffer *buf;
    static uint16_t pid;
    int i;

    if(argc < 4)
    {
        fprintf(stderr, "usage %s <file> <pid> <channel name>\n", argv[0]);
        return -1;
    }

    sscanf(argv[2], "%hu", &pid);
    channel_name = argv[3];

    signal(SIGINT, sigint_handler);

    fprintf(stderr, "start\n");



    ts = fopen(argv[1], "r");
    if(ts)
    {
        dsmcc_handle = dsmcc_open(channel_name);

	buf = malloc(sizeof(struct pid_buffer));
	buf->pid = pid;
	buf->pointer_field = 0;
	buf->in_section = 0;
	buf->cont = -1;
	buf->next = NULL;
	dsmcc_handle->buffers = buf;

        status = process_stream(ts, dsmcc_handle);
        for(i = 0; i < MAXCAROUSELS; i++)
            dsmcc_objcar_free(&dsmcc_handle->carousels[i]);

        dsmcc_free(dsmcc_handle);
    }
    else
    {
        fprintf(stderr, "open '%s' error : %s\n", argv[1], strerror(errno));
        return -1;
    }
    
    fclose(ts);

    return status;

}


