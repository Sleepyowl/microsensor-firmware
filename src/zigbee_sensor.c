#include "zigbee_sensor.h"
#include "sensor_hdc2080.h"
#include "vsense.h"
#include "version.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <zboss_api.h>
#include <zboss_api_addons.h>
#include <zigbee/zigbee_error_handler.h>
#include <zigbee/zigbee_app_utils.h>
#include <zb_nrf_platform.h>

#include <zcl/zb_zcl_temp_measurement.h>
#include <zcl/zb_zcl_rel_humidity_measurement.h>
#include <zcl/zb_zcl_reporting.h>
#include <zcl/zb_zcl_power_config.h>
#include <ha/zb_ha_device_config.h>

/**
 * @brief Register the Zigbee sensor endpoint and initialize cluster attributes.
 */
void zigbee_sensor_init(void);

/**
 * @brief Update ZCL attribute values with a fresh sensor reading.
 *
 * Must be called from the ZBOSS scheduler context (e.g. from an alarm
 * callback).
 *
 * @param temp_zcl  Temperature in ZCL units (0.01 °C per LSB).
 * @param hum_zcl   Relative humidity in ZCL units (0.01 % per LSB).
 */
void zigbee_sensor_update(int16_t temp_zcl, uint16_t hum_zcl);

/**
 * @brief Schedule the first periodic sensor measurement.
 *
 * Safe to call multiple times – starts the alarm only once.
 */
void zigbee_sensor_start_periodic(void);



LOG_MODULE_REGISTER(zigbee_sensor, LOG_LEVEL_INF);

/* ── Attribute counts ──────────────────────────────────────────────────── */
/* Use numeric literals (not macros) in ZB_DECLARE_SIMPLE_DESC and
 * ZB_AF_SIMPLE_DESC_TYPE – those macros use token-pasting (##) and
 * CAT5 differently, so macro names would produce mismatched type names. */
#define SENSOR_IN_CLUSTER_NUM    5
#define SENSOR_OUT_CLUSTER_NUM   0
#define SENSOR_REPORT_ATTR_COUNT \
	(ZB_ZCL_TEMP_MEASUREMENT_REPORT_ATTR_COUNT + \
	 ZB_ZCL_REL_HUMIDITY_MEASUREMENT_REPORT_ATTR_COUNT +\
	 2) /* BatteryVoltage + BatteryPercentageRemaining */

// extended basic attrs structure
typedef struct basic_attr_ex_s
{
    zb_uint8_t zcl_version;
    zb_uint8_t power_source;
    zb_char_t manufacturer_name[33]; // ZCL string: [len][data...], max 32 chars
    zb_char_t model_id[33];
} basic_attr_ex_t;

/* ── Device context: attribute storage ────────────────────────────────── */
struct sensor_device_ctx {
	basic_attr_ex_t            		basic_attr;
	zb_zcl_identify_attrs_t         identify_attr;
	zb_zcl_temp_measurement_attrs_t temp_attr;

	/* Humidity attributes (no addon struct, declared inline) */
	zb_uint16_t hum_value;
	zb_uint16_t hum_min;
	zb_uint16_t hum_max;

	/* Battery Power Configuration cluster attributes */
	zb_uint8_t batt_voltage_100mv; /* measured, 100 mV per LSB  */
	zb_uint8_t batt_percentage;    /* 0.5 % per LSB, 200 = 100% */
};

static struct sensor_device_ctx dev_ctx;

/* ── Attribute list declarations ───────────────────────────────────────── */
ZB_ZCL_START_DECLARE_ATTRIB_LIST_CLUSTER_REVISION(basic_attr_list, ZB_ZCL_BASIC)

    ZB_ZCL_SET_ATTR_DESC(
        ZB_ZCL_ATTR_BASIC_ZCL_VERSION_ID,
        &dev_ctx.basic_attr.zcl_version
    )

    ZB_ZCL_SET_ATTR_DESC(
        ZB_ZCL_ATTR_BASIC_POWER_SOURCE_ID,
        &dev_ctx.basic_attr.power_source
    )

    ZB_ZCL_SET_ATTR_DESC(
        ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
        dev_ctx.basic_attr.manufacturer_name
    )

    ZB_ZCL_SET_ATTR_DESC(
        ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
        dev_ctx.basic_attr.model_id
    )

ZB_ZCL_FINISH_DECLARE_ATTRIB_LIST;

