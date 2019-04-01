#include <stdbool.h>
#include <stdint.h>
#include "nordic_common.h"
#include "bsp.h"
#include "nrf_gpio.h"
#include "nrf_soc.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "ble_advdata.h"
#include "app_timer.h"
#include "nrf_pwr_mgmt.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "nrf_drv_twi.h"
#include "nrf_delay.h"

#define APP_BLE_CONN_CFG_TAG            1                                  /**< A tag identifying the SoftDevice BLE configuration. */

#define NON_CONNECTABLE_ADV_INTERVAL    BLE_GAP_ADV_INTERVAL_MAX

#define APP_BEACON_INFO_LENGTH          0x17                               /**< Total length of information advertised by the Beacon. */
#define APP_ADV_DATA_LENGTH             0x15                               /**< Length of manufacturer specific data in the advertisement. */
#define APP_DEVICE_TYPE                 0x02                               /**< 0x02 refers to Beacon. */
#define APP_MEASURED_RSSI               0xC3                               /**< The Beacon's measured RSSI at 1 meter distance in dBm. */
#define APP_COMPANY_IDENTIFIER          0x0059                             /**< Company identifier for Nordic Semiconductor ASA. as per www.bluetooth.org. */
#define APP_MAJOR_VALUE                 0x01, 0x02                         /**< Major value used to identify Beacons. */
#define APP_MINOR_VALUE                 0x03, 0x04                         /**< Minor value used to identify Beacons. */
#define APP_BEACON_UUID                 0x01, 0x12, 0x23, 0x34, \
                                        0x45, 0x56, 0x67, 0x78, \
                                        0x89, 0x9a, 0xab, 0xbc, \
                                        0xcd, 0xde, 0xef, 0xf0            /**< Proprietary UUID for Beacon. */

#define DEAD_BEEF                       0xDEADBEEF                         /**< Value used as error code on stack dump, can be used to identify stack location on stack unwind. */

#if defined(USE_UICR_FOR_MAJ_MIN_VALUES)
#define MAJ_VAL_OFFSET_IN_BEACON_INFO   18                                 /**< Position of the MSB of the Major Value in m_beacon_info array. */
#define UICR_ADDRESS                    0x10001080                         /**< Address of the UICR register used by this example. The major and minor versions to be encoded into the advertising data will be picked up from this location. */
#endif

#define SENSOR_ADDR          (0x40U >> 0)

APP_TIMER_DEF(m_timer_id);
APP_TIMER_DEF(m_timer_read_id);
static const nrf_drv_twi_t m_twi = NRF_DRV_TWI_INSTANCE(0);

static ble_gap_adv_params_t m_adv_params;                                  /**< Parameters to be passed to the stack when starting advertising. */
static uint8_t              m_adv_handle = BLE_GAP_ADV_SET_HANDLE_NOT_SET; /**< Advertising handle used to identify an advertising set. */
static uint8_t              m_enc_advdata[BLE_GAP_ADV_SET_DATA_SIZE_MAX];  /**< Buffer for storing an encoded advertising set. */

static ble_gap_adv_data_t m_adv_data = {
    .adv_data = {
        .p_data = m_enc_advdata,
        .len    = BLE_GAP_ADV_SET_DATA_SIZE_MAX
    },
    .scan_rsp_data = {
        .p_data = NULL,
        .len    = 0
    }
};


static uint8_t m_beacon_info[APP_BEACON_INFO_LENGTH] =                    /**< Information advertised by the Beacon. */
{
    APP_DEVICE_TYPE,     // Manufacturer specific information. Specifies the device type in this
                         // implementation.
    APP_ADV_DATA_LENGTH, // Manufacturer specific information. Specifies the length of the
                         // manufacturer specific data in this implementation.
    APP_BEACON_UUID,     // 128 bit UUID value.
    APP_MAJOR_VALUE,     // Major arbitrary value that can be used to distinguish between Beacons.
    APP_MINOR_VALUE,     // Minor arbitrary value that can be used to distinguish between Beacons.
    APP_MEASURED_RSSI    // Manufacturer specific information. The Beacon's measured TX power in
                         // this implementation.
};

void assert_nrf_callback(uint16_t line_num, const uint8_t * p_file_name) {
    app_error_handler(DEAD_BEEF, line_num, p_file_name);
}

static void advertising_init(void) {
    ble_advdata_t advdata;
    uint8_t       flags = BLE_GAP_ADV_FLAG_BR_EDR_NOT_SUPPORTED;

    ble_advdata_manuf_data_t manuf_specific_data;

    manuf_specific_data.company_identifier = APP_COMPANY_IDENTIFIER;

    manuf_specific_data.data.p_data = (uint8_t *) m_beacon_info;
    manuf_specific_data.data.size   = APP_BEACON_INFO_LENGTH;

    memset(&advdata, 0, sizeof(advdata));
    advdata.name_type             = BLE_ADVDATA_NO_NAME;
    advdata.flags                 = flags;
    advdata.p_manuf_specific_data = &manuf_specific_data;

    memset(&m_adv_params, 0, sizeof(m_adv_params));
    m_adv_params.properties.type = BLE_GAP_ADV_TYPE_NONCONNECTABLE_NONSCANNABLE_UNDIRECTED;
    m_adv_params.p_peer_addr     = NULL;    // Undirected advertisement.
    m_adv_params.filter_policy   = BLE_GAP_ADV_FP_ANY;
    m_adv_params.interval        = NON_CONNECTABLE_ADV_INTERVAL;
    m_adv_params.duration        = 1; // only send one advertisement

    APP_ERROR_CHECK(ble_advdata_encode(&advdata, m_adv_data.adv_data.p_data, &m_adv_data.adv_data.len));
    APP_ERROR_CHECK(sd_ble_gap_adv_set_configure(&m_adv_handle, &m_adv_data, &m_adv_params));
}

