#include "ble_server.h"
#include "sensor.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/hci_vs.h>
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

static const struct bt_le_ext_adv_cb adv_cb = {
    .sent = adv_sent,
};

struct bt_le_adv_param adv_param = {
    .id = BT_ID_DEFAULT,
    .sid = BT_GAP_SID_MIN,
    .secondary_max_skip = 0,
    .interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
    .interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
    .options = BT_LE_ADV_OPT_USE_IDENTITY
};

int btadv(void) {

    int err;
    char name[32];
    uint8_t addr[6];
    bt_addr_le_t id;
    size_t count = 1;
    struct bt_le_ext_adv *adv = NULL;


    err = bt_enable(NULL);
    if (err) {
        return err;
    }

    // Get default identity and append last 3 bytes to name
    bt_id_get(&id, &count);
    sys_memcpy_swap(addr, id.a.val, 6);  /* reverse for human-readable */
    snprintk(name, sizeof(name), "BeeEye_%02X%02X%02X", addr[3], addr[4], addr[5]);
    LOG_INF("Announcing as %s", name);

    /* Create a non-connectable advertising set */
	err = bt_le_ext_adv_create(&adv_param, &adv_cb, &adv);
	if (err) {
		return err;
	}

    /* Build custom advertising payload with name and sensor data */
    struct SensorData sensorData;
    err = hdc2080_get_temp_humidity(&sensorData);
    if(err) {
        return err;
    }

    const struct bt_data ad[] = {
        BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
        BT_DATA(BT_DATA_NAME_COMPLETE, name, strlen(name)),
        BT_DATA(BT_DATA_MANUFACTURER_DATA, &sensorData, sizeof(sensorData)),
    };

    err = bt_le_ext_adv_set_data(adv, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        return err;
    }

    /* Start advertising (send 4 ads only)*/
    err = bt_le_ext_adv_start(adv, BT_LE_EXT_ADV_START_PARAM(0, 4));
    if(err) {
        LOG_ERR("Failed to start announce: %d", err);
        return err;
    }

    // wait until done (see adv_cb and adv_sent)
    k_sem_take(&adv_done, K_FOREVER);


    err = bt_disable();
    if(err) {
        LOG_ERR("Failed to disable BT: %d", err);
        return err;
    }

    return 0;
}