#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libdsmcc.h"
#include "dsmcc-util.h"
#include "dsmcc-receiver.h"

#define DSMCC_SYNC_BYTE		0x47
#define DSMCC_TRANSPORT_ERROR	0x80
#define DSMCC_START_INDICATOR	0x40

/* Init library and return new status struct */
struct dsmcc_status *dsmcc_open(const char *channel)
{

	struct dsmcc_status *status = NULL;

	status = malloc(sizeof(struct dsmcc_status));
	if(status == NULL)
		return NULL;

	status->rec_files = status->total_files = 0;
	status->rec_dirs = status->total_dirs = 0;
	status->gzip_size = status->total_size = 0;
	status->newstreams = status->streams = NULL;

	dsmcc_init(status, channel);

	return status;
}

void dsmcc_receive(struct dsmcc_status *status, unsigned char *data, int length)
{
	struct pid_buffer *buf;
	unsigned int pid = 0;
	unsigned int cont;

	if (length <= 0 || length != 188)
	{
		DSMCC_WARN("[dsmcc] Skipping packet: Invalid packet size (got %d, expected %d)\n", length, 188);
		return;
	}

	if (!data)
	{
		DSMCC_WARN("[dsmcc] Skipping packet: data == NULL\n");
		return;
	}

	if (data[0] != DSMCC_SYNC_BYTE)
	{
		DSMCC_WARN("[dsmcc] Skipping packet: Invalid sync byte: got 0x%02x, expected 0x%02x\n", (int) *data, (int) DSMCC_SYNC_BYTE);
		return;
	}

	/* Test if error bit is set */
	if (data[1] & DSMCC_TRANSPORT_ERROR)
	{
		DSMCC_WARN("[dsmcc] Skipping packet: Error bit is set\n");
		return;
	}

	pid = ((data[1] & 0x1F) << 8) | data[2];

	/* Find correct buffer for stream */
	for (buf = status->buffers; buf != NULL; buf = buf->next)
	{
		if (buf->pid == pid)
			break;
	}
	if (buf == NULL)
	{
		DSMCC_WARN("[dsmcc] Skipping packet: No buffer found for PID 0x%04x\n", pid);
		return;
	}

	/* Test if start on new dsmcc_section */
	cont = data[3] & 0x0F;

	if (buf->cont == 0xF && cont == 0)
	{
		buf->cont = 0;
	}
	else if (buf->cont + 1 == cont)
	{
		buf->cont++;
	}
	else if (buf->cont == -1)
	{
		buf->cont = cont;
	}
	else
	{
		/* Out of sequence packet, drop current section */
		DSMCC_WARN("[dsmcc] Packet out of sequence (cont=%d, buf->cont=%d), resetting\n", cont, buf->cont);
		buf->in_section = 0;
		memset(buf->data, 0xFF, DSMCC_PID_BUF_SIZE);
	}

	if (data[1] & DSMCC_START_INDICATOR)
	{
		DSMCC_DEBUG("[dsmcc] New dsmcc section\n");
		if(buf->in_section)
		{
			buf->pointer_field = data[4];
			if (buf->pointer_field >= 0 && buf->pointer_field < 183)
			{
				if (buf->pointer_field > 0)
					memcpy(buf->data + buf->in_section, data + 5, buf->pointer_field);

				dsmcc_process_section(status, buf->data, buf->in_section, pid);
				
				/* zero buffer ? */
				memset(buf->data, 0xFF, DSMCC_PID_BUF_SIZE);
				
				/* read data upto this and append to buf */
				buf->in_section = 183 - buf->pointer_field;
				buf->cont = -1;
				memcpy(buf->data, data + 5 + buf->pointer_field, buf->in_section);
			}
			else
			{
				/* TODO corrupted ? */
				DSMCC_ERROR("Invalid pointer field %d\n", buf->pointer_field);
			}
		}
		else
		{
			buf->in_section = 183;
			memcpy(buf->data, data + 5, 183);
			/* allocate memory and save data (test end ? ) */
		}
	}
	else
	{
		if (buf->in_section > 0)
		{
			/* append data to buf */
			if (buf->in_section + 184 > DSMCC_PID_BUF_SIZE)
			{
				DSMCC_ERROR("[dsmcc] Section buffer overflow (buffer is already at %d bytes)\n", buf->in_section);
				memcpy(buf->data + buf->in_section, data + 4, DSMCC_PID_BUF_SIZE - buf->in_section);
				buf->in_section = DSMCC_PID_BUF_SIZE;
			}
			else
			{
				memcpy(buf->data + buf->in_section, data + 4, 184);
				buf->in_section += 184;
			}
		}
		else
		{
			/* TODO error ? */
		}
	}
}

void dsmcc_close(struct dsmcc_status *status)
{
	/* Handle streams */
	dsmcc_free(status);
}