ZB_ZCL_DECLARE_IDENTIFY_ATTRIB_LIST(
	identify_attr_list,
	&dev_ctx.identify_attr.identify_time);

ZB_ZCL_DECLARE_TEMP_MEASUREMENT_ATTRIB_LIST(
	temp_attr_list,
	&dev_ctx.temp_attr.measure_value,
	&dev_ctx.temp_attr.min_measure_value,
	&dev_ctx.temp_attr.max_measure_value,
	&dev_ctx.temp_attr.tolerance);

ZB_ZCL_DECLARE_REL_HUMIDITY_MEASUREMENT_ATTRIB_LIST(
	hum_attr_list,
	&dev_ctx.hum_value,
	&dev_ctx.hum_min,
	&dev_ctx.hum_max);

/* ZB_ZCL_DECLARE_POWER_CONFIG_BATTERY_ATTRIB_LIST_EXT is broken (passes the
 * literal token "bat_num" instead of an empty argument, mangling all IDs).
 * Declare manually using the inner per-attribute macros with empty bat_num. */
ZB_ZCL_START_DECLARE_ATTRIB_LIST_CLUSTER_REVISION(power_config_attr_list,
						   ZB_ZCL_POWER_CONFIG)
	ZB_SET_ATTR_DESCR_WITH_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID(
		&dev_ctx.batt_voltage_100mv, ),
	ZB_SET_ATTR_DESCR_WITH_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID(
		&dev_ctx.batt_percentage, ),
ZB_ZCL_FINISH_DECLARE_ATTRIB_LIST;

/* ── Cluster list ──────────────────────────────────────────────────────── */
zb_zcl_cluster_desc_t sensor_clusters[] = {
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_BASIC,
		ZB_ZCL_ARRAY_SIZE(basic_attr_list, zb_zcl_attr_t),
		(basic_attr_list),
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_MANUF_CODE_INVALID),
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_IDENTIFY,
		ZB_ZCL_ARRAY_SIZE(identify_attr_list, zb_zcl_attr_t),
		(identify_attr_list),
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_MANUF_CODE_INVALID),
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
		ZB_ZCL_ARRAY_SIZE(temp_attr_list, zb_zcl_attr_t),
		(temp_attr_list),
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_MANUF_CODE_INVALID),
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT,
		ZB_ZCL_ARRAY_SIZE(hum_attr_list, zb_zcl_attr_t),
		(hum_attr_list),
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_MANUF_CODE_INVALID),
	ZB_ZCL_CLUSTER_DESC(
		ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
		ZB_ZCL_ARRAY_SIZE(power_config_attr_list, zb_zcl_attr_t),
		(power_config_attr_list),
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_MANUF_CODE_INVALID),
};

/* ── Simple descriptor ─────────────────────────────────────────────────── */
ZB_DECLARE_SIMPLE_DESC(5, 0);

static ZB_AF_SIMPLE_DESC_TYPE(5, 0) sensor_simple_desc = {
	SENSOR_ENDPOINT,
	ZB_AF_HA_PROFILE_ID,
	ZB_HA_TEMPERATURE_SENSOR_DEVICE_ID,
	0,  /* device version */
	0,  /* reserved */
	SENSOR_IN_CLUSTER_NUM,
	SENSOR_OUT_CLUSTER_NUM,
	{
		ZB_ZCL_CLUSTER_ID_BASIC,
		ZB_ZCL_CLUSTER_ID_IDENTIFY,
		ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
		ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT,
		ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
	}
};

/* ── Reporting context and endpoint descriptor ─────────────────────────── */
ZBOSS_DEVICE_DECLARE_REPORTING_CTX(sensor_report_ctx, SENSOR_REPORT_ATTR_COUNT);

ZB_AF_DECLARE_ENDPOINT_DESC(
	sensor_ep,
	SENSOR_ENDPOINT,
	ZB_AF_HA_PROFILE_ID,
	0,
	NULL,
	ZB_ZCL_ARRAY_SIZE(sensor_clusters, zb_zcl_cluster_desc_t),
	sensor_clusters,
	(zb_af_simple_desc_1_1_t *)&sensor_simple_desc,
	SENSOR_REPORT_ATTR_COUNT, sensor_report_ctx,
	0, NULL);

ZBOSS_DECLARE_DEVICE_CTX_1_EP(sensor_device_ctx, sensor_ep);

/* ── CR2032 voltage → ZCL percentage ───────────────────────────────────── */
/* ZCL BatteryPercentageRemaining: 0.5 % per LSB (200 = 100 %, 0 = 0 %).
 * Lookup table derived from typical CR2032 discharge curve. */
