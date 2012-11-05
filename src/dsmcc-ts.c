#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libdsmcc.h"
#include "dsmcc-debug.h"
#include "dsmcc-util.h"
#include "dsmcc-ts.h"

#define DSMCC_SYNC_BYTE		0x47
#define DSMCC_TRANSPORT_ERROR	0x80
#define DSMCC_START_INDICATOR	0x40

void dsmcc_add_pid_buffer(struct dsmcc_pid_buffer **pid_buffers, int pid)
{
	struct dsmcc_pid_buffer *buf;

	/* check that we do not already track this PID */
	for (buf = *pid_buffers; buf != NULL; buf = buf->next)
	{
		if (buf->pid == pid)
			return;
	}

	/* create and register PID buffer */
	buf = malloc(sizeof(struct dsmcc_pid_buffer));
	buf->pid = pid;
	buf->in_section = 0;
	buf->cont = -1;
	buf->next = *pid_buffers;
	*pid_buffers = buf;
	DSMCC_DEBUG("Created buffer for PID 0x%x", pid);
}

void dsmcc_free_pid_buffers(struct dsmcc_pid_buffer **pid_buffers)
{
	struct dsmcc_pid_buffer *buffer = *pid_buffers;

	while (buffer)
	{
		struct dsmcc_pid_buffer *bufnext = buffer->next;
		free(buffer);
		buffer = bufnext;
	}

	*pid_buffers = NULL;
}

void dsmcc_parse_ts_packet(struct dsmcc_status *status, struct dsmcc_pid_buffer **buffers, unsigned char *packet, int packet_length)
{
	struct dsmcc_pid_buffer *buf;
	unsigned int pid;
	unsigned int cont;

	if (packet_length <= 0 || packet_length != 188)
	{
		DSMCC_WARN("Skipping packet: Invalid packet size (got %d, expected %d)", packet_length, 188);
		return;
	}

	if (!packet)
	{
		DSMCC_WARN("Skipping NULL packet");
		return;
	}

	if (packet[0] != DSMCC_SYNC_BYTE)
	{
		DSMCC_WARN("Skipping packet: Invalid sync byte: got 0x%02x, expected 0x%02x", (int) *packet, (int) DSMCC_SYNC_BYTE);
		return;
	}

	/* Test if error bit is set */
	if (packet[1] & DSMCC_TRANSPORT_ERROR)
	{
		DSMCC_WARN("Skipping packet: Error bit is set");
		return;
	}

	pid = ((packet[1] & 0x1F) << 8) | packet[2];

	/* Find correct buffer for stream */
	for (buf = *buffers; buf != NULL; buf = buf->next)
	{
		if (buf->pid == pid)
			break;
	}
	if (buf == NULL)
	{
		DSMCC_WARN("Skipping packet: No buffer found for PID 0x%x", pid);
		return;
	}

	/* Test if start on new dsmcc_section */
	cont = packet[3] & 0x0F;

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
		DSMCC_WARN("Packet out of sequence (cont=%d, buf->cont=%d), resetting", cont, buf->cont);
		buf->in_section = 0;
		memset(buf->data, 0xFF, DSMCC_PID_BUFFER_SIZE);
	}

	if (packet[1] & DSMCC_START_INDICATOR)
	{
		DSMCC_DEBUG("New section");
		if(buf->in_section)
		{
			int pointer_field = packet[4];
			if (pointer_field >= 0 && pointer_field < 183)
			{
				if (pointer_field > 0)
					memcpy(buf->data + buf->in_section, packet + 5, pointer_field);

				dsmcc_parse_section(status, pid, buf->data, buf->in_section);
				
				/* zero buffer ? */
				memset(buf->data, 0xFF, DSMCC_PID_BUFFER_SIZE);
				
				/* read data upto this and append to buf */
				buf->in_section = 183 - pointer_field;
				buf->cont = -1;
				memcpy(buf->data, packet + 5 + pointer_field, buf->in_section);
			}
			else
			{
				/* TODO corrupted ? */
				DSMCC_ERROR("Invalid pointer field %d", pointer_field);
			}
		}
		else
		{
			buf->in_section = 183;
			memcpy(buf->data, packet + 5, 183);
			/* allocate memory and save data (test end ? ) */
		}
	}
	else
	{
		if (buf->in_section > 0)
		{
			/* append data to buf */
			if (buf->in_section + 184 > DSMCC_PID_BUFFER_SIZE)
			{
				DSMCC_ERROR("Section buffer overflow (buffer is already at %d bytes) (table ID is 0x%02x)", buf->in_section, buf->data[0]);
				memcpy(buf->data + buf->in_section, packet + 4, DSMCC_PID_BUFFER_SIZE - buf->in_section);
				buf->in_section = DSMCC_PID_BUFFER_SIZE;
			}
			else
			{
				memcpy(buf->data + buf->in_section, packet + 4, 184);
				buf->in_section += 184;
			}
		}
		else
		{
			/* TODO error ? */
		}
	}
}

void dsmcc_parse_buffered_sections(struct dsmcc_status *status, struct dsmcc_pid_buffer *pid_buffers)
{
	struct dsmcc_pid_buffer *buf;
	for (buf = pid_buffers; buf != NULL; buf = buf->next)
		dsmcc_parse_section(status, buf->pid, buf->data, buf->in_section);
}
