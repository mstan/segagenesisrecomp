#ifndef SEGACD_BACKUP_H
#define SEGACD_BACKUP_H
#include "segacd.h"
typedef enum {SEGACD_BACKUP_OK,SEGACD_BACKUP_NEW,SEGACD_BACKUP_ERROR} SegaCDBackupResult;
/* Missing files leave the device's erased contents intact. Invalid/truncated
 * files are errors and never partially replace guest RAM. UTF-8 host paths. */
SegaCDBackupResult segacd_backup_load(const char *,uint8_t backup[SEGACD_BACKUP_BYTES]);
/* Write, flush and commit a separate file before replacing the existing save.
 * A failed write leaves the previous file in place. */
SegaCDBackupResult segacd_backup_store(const char *,const uint8_t backup[SEGACD_BACKUP_BYTES]);
#endif