static zb_uint8_t batt_mv_to_zcl_pct(uint16_t mv)
{
	static const struct {
		uint16_t mv;
		uint8_t  pct; /* 0–100 % */
	} curve[] = {
		{ 3000, 100 },
		{ 2900,  80 },
		{ 2800,  60 },
		{ 2700,  40 },
		{ 2600,  20 },
		{ 2500,  10 },
		{ 2200,   5 },
		{ 2000,   0 },
	};

	if (mv >= curve[0].mv) {
		return 200; /* 100 % */
	}

	for (int i = 0; i < (int)ARRAY_SIZE(curve) - 1; i++) {
		if (mv >= curve[i + 1].mv) {
			/* linear interpolation between two table points */
			uint16_t range_mv  = curve[i].mv - curve[i + 1].mv;
			uint8_t  range_pct = curve[i].pct - curve[i + 1].pct;
			uint16_t offset    = mv - curve[i + 1].mv;
			uint8_t  pct = curve[i + 1].pct +
				       (uint8_t)((uint32_t)offset * range_pct / range_mv);
			return (zb_uint8_t)(pct * 2); /* convert to 0.5 %/LSB */
		}
	}

	return 0;
}

/* ── Periodic measurement ──────────────────────────────────────────────── */
static bool periodic_started;

static void sensor_measure_cb(zb_bufid_t bufid)
{
	int16_t  temp_zcl = ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_UNKNOWN;
	uint16_t hum_zcl  = ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_UNKNOWN;
	int rc;

	ZVUNUSED(bufid);

	rc = hdc2080_get_temp_humidity_x100(&temp_zcl, &hum_zcl);
	if (rc == 0) {
		zigbee_sensor_update(temp_zcl, hum_zcl);
	} else {
		LOG_WRN("Sensor read failed (%d); keeping last valid values", rc);
	}

	uint16_t batt_mv = 0;
	rc = vsense_measure_mv(&batt_mv);
	if (rc == 0) {
		zb_uint8_t batt_100mv = (zb_uint8_t)(batt_mv / 100U);
		zb_uint8_t batt_pct   = batt_mv_to_zcl_pct(batt_mv);

		ZB_ZCL_SET_ATTRIBUTE(
			SENSOR_ENDPOINT,
			ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
			ZB_ZCL_CLUSTER_SERVER_ROLE,
			ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID,
			&batt_100mv,
			ZB_FALSE);

		ZB_ZCL_SET_ATTRIBUTE(
			SENSOR_ENDPOINT,
			ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
			ZB_ZCL_CLUSTER_SERVER_ROLE,
			ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID,
			&batt_pct,
			ZB_FALSE);

		LOG_INF("Battery: %u mV → %u %% (ZCL %u)", (unsigned)batt_mv,
			(unsigned)(batt_pct / 2), (unsigned)batt_pct);
	} else {
		LOG_WRN("vsense_measure_mv failed (%d)", rc);
	}

	ZB_SCHEDULE_APP_ALARM(sensor_measure_cb, 0,
		ZB_MILLISECONDS_TO_BEACON_INTERVAL(SENSOR_SAMPLING_INTERVAL_MS));
}

/* ── Reporting configuration ───────────────────────────────────────────── */

