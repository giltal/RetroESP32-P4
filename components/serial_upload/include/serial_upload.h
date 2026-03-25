#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start a background task that monitors USB Serial JTAG for file upload
 * requests. Call once from app_main() after SD card is mounted.
 */
void serial_upload_init(void);

/**
 * Stop the upload task and uninstall the USB Serial JTAG driver.
 * Call serial_upload_init() to restart.
 */
void serial_upload_deinit(void);

#ifdef __cplusplus
}
#endif
