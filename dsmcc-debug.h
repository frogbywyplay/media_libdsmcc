#ifndef DSMCC_DEBUG_H
#define DSMCC_DEBUG_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef DSMCC_DEBUG_ENABLED
#define DSMCC_DEBUG(format, args...) dsmcc_debug(format, ##args)
void dsmcc_debug(char *format, ...);
#else
#define DSMCC_DEBUG(format, args...) do {} while (0)
#endif

#define DSMCC_WARN(format, args...) dsmcc_warn(format, ##args)
void dsmcc_warn(char *format, ...);

#define DSMCC_ERROR(format, args...) dsmcc_error(format, ##args)
void dsmcc_error(char *format, ...);


#ifdef __cplusplus
}
#endif

#endif /* DSMCC_DEBUG_H */