static void sensor_configure_reporting(void)
{
	zb_zcl_reporting_info_t rep;

	/* Temperature Measurement – MeasuredValue */
	memset(&rep, 0, sizeof(rep));
	rep.direction      = ZB_ZCL_CONFIGURE_REPORTING_SEND_REPORT;
	rep.ep             = SENSOR_ENDPOINT;
	rep.cluster_id     = ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT;
	rep.cluster_role   = ZB_ZCL_CLUSTER_SERVER_ROLE;
	rep.attr_id        = ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID;
	rep.manuf_code     = ZB_ZCL_NON_MANUFACTURER_SPECIFIC;
	rep.dst.short_addr = 0x0000;            /* coordinator */
	rep.dst.endpoint   = 1;
	rep.dst.profile_id = ZB_AF_HA_PROFILE_ID;
	rep.u.send_info.min_interval     = 30;  /* send no more often than 30 s */
	rep.u.send_info.max_interval     = 300; /* force a report every 5 min   */
	rep.u.send_info.delta.s16        = 50;  /* or when change >= 0.50 °C   */
	rep.u.send_info.def_min_interval = 30;
	rep.u.send_info.def_max_interval = 300;
	ZB_ERROR_CHECK(zb_zcl_put_reporting_info(&rep, ZB_TRUE));

	/* Relative Humidity Measurement – MeasuredValue */
	memset(&rep, 0, sizeof(rep));
	rep.direction      = ZB_ZCL_CONFIGURE_REPORTING_SEND_REPORT;
	rep.ep             = SENSOR_ENDPOINT;
	rep.cluster_id     = ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT;
	rep.cluster_role   = ZB_ZCL_CLUSTER_SERVER_ROLE;
	rep.attr_id        = ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_ID;
	rep.manuf_code     = ZB_ZCL_NON_MANUFACTURER_SPECIFIC;
	rep.dst.short_addr = 0x0000;
	rep.dst.endpoint   = 1;
	rep.dst.profile_id = ZB_AF_HA_PROFILE_ID;
	rep.u.send_info.min_interval     = 30;
	rep.u.send_info.max_interval     = 300;
	rep.u.send_info.delta.u16        = 100; /* or when change >= 1.00 %    */
	rep.u.send_info.def_min_interval = 30;
	rep.u.send_info.def_max_interval = 300;
	ZB_ERROR_CHECK(zb_zcl_put_reporting_info(&rep, ZB_TRUE));

	/* Power Configuration – BatteryVoltage */
	memset(&rep, 0, sizeof(rep));
	rep.direction      = ZB_ZCL_CONFIGURE_REPORTING_SEND_REPORT;
	rep.ep             = SENSOR_ENDPOINT;
	rep.cluster_id     = ZB_ZCL_CLUSTER_ID_POWER_CONFIG;
	rep.cluster_role   = ZB_ZCL_CLUSTER_SERVER_ROLE;
	rep.attr_id        = ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID;
	rep.manuf_code     = ZB_ZCL_NON_MANUFACTURER_SPECIFIC;
	rep.dst.short_addr = 0x0000;
	rep.dst.endpoint   = 1;
	rep.dst.profile_id = ZB_AF_HA_PROFILE_ID;
	rep.u.send_info.min_interval     = 60;   /* no more often than 60 s   */
	rep.u.send_info.max_interval     = 3600; /* force report every hour   */
	rep.u.send_info.delta.u8         = 1;    /* or when change >= 100 mV  */
	rep.u.send_info.def_min_interval = 60;
	rep.u.send_info.def_max_interval = 3600;
	ZB_ERROR_CHECK(zb_zcl_put_reporting_info(&rep, ZB_TRUE));

	/* Power Configuration – BatteryPercentageRemaining */
	memset(&rep, 0, sizeof(rep));
	rep.direction      = ZB_ZCL_CONFIGURE_REPORTING_SEND_REPORT;
	rep.ep             = SENSOR_ENDPOINT;
	rep.cluster_id     = ZB_ZCL_CLUSTER_ID_POWER_CONFIG;
	rep.cluster_role   = ZB_ZCL_CLUSTER_SERVER_ROLE;
	rep.attr_id        = ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID;
	rep.manuf_code     = ZB_ZCL_NON_MANUFACTURER_SPECIFIC;
	rep.dst.short_addr = 0x0000;
	rep.dst.endpoint   = 1;
	rep.dst.profile_id = ZB_AF_HA_PROFILE_ID;
	rep.u.send_info.min_interval     = 60;
	rep.u.send_info.max_interval     = 3600;
	rep.u.send_info.delta.u8         = 2;    /* or when change >= 1 %     */
	rep.u.send_info.def_min_interval = 60;
	rep.u.send_info.def_max_interval = 3600;
	ZB_ERROR_CHECK(zb_zcl_put_reporting_info(&rep, ZB_TRUE));

	LOG_INF("Default reporting configured");
}

/* ── Public API ────────────────────────────────────────────────────────── */

