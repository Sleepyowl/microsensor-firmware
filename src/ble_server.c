#include "ble_server.h"
#include "sensor_hdc2080.h"
#include "rtc.h"
#include "vsense.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/hci_vs.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(app_ble_server, LOG_LEVEL_DBG);

// int adv_set_tx_power(struct bt_le_ext_adv *adv, int8_t dbm)
// {
//     struct net_buf *buf, *rsp = NULL;
//     struct bt_hci_cp_vs_write_tx_power_level *cp;
//     int err;

//     uint16_t handle = bt_le_ext_adv_get_index(adv); // adv handle (per set)

//     buf = bt_hci_cmd_create(BT_HCI_OP_VS_WRITE_TX_POWER_LEVEL, sizeof(*cp));
//     if (!buf) return -ENOBUFS;

//     cp = net_buf_add(buf, sizeof(*cp));
//     cp->handle_type   = BT_HCI_VS_LL_HANDLE_TYPE_ADV;
//     cp->handle        = sys_cpu_to_le16(handle);
//     cp->tx_power_level = dbm;     // e.g. 0, -4, -8 ... (SDC rounds to supported)

//     err = bt_hci_cmd_send_sync(BT_HCI_OP_VS_WRITE_TX_POWER_LEVEL, buf, &rsp);
//     if (rsp) net_buf_unref(rsp);
//     return err;
// }


static K_SEM_DEFINE(adv_done, 0, 1);

static void adv_sent(struct bt_le_ext_adv *adv,
                     struct bt_le_ext_adv_sent_info *info)
{
    k_sem_give(&adv_done);
}

static void adv_connected(struct bt_le_ext_adv *adv,
			  struct bt_le_ext_adv_connected_info *info) {
    LOG_DBG("BLE connection");
}

static const struct bt_le_ext_adv_cb adv_cb = {
    .sent = adv_sent,
    .connected = adv_connected
};

struct bt_le_adv_param adv_param_normal = {
    .id = BT_ID_DEFAULT,
    .sid = BT_GAP_SID_MIN,
    .secondary_max_skip = 0,
    .interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
    .interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
    .options = BT_LE_ADV_OPT_USE_IDENTITY | BT_LE_ADV_OPT_EXT_ADV 
};

struct bt_le_adv_param adv_param_pair = {
    .id = BT_ID_DEFAULT,
    .sid = BT_GAP_SID_MIN,
    .secondary_max_skip = 0,
    .interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
    .interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
    .options = BT_LE_ADV_OPT_USE_IDENTITY | BT_LE_ADV_OPT_EXT_ADV | BT_LE_ADV_OPT_CONN
};

/* Characteristic storage */
static int16_t g_temperature = 0;
static int16_t g_humidity = 0;

/* Read callbacks */
static ssize_t read_temperature(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &g_temperature, sizeof(g_temperature));
}

static ssize_t read_humidity(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &g_humidity, sizeof(g_humidity));
}

static ssize_t read_timestamp(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset)
{
    uint32_t ts = 0;
    get_rtc_unix_time(&ts);
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &ts, sizeof(ts));
}

static ssize_t read_currenttime(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset)
{
    struct cts_current_time cts;
    memset(&cts, 0, sizeof(cts));
    get_rtc_cts_time(&cts);
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &cts, sizeof(cts));
}


