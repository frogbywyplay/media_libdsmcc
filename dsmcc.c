#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libdsmcc.h"
#include "dsmcc-receiver.h"

/* Init library and return new status struct */

struct dsmcc_status *dsmcc_open(const char *channel, FILE *debug_fd) {

	struct dsmcc_status *status = NULL;

	status = malloc(sizeof(struct dsmcc_status));

	if(status == NULL) {
		return NULL;
	}

	status->rec_files = status->total_files = 0;
	status->rec_dirs = status->total_dirs = 0;
	status->gzip_size = status->total_size = 0;
	
	status->newstreams = status->streams = NULL;

	status->debug_fd = debug_fd;

	dsmcc_init(status, channel);

	return status;
}

void dsmcc_receive(struct dsmcc_status *status, unsigned char *Data, int Length) {
	struct pid_buffer *buf;
	unsigned int pid = 0;
	unsigned int cont;


	if(Length <= 0 || Length != 188) {
		return;
	}


	if(!Data || *Data != DSMCC_SYNC_BYTE) {
		/* Cancel current section as skipped a packet */
		return;
	}

	/* Test if error set */

	if(*(Data+1) & DSMCC_TRANSPORT_ERROR) {
		return;
	}

	pid = ((*(Data+1) & 0x1F) << 8) | *(Data+2);

	/* Find correct buffer for stream - TODO speed up ? */

	for(buf=status->buffers; buf!=NULL; buf= buf->next) {
		if(buf->pid == pid) { break; }
	}

	if(buf == NULL) {
		return;
	}

	/* Test if start on new dsmcc_section */

	cont = *(Data+3) & 0x0F;

	if(buf->cont == 0xF && cont == 0) {
		buf->cont = 0;
	} else if(buf->cont+1 == cont) {
		buf->cont++;
	} else if(buf->cont == -1) {
		buf->cont = cont;
	} else {
		/* Out of sequence packet, drop current section */
		if(status->debug_fd != NULL) {
			fprintf(status->debug_fd, "[libdsmcc] Packet out of sequence (cont %d, buf->cont %d , resetting\n", cont, buf->cont);
		}
		buf->in_section = 0;
		memset(buf->data, 0xFF, 4284);
	}

	if(*(Data+1) & DSMCC_START_INDICATOR) {
//		syslog(LOG_ERR, "new dsmcc section");
		if(status->debug_fd != NULL) {
			fprintf(status->debug_fd, "[libdsmcc] New dsmcc section\n");
		}
		if(buf->in_section) {
			buf->pointer_field = *(Data+4);
			if(buf->pointer_field >= 0 && buf->pointer_field <183) {
			    if(buf->pointer_field > 0) {
				memcpy(buf->data+buf->in_section, Data+5,
							 buf->pointer_field);
			    }
			    dsmcc_process_section(status, buf->data, buf->in_section, pid);
			    /* zero buffer ? */
			    memset(buf->data, 0xFF, 4284);
			    /* read data upto this and append to buf */
			    buf->in_section = 183 - buf->pointer_field;
			    buf->cont = -1;
			    memcpy(buf->data, Data+(5+buf->pointer_field),
							buf->in_section);
		        } else {
			   /* corrupted ? */
			   fprintf(stderr,"pointer field %d\n", buf->pointer_field);
		   	}
		} else {
		   buf->in_section = 183;
		   memcpy(buf->data, Data+5, 183);
		   /* allocate memory and save data (test end ? ) */
		}
	} else {
	    if(buf->in_section > 0) {
		if(buf->in_section > 4284) 
			syslog(LOG_ERR, "Packet overwrriten buffer");
	        /* append data to buf */
		memcpy(buf->data+buf->in_section, Data+4, 184);
		buf->in_section += 184;
	    } else {
		/* error ? */
	    }
	}

}
	
void dsmcc_close(struct dsmcc_status *status) {

	/* Handle streams */

	dsmcc_free(status);

}
	
