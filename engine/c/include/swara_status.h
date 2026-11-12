#ifndef SWARA_STATUS_H
#define SWARA_STATUS_H
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    SWARA_OK = 0,
    SWARA_ERR_ARG = -1,   /* null pointer or invalid parameter */
    SWARA_ERR_FULL = -2,  /* not enough free space */
    SWARA_ERR_EMPTY = -3  /* not enough data available */
} swara_status;
#ifdef __cplusplus
}
#endif
#endif