/* Write callbacks */
static ssize_t write_timestamp(struct bt_conn *conn,
                        const struct bt_gatt_attr *attr,
                        const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    if (offset != 0 || len != sizeof(uint32_t)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    uint32_t timestamp = sys_get_le32(buf);
    LOG_DBG("Writing timestamp: %u", timestamp);
    set_rtc_unix_time(timestamp);

    return len;
}

static ssize_t write_currenttime(struct bt_conn *conn,
                        const struct bt_gatt_attr *attr,
                        const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    if (offset != 0 || len != sizeof(struct cts_current_time)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    const struct cts_current_time *time = buf;
    LOG_DBG("Writing current time: %04u-%02u-%02u %02u:%02u:%02u",
            sys_le16_to_cpu(time->year),
            time->month,
            time->day,
            time->hours,
            time->minutes,
            time->seconds);
    set_rtc_cts_time(time);

    return len;
}

/* UUIDs */
#define BT_UUID_MY_SERVICE_VAL 0x12,0x34,0x00,0x01,0x00,0x00,0x10,0x00,0x80,0x00,0x00,0x80,0x5F,0x9B,0x34,0xFB
#define BT_UUID_MY_SERVICE       BT_UUID_DECLARE_128(BT_UUID_MY_SERVICE_VAL)
#define BT_UUID_TEMP_CHAR        BT_UUID_DECLARE_128(0x12,0x34,0x00,0x02,0x00,0x00,0x10,0x00,0x80,0x00,0x00,0x80,0x5F,0x9B,0x34,0xFB)
#define BT_UUID_HUMID_CHAR       BT_UUID_DECLARE_128(0x12,0x34,0x00,0x03,0x00,0x00,0x10,0x00,0x80,0x00,0x00,0x80,0x5F,0x9B,0x34,0xFB)
#define BT_UUID_TIME_CHAR        BT_UUID_DECLARE_128(0x12,0x34,0x00,0x04,0x00,0x00,0x10,0x00,0x80,0x00,0x00,0x80,0x5F,0x9B,0x34,0xFB)

/* Service declaration */
BT_GATT_SERVICE_DEFINE(sensor_service,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_MY_SERVICE),

    BT_GATT_CHARACTERISTIC(BT_UUID_TEMP_CHAR,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ,
                           read_temperature, NULL, NULL),

    BT_GATT_CHARACTERISTIC(BT_UUID_HUMID_CHAR,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ,
                           read_humidity, NULL, NULL),

    BT_GATT_CHARACTERISTIC(BT_UUID_TIME_CHAR,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                           read_timestamp, write_timestamp, NULL)
);

BT_GATT_SERVICE_DEFINE(time_service,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_CTS),

    BT_GATT_CHARACTERISTIC(BT_UUID_CTS_CURRENT_TIME,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                           read_currenttime, write_currenttime, NULL)
);

#define BEEEYE_MAGIC 0xBEEE
struct __attribute__((packed)) ManufacturerData {
    uint16_t            magic;
    int16_t             temp;
    int16_t             hum;
    uint32_t            nextWindow;
    uint16_t            batteryMilliVolt;
} manufacturerData;

static char name[32];
int set_adv_data(struct bt_le_ext_adv *adv) {
    int err = 0;
    int16_t temp = 0;
    int16_t hum = 0;
    uint16_t mv = 0;
    // Build manufacturer data
    if(manufacturerData.magic == 0) {
        manufacturerData.magic = BEEEYE_MAGIC;
        err = hdc2080_get_temp_humidity_x256(&temp, &hum);
        if(err) {
            LOG_ERR("Couldn't read sensor: %d", err);
            return err;
        } else {
            manufacturerData.temp = temp;
            manufacturerData.hum = hum;
        }

        err = vsense_measure_mv(&mv);
        if(err) {
            LOG_ERR("Couldn't get battery voltage %d", err);
        } else {
            manufacturerData.batteryMilliVolt = mv;
        }
    }

    uint32_t rtc_now = 0;
    err = get_rtc_unix_time(&rtc_now);
    if(err) {
        return err;
    }

    uint16_t timer_status = 0;
    if(get_rtc_pit_timer_status(&timer_status)) {
        LOG_ERR("Failed to get RTC time status");
    }
    manufacturerData.nextWindow = ((uint64_t)timer_status) * 15625 / 1000;

    const struct bt_data ad[] = {
        BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
        BT_DATA(BT_DATA_NAME_COMPLETE, name, strlen(name)),
        BT_DATA(BT_DATA_MANUFACTURER_DATA, &manufacturerData, sizeof(manufacturerData)),
        BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_MY_SERVICE_VAL)
    };

    err = bt_le_ext_adv_set_data(adv, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        LOG_ERR("Couldn't set adv data: %d", err);
        return err;
    }
    return 0;
}

