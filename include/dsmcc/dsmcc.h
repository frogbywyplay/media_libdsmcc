/**
 * \file	dsmcc.h
 * \brief	DSM-CC Object Carousel parser
 * \author	Loic Lefort <llefort@wyplay.com>
 * \version	2.0.0
 * \note \n
 *
 * Version 0.6 - 19-02-2004
 *                   - Initial version by Richard Palmer <richard@tiro.org.uk>
 *                   - imported from http://libdsmcc.sourceforge.net/
 * 
 * Version 2.0.0 - 05-11-2012
 *                   - Refactored/Cleaned-up version for Wyplay
 *
 * \date	05-11-2012
 */

#ifndef LIBDSMCC_H
#define LIBDSMCC_H

#ifdef __cplusplus
extern "C" {
#endif

/** \defgroup logging Logging
 *  \{
 */

/** The different log levels */
enum
{
	DSMCC_LOG_DEBUG = 0,
	DSMCC_LOG_WARN,
	DSMCC_LOG_ERROR
};

/** \brief Callback called by the logging code
  * \param severity the severity of the message
  * \param message the message to be logged
  */
typedef void (dsmcc_logger_t)(int severity, const char *message);

/** \brief Set the logger callback
  * \param logger the callback
  * \param severity the minimum severity for which the callback will be called
  */
void dsmcc_set_logger(dsmcc_logger_t *logger, int severity);

/** \} */ // end of 'logging' group


/** \defgroup main Main DSM-CC API
 *  \{
 */

/** \brief Callback called when the library needs to subscribe to new streams
  * \param arg Opaque argument (passed as-is from dsmcc-open argument stream_sub_callback_arg)
  * \param assoc_tag the Association Tag of the requested stream
  * \return the PID of the requested stream
  */
typedef int (dsmcc_stream_subscribe_callback_t)(void *arg, unsigned short assoc_tag);

/** Opaque type containing the library state */
struct dsmcc_state;

/** \brief Initialize the DSM-CC parser
  * @param tmpdir the temporary directory that will be used. Will be created if not already existent. If NULL, it will default to /tmp/dsmcc-XYZ (where XYZ is the PID of the current process)
  * @param stream_sub_callback the callback that will be called when the library needs to subscribe to new streams
  * @param stream_sub_callbacl_arg this will be passed as-is as first argument to the callback
  */
struct dsmcc_state *dsmcc_open(const char *tmpdir, dsmcc_stream_subscribe_callback_t *stream_sub_callback, void *stream_sub_callback_arg);

/** \brief Parse a MPEG Section
  * @param state the library state
  * @param pid the PID of the stream from which the section originates
  * @param data the section data
  * @param data_length the total length of the data buffer
  */
int dsmcc_parse_section(struct dsmcc_state *state, int pid, unsigned char *data, int data_length);

/** \brief Free the memory used by the library
  * \param state the library state
  */
void dsmcc_close(struct dsmcc_state *state);

/** \} */ // end of 'main' group

/** \defgroup control Download Control
 *  \{
 */

/** Constants for the 'reason' passed to the cache callback */
enum
{
	/** Check if a directory should be created */
	DSMCC_CACHE_DIR_CHECK = 0,

	/** Notification of directory creation */
	DSMCC_CACHE_DIR_SAVED,

	/** Check if a file should be created */
	DSMCC_CACHE_FILE_CHECK,

	/** Notification of file creation */
	DSMCC_CACHE_FILE_SAVED
};

/** \brief Callback called for each directory/file in the carousel
  * \param arg Opaque argument (passed as-is from the cache_callback_arg argument of dsmcc_add_carousel
  * \param cid the carousel ID
  * \param reason the reason
  * \param path the directory/file path in the carousel
  * \param fullpath the directory/file path on disk
  * \return 0/1 for *_CHECK reasons whether the directory/file should be created or skipped. The return value is not used for *_SAVED reasons.
  */
typedef int (dsmcc_cache_callback_t)(void *arg, unsigned long cid, int reason, char *path, char *fullpath);

/** \brief Add a carousel to the list of carousels to be parsed
  * \param state the library state
  * \param pid the PID of the stream where the carousel DSI message will be broadcasted
  * \param downloadpath the directory where the carousel files will be downloaded
  * \param cache_callback the callback that will be called before/after each directory or file creation
  * \param cache_callback_arg this will be passed as-is as first argument to the callback
  */
void dsmcc_add_carousel(struct dsmcc_state *state, int pid, const char *downloadpath, dsmcc_cache_callback_t *cache_callback, void *cache_callback_arg);

/** \} */ // end of 'control' group

/** \defgroup tsparser TS Parser
 *  \{
 */

/** Opaque structure used by the TS Parser */
struct dsmcc_tsparser_buffer;

/** \brief Allocate a buffer for a PID in the list of section buffers used by the TS Parser
  * \param buffers a pointer to the list of buffers
  * \param pid the PID
  */
void dsmcc_tsparser_add_pid(struct dsmcc_tsparser_buffer **buffers, unsigned int pid);

/** \brief Free all the buffers used by the TS Parser
  * \param buffers a pointer to the list of buffers
  */
void dsmcc_tsparser_free_buffers(struct dsmcc_tsparser_buffer **buffers);

/** \brief Parse a single TS packet. It will be added to current sections buffers and in case of complete section, dsmcc_parse_section will be called.
  * \param state the library state
  * \param buffers a pointer to the list of buffers
  * \param packet the packet data
  * \param packet_length the packet length (should be 188)
  */
void dsmcc_tsparser_parse_packet(struct dsmcc_state *state, struct dsmcc_tsparser_buffer **buffers, unsigned char *packet, int packet_length);

/** \brief Call dsmcc_parse_section on all current section buffers.
  * \param state the library state
  * \param buffers a pointer to the list of buffers
  */
void dsmcc_tsparser_parse_buffered_sections(struct dsmcc_state *state, struct dsmcc_tsparser_buffer *buffers);

/** \} */ // end of 'tsparser' group

#ifdef __cplusplus
}
#endif
#endif
