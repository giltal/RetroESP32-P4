/* Stub for PSRAM app build — no SPI master in papp */
#ifndef PAPP_COMPAT_DRIVER_SPI_MASTER_H
#define PAPP_COMPAT_DRIVER_SPI_MASTER_H

#include <stddef.h>
#include <stdint.h>

typedef void *spi_device_handle_t;

#define TFT_VSPI_HOST  3
#define SPI_DEVICE_NO_DUMMY 0
#define SPI_TRANS_USE_TXDATA 0

typedef struct {
    uint8_t command_bits;
    uint8_t address_bits;
    uint8_t dummy_bits;
    uint8_t mode;
    int clock_speed_hz;
    int spics_io_num;
    uint32_t flags;
    int queue_size;
    void *pre_cb;
    void *post_cb;
    int duty_cycle_pos;
    int cs_ena_pretrans;
    int cs_ena_posttrans;
    int input_delay_ns;
} spi_device_interface_config_t;

typedef struct {
    uint32_t flags;
    uint16_t cmd;
    uint64_t addr;
    size_t length;
    size_t rxlength;
    void *user;
    union { const void *tx_buffer; uint8_t tx_data[4]; };
    union { void *rx_buffer; uint8_t rx_data[4]; };
} spi_transaction_t;

typedef struct {
    int miso_io_num;
    int mosi_io_num;
    int sclk_io_num;
    int quadwp_io_num;
    int quadhd_io_num;
    int max_transfer_sz;
} spi_bus_config_t;

static inline int spi_bus_initialize(int host, const spi_bus_config_t *cfg, int dma) {
    (void)host; (void)cfg; (void)dma; return 0;
}
static inline int spi_bus_add_device(int host, const spi_device_interface_config_t *cfg, spi_device_handle_t *h) {
    (void)host; (void)cfg; (void)h; return 0;
}
static inline int spi_device_queue_trans(spi_device_handle_t h, spi_transaction_t *t, int ticks) {
    (void)h; (void)t; (void)ticks; return 0;
}
static inline int spi_device_get_trans_result(spi_device_handle_t h, spi_transaction_t **t, int ticks) {
    (void)h; (void)t; (void)ticks; return 0;
}

#endif
