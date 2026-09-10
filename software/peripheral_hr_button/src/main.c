/* main.c - Application main entry point */

/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/services/hrs.h>

/* The devicetree node identifiers for the LEDs and button */
#define LED_BUTTON_NODE DT_ALIAS(led0)
#define LED_BLINK_NODE DT_ALIAS(led1)
#define LED_REMOTE_NODE DT_ALIAS(led2)

#define BUTTON_NODE DT_ALIAS(sw0)

static const struct gpio_dt_spec led_button = GPIO_DT_SPEC_GET(LED_BUTTON_NODE, gpios);
static const struct gpio_dt_spec led_blink = GPIO_DT_SPEC_GET(LED_BLINK_NODE, gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(BUTTON_NODE, gpios);

static const struct gpio_dt_spec led_remote = GPIO_DT_SPEC_GET(LED_REMOTE_NODE, gpios);


/* UUID du service et de la characteristic */
static struct bt_uuid_16 remote_led_service_uuid = BT_UUID_INIT_16(0x1600);
static struct bt_uuid_16 remote_led_char_uuid    = BT_UUID_INIT_16(0x1601);



static struct gpio_callback button_cb_data;

/* Variables for blinking */
static struct k_work_delayable blink_work;
static bool led_blink_is_on = false;


/* CallBack Receiver*/
static ssize_t write_remote_led(struct bt_conn *conn,
				const struct bt_gatt_attr *attr,
				const void *buf, uint16_t len,
				uint16_t offset, uint8_t flags)
{
    if (len < 1) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    uint8_t value = ((uint8_t *)buf)[0];  // 0x00 ou 0x01

    gpio_pin_set(led_remote.port, led_remote.pin, value ? 1 : 0);

    printk("LED2 commandée via BLE : %d\n", value);

    return len;
}

BT_GATT_SERVICE_DEFINE(remote_led_svc,
	BT_GATT_PRIMARY_SERVICE(&remote_led_service_uuid),
	BT_GATT_CHARACTERISTIC(&remote_led_char_uuid.uuid,
			       BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_WRITE,
			       NULL, write_remote_led, NULL),
);



/* Callback function for button press */
static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    /* Toggle the button-controlled LED */
    gpio_pin_toggle(led_button.port, led_button.pin);
    printk("Bouton pressé : LED contrôlée par le bouton changée d'état\n");
}

/* Function for blinking LED */
static void blink_timeout(struct k_work *work)
{
    led_blink_is_on = !led_blink_is_on;
    gpio_pin_set(led_blink.port, led_blink.pin, (int)led_blink_is_on);

    k_work_schedule(&blink_work, K_MSEC(500)); /* Clignote toutes les 500 ms */
}



static bool hrf_ntf_enabled;

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL,
		      BT_UUID_16_ENCODE(BT_UUID_HRS_VAL),
		      BT_UUID_16_ENCODE(BT_UUID_BAS_VAL),
		      BT_UUID_16_ENCODE(BT_UUID_DIS_VAL)),
#if defined(CONFIG_BT_EXT_ADV)
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
#endif /* CONFIG_BT_EXT_ADV */
};

#if !defined(CONFIG_BT_EXT_ADV)
static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};
#endif /* !CONFIG_BT_EXT_ADV */

/* Use atomic variable, 2 bits for connection and disconnection state */
static ATOMIC_DEFINE(state, 2U);

#define STATE_CONNECTED    1U
#define STATE_DISCONNECTED 2U

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("Connection failed, err 0x%02x %s\n", err, bt_hci_err_to_str(err));
	} else {
		printk("Connected\n");

		(void)atomic_set_bit(state, STATE_CONNECTED);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("Disconnected, reason 0x%02x %s\n", reason, bt_hci_err_to_str(reason));

	(void)atomic_set_bit(state, STATE_DISCONNECTED);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

static void hrs_ntf_changed(bool enabled)
{
	hrf_ntf_enabled = enabled;

	printk("HRS notification status changed: %s\n",
	       enabled ? "enabled" : "disabled");
}

static struct bt_hrs_cb hrs_cb = {
	.ntf_changed = hrs_ntf_changed,
};

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing cancelled: %s\n", addr);
}

static struct bt_conn_auth_cb auth_cb_display = {
	.cancel = auth_cancel,
};

static void bas_notify(void)
{
	uint8_t battery_level = bt_bas_get_battery_level();

	battery_level--;

	if (!battery_level) {
		battery_level = 100U;
	}

	bt_bas_set_battery_level(battery_level);
}

static void hrs_notify(void)
{
	static uint8_t heartrate = 90U;

	/* Heartrate measurements simulation */
	heartrate++;
	if (heartrate == 160U) {
		heartrate = 90U;
	}

	if (hrf_ntf_enabled) {
		bt_hrs_notify(heartrate);
	}
}

