#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/hci_vs.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/sys/timeutil.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/state.h>
#include <zephyr/sys/reboot.h>
#include <hal/nrf_ficr.h>
#include <hal/nrf_power.h>

#include "sensor.h"
//#define NOBLINK

int blink(int count, int duration) {
    #ifndef NOBLINK
    const struct device *port = DEVICE_DT_GET(DT_NODELABEL(gpio0));
    if (!device_is_ready(port)) return -ENODEV;
    gpio_pin_configure(port, 11, GPIO_OUTPUT_INACTIVE);
    for(int i=0;i<count;++i) {
        gpio_pin_toggle(port, 11);
        k_msleep(duration);
        gpio_pin_toggle(port, 11);
        k_msleep(duration);
    }
    #endif
    
    return 0;
}

int megablink(int countA, int countB, int spacing) {
    #ifndef NOBLINK
    for(int i=0;i<countB;++i) {
        blink(countA, 200);
        k_msleep(spacing);
    }
    #endif
    return 0;
}


int intinitialize_rtc(bool setTime);
int set_alarm_and_sleep(int minutes);
int btadv(void);

__attribute__((section(".noinit"))) static uint32_t boot_resetreas;

static int capture_resetreas(void)
{
    uint32_t reas = nrf_power_resetreas_get(NRF_POWER);
    boot_resetreas = reas;
    nrf_power_resetreas_clear(NRF_POWER, 0xFFFFFFFF);
    return 0;
}
SYS_INIT(capture_resetreas, PRE_KERNEL_1, 0);


int xmain(void) {
    nrf_power_dcdcen_set(NRF_POWER, false);
    nrf_power_system_off(NRF_POWER);
    while(1);
}

static const struct gpio_dt_spec btn = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

int main(void)
{
    if (!(boot_resetreas & NRF_POWER_RESETREAS_OFF_MASK)) {
        k_msleep(500); // sleep for half a second (let bulk capacitor charge up)
        blink(1,100);  // indicator that thing is working
        k_msleep(500); // more sleep for capacitor
    }

    // If we are woken up by a button press then reset the clock 
    // because alarm is aligned to minute boundary
    bool button_is_pressed = gpio_pin_get_dt(&btn) == 0; // SW0 is active-low
    if(intinitialize_rtc(button_is_pressed)) {
        megablink(3,3, 300);
        k_msleep(5000);
        sys_reboot(SYS_REBOOT_COLD);
        return 0;
    }

    if(btadv()) {
        megablink(4,4, 300);
        k_msleep(5000);
        sys_reboot(SYS_REBOOT_COLD);
        return 0;
    }

    int status = set_alarm_and_sleep(2);
    if(status) {
        megablink(2,2, 100);
        k_msleep(5000);
        sys_reboot(SYS_REBOOT_COLD);
    }

    return 0;
}

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

int btadv(void)
{

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



    /* Create a non-connectable advertising set */
	err = bt_le_ext_adv_create(&adv_param, &adv_cb, &adv);
	if (err) {
		return err;
	}

    /* Build custom advertising payload with name and sensor data */
    float temp, hum;
    err = hdc2080_get_temp_humidity(&temp, &hum);
    if(err) {
        return err;
    }
    int16_t temp_i = (int16_t)(temp * 256.0f);
    int16_t hum_i  = (int16_t)(hum  * 256.0f);

    struct {
        uint16_t magic;
        int16_t temp;
        int16_t hum;
    } __packed sensor_payload = {
        .magic = 0xBEEE,
        .temp = sys_cpu_to_le16(temp_i),
        .hum  = sys_cpu_to_le16(hum_i),
    };

    const struct bt_data ad[] = {
        BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
        BT_DATA(BT_DATA_NAME_COMPLETE, name, strlen(name)),
        BT_DATA(BT_DATA_MANUFACTURER_DATA, &sensor_payload, sizeof(sensor_payload)),
    };

    err = bt_le_ext_adv_set_data(adv, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        return err;
    }

    /* Start advertising (send 4 ads only)*/
    bt_le_ext_adv_start(adv, BT_LE_EXT_ADV_START_PARAM(0, 4));
    k_sem_take(&adv_done, K_FOREVER);
    bt_disable();

    return 0;
}