static void advertising_start(void) {
    APP_ERROR_CHECK(sd_ble_gap_adv_start(m_adv_handle, APP_BLE_CONN_CFG_TAG));
}

static void ble_stack_init(void) {
    APP_ERROR_CHECK(nrf_sdh_enable_request());

    uint32_t ram_start = 0;
    APP_ERROR_CHECK(nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start));
    APP_ERROR_CHECK(nrf_sdh_ble_enable(&ram_start));
}

static volatile bool m_xfer_done = false;
static struct {
    uint16_t temp;
    uint16_t humidity;
} m_sample;

static void read_event(void * p_context) {
    //nrf_gpio_pin_toggle(5);
    uint8_t reg = 0x0;

    m_xfer_done = false;
    APP_ERROR_CHECK(nrf_drv_twi_tx(&m_twi, SENSOR_ADDR, &reg, 1, true));
    while (m_xfer_done == false);

    m_xfer_done = false;
    APP_ERROR_CHECK(nrf_drv_twi_rx(&m_twi, SENSOR_ADDR, (uint8_t*) &m_sample, sizeof(m_sample)));
    while (m_xfer_done == false);

    int16_t temp = m_sample.temp * 165 * 100 / UINT16_MAX - 4000;
    int16_t humidity = m_sample.humidity * 100 / UINT16_MAX;
    NRF_LOG_INFO("Temperature: %d (%x), Humidity %d (%x)",
        temp, m_sample.temp, humidity, m_sample.humidity);
    m_beacon_info[18] = temp >> 8;
    m_beacon_info[19] = temp & 0xff;
    m_beacon_info[20] = humidity >> 8;
    m_beacon_info[21] = humidity & 0xff;

    // nrf_pwr_mgmt_shutdown(NRF_PWR_MGMT_SHUTDOWN_GOTO_SYSOFF);

    advertising_init();
    advertising_start();
}

static void start_event(void * p_context) {
    //nrf_gpio_pin_toggle(5);
    uint8_t regs[] = {0x0f, 0x01};

    m_xfer_done = false;
    APP_ERROR_CHECK(nrf_drv_twi_tx(&m_twi, SENSOR_ADDR, regs, 2, false));
    while (m_xfer_done == false);

    // give sensor time to measure (~700us needed)
    app_timer_start(m_timer_read_id, APP_TIMER_TICKS(1), NULL);
}

void twi_handler(nrf_drv_twi_evt_t const * p_event, void * p_context) {
    switch (p_event->type) {
        case NRF_DRV_TWI_EVT_DONE:
            m_xfer_done = true;
            break;
        default:
            break;
    }
}

void twi_init (void) {
    const nrf_drv_twi_config_t twi_config = {
       .scl                = 9,
       .sda                = 6,
       .frequency          = NRF_DRV_TWI_FREQ_100K,
       .interrupt_priority = APP_IRQ_PRIORITY_HIGH,
       .clear_bus_init     = false
    };

    APP_ERROR_CHECK(nrf_drv_twi_init(&m_twi, &twi_config, twi_handler, NULL));

    nrf_drv_twi_enable(&m_twi);
}

int main(void) {
    nrf_gpio_cfg_output(5);

    APP_ERROR_CHECK(NRF_LOG_INIT(NULL));
    NRF_LOG_DEFAULT_BACKENDS_INIT();

    APP_ERROR_CHECK(app_timer_init());
    APP_ERROR_CHECK(app_timer_create(&m_timer_id, APP_TIMER_MODE_REPEATED, start_event));
    APP_ERROR_CHECK(app_timer_create(&m_timer_read_id, APP_TIMER_MODE_SINGLE_SHOT, read_event));

    APP_ERROR_CHECK(bsp_init(BSP_INIT_LEDS, NULL));
    twi_init();
    APP_ERROR_CHECK(nrf_pwr_mgmt_init());

    ble_stack_init();
    advertising_init();

    NRF_LOG_INFO("Beacon example started.");
    advertising_start();

    // reset
    uint8_t regs[] = {0x0e, 0x80};
    m_xfer_done = false;
    APP_ERROR_CHECK(nrf_drv_twi_tx(&m_twi, SENSOR_ADDR, regs, 2, false));
    while (m_xfer_done == false);

    app_timer_start(m_timer_id, APP_TIMER_TICKS(5000), NULL);

    for (;; ) {
        if (NRF_LOG_PROCESS() == false)
            NRF_UARTE0->ENABLE = UARTE_ENABLE_ENABLE_Disabled;
            nrf_pwr_mgmt_run();
            NRF_UARTE0->ENABLE = UARTE_ENABLE_ENABLE_Enabled;
    }
}