void zigbee_sensor_init(void)
{
	ZB_AF_REGISTER_DEVICE_CTX(&sensor_device_ctx);

	dev_ctx.basic_attr.zcl_version  = ZB_ZCL_VERSION;
	dev_ctx.basic_attr.power_source = ZB_ZCL_BASIC_POWER_SOURCE_BATTERY;

	ZB_ZCL_SET_STRING_VAL(
        dev_ctx.basic_attr.manufacturer_name,
        bee_manufacturer_name,
        strlen(bee_manufacturer_name)
    );

    ZB_ZCL_SET_STRING_VAL(
        dev_ctx.basic_attr.model_id,
        bee_model_id,
        strlen(bee_model_id)
    );

	dev_ctx.identify_attr.identify_time = ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE;

	dev_ctx.temp_attr.measure_value     = ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_UNKNOWN;
	dev_ctx.temp_attr.min_measure_value = (-4000);  /* -40.00 °C */
	dev_ctx.temp_attr.max_measure_value = 8500;     /* +85.00 °C */
	dev_ctx.temp_attr.tolerance         = 50;       /* ±0.50 °C  */

	dev_ctx.hum_value = ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_UNKNOWN;
	dev_ctx.hum_min   = 0;      /* 0.00 % */
	dev_ctx.hum_max   = 10000;  /* 100.00 % */

	dev_ctx.batt_voltage_100mv         = ZB_ZCL_POWER_CONFIG_BATTERY_VOLTAGE_INVALID;
	dev_ctx.batt_percentage    = 0xFF; /* unknown until first read */
	
	LOG_INF("Zigbee sensor endpoint %d registered", SENSOR_ENDPOINT);
}

void zigbee_sensor_update(int16_t temp_zcl, uint16_t hum_zcl)
{
	ZB_ZCL_SET_ATTRIBUTE(
		SENSOR_ENDPOINT,
		ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID,
		(zb_uint8_t *)&temp_zcl,
		ZB_FALSE);

	ZB_ZCL_SET_ATTRIBUTE(
		SENSOR_ENDPOINT,
		ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT,
		ZB_ZCL_CLUSTER_SERVER_ROLE,
		ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_ID,
		(zb_uint8_t *)&hum_zcl,
		ZB_FALSE);

	LOG_DBG("Attributes updated: temp=%d hum=%u", (int)temp_zcl, (unsigned)hum_zcl);
}

void zigbee_sensor_start_periodic(void)
{
	if (periodic_started) {
		return;
	}
	periodic_started = true;
	/* Configure reporting now – ZBOSS reporting context is only valid
	 * after the stack scheduler has started (i.e. after zigbee_enable()). */
	sensor_configure_reporting();
	ZB_SCHEDULE_APP_CALLBACK(sensor_measure_cb, 0);
	LOG_INF("Periodic sampling started (%u ms interval)",
		SENSOR_SAMPLING_INTERVAL_MS);
}

int zigbee_sensor_start(void) {
	int rc = 0;

	/* Register Zigbee endpoint and initialise cluster attributes. */
	zigbee_sensor_init();

	/* Enable sleepy end-device behaviour to reduce radio-on time. */
	zigbee_configure_sleepy_behavior(true);
	
	// /* the radio must be disabled between the polls to parent */
	zb_set_rx_on_when_idle(false);

	/* Load persistent Zigbee configuration (network keys, address …). */
	rc = settings_subsys_init();
	if (rc) {
		LOG_ERR("settings init failed (%d)", rc);
	}

	rc = settings_load();
	if (rc) {
		LOG_ERR("settings load failed (%d)", rc);
	}

	/* Start the ZBOSS thread.  All further work is driven by the
	 * ZBOSS scheduler; main() returns after this call. */
	zigbee_enable();

	LOG_INF("Zigbee stack started, waiting for network");

	return 0;
}

/**@brief Zigbee stack event handler. */
void zboss_signal_handler(zb_bufid_t bufid)
{
	zb_zdo_app_signal_hdr_t   *sig_hdr = NULL;
	zb_zdo_app_signal_type_t   sig     = zb_get_app_signal(bufid, &sig_hdr);
	zb_ret_t                   status  = ZB_GET_APP_SIGNAL_STATUS(bufid);

	switch (sig) {
	case ZB_BDB_SIGNAL_DEVICE_FIRST_START:
	case ZB_BDB_SIGNAL_DEVICE_REBOOT:
		if (status == RET_OK) {
			/* Already in network from NVRAM – start sampling. */
			zigbee_sensor_start_periodic();
			zb_zdo_pim_set_long_poll_interval(30000U);
		}
		break;

	case ZB_BDB_SIGNAL_STEERING:
		if (status == RET_OK) {
			/* Successfully joined a network – start sampling. */
			zigbee_sensor_start_periodic();
			zb_zdo_pim_set_long_poll_interval(30000U);
		}
		break;

	default:
		break;
	}

	ZB_ERROR_CHECK(zigbee_default_signal_handler(bufid));

	if (bufid) {
		zb_buf_free(bufid);
	}
}