static void count_conn_cb(struct bt_conn *conn, void *user_data)
{
    int *count = user_data;

    if (bt_conn_is_type(conn, BT_CONN_TYPE_LE)) {
        (*count)++;
    }
}

static int le_conn_count(void)
{
    int count = 0;

    bt_conn_foreach(BT_CONN_TYPE_LE, count_conn_cb, &count);
    return count;
}

int btadv(bool pairing) {
    int err;
    uint8_t addr[6];
    bt_addr_le_t id;
    size_t count = 1;
    struct bt_le_ext_adv *adv = NULL;

    err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Couldn't enable BT: %d", err);
        return err;
    }

    // Get default identity and append last 3 bytes to name
    bt_id_get(&id, &count);
    sys_memcpy_swap(addr, id.a.val, 6);  /* reverse for human-readable */
    snprintk(name, sizeof(name), "BeeEye_%02X%02X%02X", addr[3], addr[4], addr[5]);
    LOG_INF("Announcing as %s", name);

    // Create adv set
    // const struct bt_le_adv_param *adv_param = pairing ? &adv_param_pair : &adv_param_normal;
	err = bt_le_ext_adv_create(&adv_param_pair, &adv_cb, &adv);
	if (err) {
        LOG_ERR("Couldn't create adv: %d", err);
		return err;
	}

    memset(&manufacturerData, 0, sizeof(manufacturerData));
    err = set_adv_data(adv);
    if (err) {
        LOG_ERR("Couldn't set manufacturer data: %d", err);
        return err;
    }

    // Normal adv mode: advertise 4 times
    // "Pairing" adv mode: advertise for 30 seconds
    const struct bt_le_ext_adv_start_param adv_start_normal = BT_LE_EXT_ADV_START_PARAM_INIT(0,4);
    const struct bt_le_ext_adv_start_param adv_start_pairing = BT_LE_EXT_ADV_START_PARAM_INIT(300,0);
    const struct bt_le_ext_adv_start_param *adv_start = pairing ? &adv_start_pairing : &adv_start_normal;
    LOG_DBG("adv start param = {timeout=%dms, num=%d}", adv_start->timeout * 10, adv_start->num_events);
    err = bt_le_ext_adv_start(adv, adv_start);
    if(err) {
        LOG_ERR("Failed to start announce: %d", err);
        return err;
    }

    if(adv_start->num_events != 0) {
        // adv_sent() releases the semaphore
        // k_sem_take(&adv_done, K_FOREVER);
        while(k_sem_take(&adv_done, K_NO_WAIT) == -EBUSY) {
            err = set_adv_data(adv);
            if (err) {
                LOG_ERR("Couldn't set manufacturer data: %d", err);
                return err;
            }
            k_msleep(100);
        }
    } else if(adv_start->timeout != 0) {
        // k_msleep(adv_start->timeout * 10);
        const uint64_t until = k_uptime_get() + adv_start->timeout * 10;
        while(1) {
            err = set_adv_data(adv);
            if (err) {
                LOG_ERR("Couldn't set manufacturer data: %d", err);
                return err;
            }
            const uint64_t now = k_uptime_get();
            if(now >= until) {
                break;
            }
            k_msleep(100);
        }
    }

    err = bt_le_ext_adv_stop(adv);
    if (err && err != -EALREADY) {
        LOG_WRN("bt_le_ext_adv_stop failed: %d", err);        
    }

    while (le_conn_count() > 0) {
        LOG_DBG("Waiting for BLE connection(s) to close...");
        k_msleep(200);
    }

    err = bt_disable();
    if(err) {
        LOG_ERR("Failed to disable BT: %d", err);
        return err;
    }

    return 0;
}