void main(void)
{
    int ret;

    printk("Initialisation des GPIOs...\n");

    /* Configure the button-controlled LED */
    if (!gpio_is_ready_dt(&led_button)) {
        printk("Erreur : le périphérique LED_BUTTON n'est pas prêt\n");
        return;
    }
    ret = gpio_pin_configure_dt(&led_button, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        printk("Erreur lors de la configuration de LED_BUTTON (code %d)\n", ret);
        return;
    }

    /* Configure the blinking LED */
    if (!gpio_is_ready_dt(&led_blink)) {
        printk("Erreur : le périphérique LED_BLINK n'est pas prêt\n");
        return;
    }
    ret = gpio_pin_configure_dt(&led_blink, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        printk("Erreur lors de la configuration de LED_BLINK (code %d)\n", ret);
        return;
    }

    /* Configure the Button GPIO */
    if (!gpio_is_ready_dt(&button)) {
        printk("Erreur : le périphérique BUTTON n'est pas prêt\n");
        return;
    }
    ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
    if (ret < 0) {
        printk("Erreur lors de la configuration du bouton (code %d)\n", ret);
        return;
    }

    ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret < 0) {
        printk("Erreur lors de la configuration de l'interruption du bouton (code %d)\n", ret);
        return;
    }

	if (!gpio_is_ready_dt(&led_remote)) {
		printk("LED2 non prête\n");
		return;
	}

	int ret_led_remote = gpio_pin_configure_dt(&led_remote, GPIO_OUTPUT_INACTIVE);
	if (ret_led_remote < 0) {
		printk("Erreur configuration LED2 (%d)\n", ret_led_remote);
		return;
	}



    gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb_data);

    printk("Configuration terminée. Appuyez sur le bouton pour contrôler LED_BUTTON. LED_BLINK clignote automatiquement.\n");

    /* Start blinking LED */
    k_work_init_delayable(&blink_work, blink_timeout);
    k_work_schedule(&blink_work, K_MSEC(500)); /* Démarre le clignotement */

    int err;

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	printk("Bluetooth initialized\n");

	bt_conn_auth_cb_register(&auth_cb_display);

	bt_hrs_cb_register(&hrs_cb);

#if !defined(CONFIG_BT_EXT_ADV)
	printk("Starting Legacy Advertising (connectable and scannable)\n");
	err = bt_le_adv_start(BT_LE_ADV_CONN_ONE_TIME, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return;
	}

#else /* CONFIG_BT_EXT_ADV */
	struct bt_le_adv_param adv_param = {
		.id = BT_ID_DEFAULT,
		.sid = 0U,
		.secondary_max_skip = 0U,
		.options = (BT_LE_ADV_OPT_EXT_ADV |
			    BT_LE_ADV_OPT_CONNECTABLE |
			    BT_LE_ADV_OPT_CODED),
		.interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
		.interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
		.peer = NULL,
	};
	struct bt_le_ext_adv *adv;

	printk("Creating a Coded PHY connectable non-scannable advertising set\n");
	err = bt_le_ext_adv_create(&adv_param, NULL, &adv);
	if (err) {
		printk("Failed to create Coded PHY extended advertising set (err %d)\n", err);

		printk("Creating a non-Coded PHY connectable non-scannable advertising set\n");
		adv_param.options &= ~BT_LE_ADV_OPT_CODED;
		err = bt_le_ext_adv_create(&adv_param, NULL, &adv);
		if (err) {
			printk("Failed to create extended advertising set (err %d)\n", err);
			return;
		}
	}

	printk("Setting extended advertising data\n");
	err = bt_le_ext_adv_set_data(adv, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		printk("Failed to set extended advertising data (err %d)\n", err);
		return;
	}

	printk("Starting Extended Advertising (connectable non-scannable)\n");
	err = bt_le_ext_adv_start(adv, BT_LE_EXT_ADV_START_DEFAULT);
	if (err) {
		printk("Failed to start extended advertising set (err %d)\n", err);
		return;
	}
#endif /* CONFIG_BT_EXT_ADV */

	printk("Advertising successfully started\n");

	/* Implement notification. */
	while (1) {
		k_sleep(K_SECONDS(1));

		/* Heartrate measurements simulation */
		hrs_notify();

		/* Battery level simulation */
		bas_notify();

		if (atomic_test_and_clear_bit(state, STATE_CONNECTED)) {
			/* Connected callback executed */
		} else if (atomic_test_and_clear_bit(state, STATE_DISCONNECTED)) {
#if !defined(CONFIG_BT_EXT_ADV)
			printk("Starting Legacy Advertising (connectable and scannable)\n");
			err = bt_le_adv_start(BT_LE_ADV_CONN_ONE_TIME, ad, ARRAY_SIZE(ad), sd,
					      ARRAY_SIZE(sd));
			if (err) {
				printk("Advertising failed to start (err %d)\n", err);
				return;
			}

#else /* CONFIG_BT_EXT_ADV */
			printk("Starting Extended Advertising (connectable and non-scannable)\n");
			err = bt_le_ext_adv_start(adv, BT_LE_EXT_ADV_START_DEFAULT);
			if (err) {
				printk("Failed to start extended advertising set (err %d)\n", err);
				return;
			}
#endif /* CONFIG_BT_EXT_ADV */
		}
	}
}


