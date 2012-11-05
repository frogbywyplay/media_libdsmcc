#ifndef LIBDSMCC_H
#define LIBDSMCC_H

#ifdef __cplusplus
extern "C" {
#endif

/* --- DEBUG --- */

enum
{
	DSMCC_LOG_DEBUG = 0,
	DSMCC_LOG_WARN,
	DSMCC_LOG_ERROR
};
typedef void (dsmcc_logger_t)(int severity, const char *message);
void dsmcc_set_logger(dsmcc_logger_t *logger, int severity);

/* --- MAIN --- */

typedef int (dsmcc_stream_subscribe_callback_t)(void *arg, unsigned short assoc_tag);

struct dsmcc_status;

struct dsmcc_status *dsmcc_open(const char *tmpdir, dsmcc_stream_subscribe_callback_t *stream_sub_callback, void *stream_sub_callback_arg);
int dsmcc_parse_section(struct dsmcc_status *status, int pid, unsigned char *data, int data_length);
void dsmcc_close(struct dsmcc_status *status);

/* --- DOWNLOAD MANAGEMENT --- */

enum
{
	DSMCC_CACHE_DIR_CHECK = 0,
	DSMCC_CACHE_DIR_SAVED,
	DSMCC_CACHE_FILE_CHECK,
	DSMCC_CACHE_FILE_SAVED
};

typedef int (dsmcc_cache_callback_t)(void *arg, unsigned long cid, int reason, char *path, char *fullpath);

void dsmcc_add_carousel(struct dsmcc_status *status, int cid, int pid, const char *downloadpath, dsmcc_cache_callback_t *cache_callback, void *cache_callback_arg);

/* --- TS -> SECTION PARSER --- */

struct dsmcc_pid_buffer;
void dsmcc_add_pid_buffer(struct dsmcc_pid_buffer **pid_buffers, int pid);
void dsmcc_free_pid_buffers(struct dsmcc_pid_buffer **pid_buffers);
void dsmcc_parse_ts_packet(struct dsmcc_status *status, struct dsmcc_pid_buffer **pid_buffers, unsigned char *packet, int packet_length);
void dsmcc_parse_buffered_sections(struct dsmcc_status *status, struct dsmcc_pid_buffer *pid_buffers);

#ifdef __cplusplus
}
#endif
#endif
