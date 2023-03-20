/*
 * Copyright (c) 2016 Linaro Limited.
 *               2016 Intel Corporation.
 * 				 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/types.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/drivers/gpio.h>
#include <nrfx_gpiote.h>
#include <sensor/hx711/hx711.h>
#include <stddef.h>

#ifdef CONFIG_TRUSTED_EXECUTION_NONSECURE
#define TEST_PARTITION	slot1_ns_partition
#else
#define TEST_PARTITION	slot1_partition
#endif

#define TEST_PARTITION_OFFSET	FIXED_PARTITION_OFFSET(TEST_PARTITION)
#define TEST_PARTITION_DEVICE	FIXED_PARTITION_DEVICE(TEST_PARTITION)
#define FLASH_PAGE_SIZE         4096
#define CALIBRATION_VALUE_ADDR  0x40000 // Address where the calibration value will be stored in flash memory
#define OFFSET_VALUE_ADDR       0x50000 // Address where the offset value will be stored in flash memory
#define LED1 2
#define LED2 3
#define LED3 4
#define LED4 5
#define LED5 28
#define BUTTON1 6
#define BUTTON2 7
#define SWITCH1 8
#define SWITCH2 9

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

const struct device *hx711_dev;
struct sensor_value slope, slope2;
const struct device *flash_dev = TEST_PARTITION_DEVICE;
static struct sensor_value weight, weight2;
int offset, offset2; //offset stores return value of avia_hx711_tare()

void measure(void) {
	int ret = sensor_sample_fetch(hx711_dev);
	if(ret != 0) {
		LOG_ERR("Cannot take measurement: %d", ret);
	} else {
		sensor_channel_get(hx711_dev, HX711_SENSOR_CHAN_WEIGHT, &weight);
		LOG_INF("Weight: %d.%06d grams", weight.val1, weight.val2);
	}
}

void measureWithFlash(void) {
    int ret = sensor_sample_fetch(hx711_dev);
    if (ret != 0) {
		LOG_ERR("Cannot take measurement: %d", ret);
	} else {
        sensor_channel_get(hx711_dev, HX711_SENSOR_CHAN_WEIGHT, &weight2);
        weight2.val1 = (sensor_value_to_double(&slope2.val1) * (weight2.val1 - offset2));
		LOG_INF("Weight: %d.%06d grams", weight2.val1, weight2.val2);
    }
}

void readWriteCalibration(void) {
    LOG_INF("Do you want to calibrate load sensor?");
    LOG_INF("To Make a Choice Push Switch to ON Before Count Down Ends");
    LOG_INF("SWITCH 1: Yes");
    LOG_INF("SWITCH 2: No");
    LOG_INF("Waiting 5 seconds for user response...");

    nrf_gpio_cfg_output(LED3);
    nrf_gpio_cfg_output(LED4);
    nrf_gpio_cfg_input(SWITCH1, NRF_GPIO_PIN_PULLUP);
    nrf_gpio_cfg_input(SWITCH2, NRF_GPIO_PIN_PULLUP);

    for(int i = 5; i >= 0; i--) {
        LOG_INF(" %d..", i);
        k_msleep(1000);
    }
}

void deviceDisable(void) {
	// Disable buttons
	nrfx_gpiote_pin_uninit(SWITCH1);
	nrfx_gpiote_pin_uninit(SWITCH2);
    nrfx_gpiote_pin_uninit(BUTTON1);
	nrfx_gpiote_pin_uninit(BUTTON2);

	// Disable pins
	nrf_gpio_pin_clear(LED3);
	nrf_gpio_pin_clear(LED4);
    nrf_gpio_pin_clear(LED5);
	nrfx_gpiote_pin_uninit(LED3);
	nrfx_gpiote_pin_uninit(LED4);
    nrfx_gpiote_pin_uninit(LED5);

	// Disable sensor
	nrfx_gpiote_pin_uninit(2);
	nrfx_gpiote_pin_uninit(3);
}

void deviceEnable(void) {
	// Enable buttons
	nrf_gpio_cfg_input(SWITCH1, NRF_GPIO_PIN_PULLUP);
	nrf_gpio_cfg_input(SWITCH2, NRF_GPIO_PIN_PULLUP);
    nrf_gpio_cfg_input(BUTTON1, NRF_GPIO_PIN_PULLUP);
	nrf_gpio_cfg_input(BUTTON2, NRF_GPIO_PIN_PULLUP);
    
	// Enable pins
	nrf_gpio_cfg_output(LED3);
	nrf_gpio_cfg_output(LED4);
    nrf_gpio_cfg_output(LED5);
	nrf_gpio_pin_set(LED3);
	nrf_gpio_pin_set(LED4);
    nrf_gpio_pin_set(LED5);

	// Enable sensor
	nrf_gpio_cfg_input(2, (GPIO_ACTIVE_HIGH | GPIO_PULL_UP));
	nrf_gpio_cfg_input(3, GPIO_ACTIVE_HIGH);
}

void main(void) {
    int rc; // rc return value for flash functions
	int calibration_weight = 1000; // weight in grams
    hx711_dev = DEVICE_DT_GET_ANY(avia_hx711);
    __ASSERT(hx711_dev == NULL, "Failed to get device binding");
    LOG_INF("Device is %p, name is %s", hx711_dev, hx711_dev->name);

    readWriteCalibration();
    
    if(nrf_gpio_pin_read(SWITCH1)==0) {
        nrf_gpio_pin_set(LED3);
        LOG_INF("Switch 1 Pushed: Option 1 Choosen");

        LOG_INF("Calculating offset...");
        for(int i = 5; i >= 0; i--) {
            LOG_INF(" %d..", i);
            k_msleep(1000);
        }

        avia_hx711_tare(hx711_dev, 5);
        offset = avia_hx711_tare(hx711_dev, 5);
        LOG_INF("Offset is: %d", offset);

        // Erase Flash for offset value address
        rc = flash_erase(flash_dev, OFFSET_VALUE_ADDR, FLASH_PAGE_SIZE);
        if (rc) {
            LOG_ERR("Failed to erase flash memory: %d", rc);
            return;
        }

        // Writing offset to Flash memory
        rc = flash_write(flash_dev, OFFSET_VALUE_ADDR, &offset, sizeof(offset));
        if (rc) {
            LOG_ERR("Failed to write to flash memory: %d", rc);
            return;
        }

        LOG_INF("Waiting for known weight of %d grams...", calibration_weight);
        for(int i = 5; i >= 0; i--) {
            LOG_INF(" %d..", i);
            k_msleep(1000);
        }

        LOG_INF("Calculating slope...");
        avia_hx711_calibrate(hx711_dev, calibration_weight, 5);
        slope = avia_hx711_calibrate(hx711_dev, calibration_weight, 5);
        LOG_INF("Slope is: %d.%06d", slope.val1, slope.val2);

        // Erase Flash for calibration value address
        rc = flash_erase(flash_dev, CALIBRATION_VALUE_ADDR, FLASH_PAGE_SIZE);
        if (rc) {
            LOG_ERR("Failed to erase flash memory: %d", rc);
            return;
        }

        // Writing slope to Flash memory
        rc = flash_write(flash_dev, CALIBRATION_VALUE_ADDR, &slope, sizeof(slope));
        if (rc) {
            LOG_ERR("Failed to write to flash memory: %d", rc);
            return;
        }

        LOG_INF("Slope and offset successfully saved to flash memory");
        LOG_INF("Offset saved to flash memory is: %d", offset);
        LOG_INF("Slope saved to flash memory is: %d.%06d", slope.val1, slope.val2);
        nrf_gpio_pin_clear(LED3);

        while(true) {
            deviceEnable();
            measure();
            deviceDisable();
            k_sleep(K_HOURS(6));
        }
    }

    else if(nrf_gpio_pin_read(SWITCH2)==0) {
        nrf_gpio_pin_set(LED4);
        LOG_INF("Switch 2 Pushed: Option 2 Choosen");

        // Read offset from memory
        rc = flash_read(flash_dev, OFFSET_VALUE_ADDR, &offset2, sizeof(offset2));
        if (rc) {
            LOG_ERR("Failed to read from flash memory: %d", rc);
            return;
        }

        // Read control offset from memory
        int stored_offset; 
        rc = flash_read(flash_dev, OFFSET_VALUE_ADDR, &stored_offset, sizeof(stored_offset));
        if (rc) {
            LOG_ERR("Failed to read from flash memory: %d", rc);
            return;
        }

        // Compare stored and calculated offset
        if (offset2 == stored_offset) {
            LOG_INF("Offset matches the value stored in memory");
        } else {
            LOG_INF("Offset does not match the value stored in memory");
        }

        // Read slope from memory
        int rc = flash_read(flash_dev, CALIBRATION_VALUE_ADDR, &slope2, sizeof(slope2));
        if (rc) {
            LOG_ERR("Failed to read from flash memory: %d", rc);
            return;
        }

        // Read control slope from memory
        struct sensor_value stored_slope; 
        rc = flash_read(flash_dev, CALIBRATION_VALUE_ADDR, &stored_slope, sizeof(stored_slope));
        if (rc) {
            LOG_ERR("Failed to read from flash memory: %d", rc);
            return;
        }

        // Compare stored and calculated slopes
        if (stored_slope.val1 == slope2.val1 && stored_slope.val2 == slope2.val2) {
            LOG_INF("Slope matches the value stored in memory");
        } else {
            LOG_INF("Slope does not match the value stored in memory");
        }

        LOG_INF("Slope and offset successfully read from flash memeory");
        LOG_INF("Offset read from flash memory is: %d", offset2);
        LOG_INF("Slope read from flash memory is: %d.%06d", slope2.val1, slope2.val2);
        nrf_gpio_pin_clear(LED4);

        while(true) {
            deviceEnable();
            measureWithFlash();
            deviceDisable();
		    k_sleep(K_HOURS(6));
        }
    }

    else if((nrf_gpio_pin_read(SWITCH1)==0) && (nrf_gpio_pin_read(SWITCH2)==0)) {
        nrf_gpio_pin_set(LED3);
        nrf_gpio_pin_set(LED4);
        LOG_INF("Error: Push Just One Switch, Auto-Choosing Option 2");

        // Read offset from memory
        rc = flash_read(flash_dev, OFFSET_VALUE_ADDR, &offset2, sizeof(offset2));
        if (rc) {
            LOG_ERR("Failed to read from flash memory: %d", rc);
            return;
        }

        // Read control offset from memory
        int stored_offset; 
        rc = flash_read(flash_dev, OFFSET_VALUE_ADDR, &stored_offset, sizeof(stored_offset));
        if (rc) {
            LOG_ERR("Failed to read from flash memory: %d", rc);
            return;
        }

        // Compare stored and calculated offset
        if (offset2 == stored_offset) {
            LOG_INF("Offset matches the value stored in memory");
        } else {
            LOG_INF("Offset does not match the value stored in memory");
        }

        // Read slope from memory
        int rc = flash_read(flash_dev, CALIBRATION_VALUE_ADDR, &slope2, sizeof(slope2));
        if (rc) {
            LOG_ERR("Failed to read from flash memory: %d", rc);
            return;
        }

        // Read control slope from memory
        struct sensor_value stored_slope; 
        rc = flash_read(flash_dev, CALIBRATION_VALUE_ADDR, &stored_slope, sizeof(stored_slope));
        if (rc) {
            LOG_ERR("Failed to read from flash memory: %d", rc);
            return;
        }

        // Compare stored and calculated slopes
        if (stored_slope.val1 == slope2.val1 && stored_slope.val2 == slope2.val2) {
            LOG_INF("Slope matches the value stored in memory");
        } else {
            LOG_INF("Slope does not match the value stored in memory");
        }

        LOG_INF("Slope and offset successfully read from flash memeory");
        LOG_INF("Offset read from flash memory is: %d", offset2);
        LOG_INF("Slope read from flash memory is: %d.%06d", slope2.val1, slope2.val2);
        nrf_gpio_pin_clear(LED3);
        nrf_gpio_pin_clear(LED4);

        while(true) {
            deviceEnable();
            measureWithFlash();
            deviceDisable();
		    k_sleep(K_HOURS(6));
        }
    }

    else if((nrf_gpio_pin_read(SWITCH1)!=0) && (nrf_gpio_pin_read(SWITCH2)!=0)) {
        LOG_ERR("Error: Timeout No Button Pressed, Auto-Choosing Option 2");

        // Read offset from memory
        rc = flash_read(flash_dev, OFFSET_VALUE_ADDR, &offset2, sizeof(offset2));
        if (rc) {
            LOG_ERR("Failed to read from flash memory: %d", rc);
            return;
        }

        // Read control offset from memory
        int stored_offset; 
        rc = flash_read(flash_dev, OFFSET_VALUE_ADDR, &stored_offset, sizeof(stored_offset));
        if (rc) {
            LOG_ERR("Failed to read from flash memory: %d", rc);
            return;
        }

        // Compare stored and calculated offset
        if (offset2 == stored_offset) {
            LOG_INF("Offset matches the value stored in memory");
        } else {
            LOG_INF("Offset does not match the value stored in memory");
        }

        // Read slope from memory
        int rc = flash_read(flash_dev, CALIBRATION_VALUE_ADDR, &slope2, sizeof(slope2));
        if (rc) {
            LOG_ERR("Failed to read from flash memory: %d", rc);
            return;
        }

        // Read control slope from memory
        struct sensor_value stored_slope; 
        rc = flash_read(flash_dev, CALIBRATION_VALUE_ADDR, &stored_slope, sizeof(stored_slope));
        if (rc) {
            LOG_ERR("Failed to read from flash memory: %d", rc);
            return;
        }

        // Compare stored and calculated slopes
        if (stored_slope.val1 == slope2.val1 && stored_slope.val2 == slope2.val2) {
            LOG_INF("Slope matches the value stored in memory");
        } else {
            LOG_INF("Slope does not match the value stored in memory");
        }

        LOG_INF("Slope and offset successfully read from flash memeory");
        LOG_INF("Offset read from flash memory is: %d", offset2);
        LOG_INF("Slope read from flash memory is: %d.%06d", slope2.val1, slope2.val2);

        while(true) {
            deviceEnable();
            measureWithFlash();
            deviceDisable();
		    k_sleep(K_HOURS(6));
        }
    }
    
    else {
        LOG_ERR("Error: Timeout No Button Pressed, Auto-Choosing Option 2");

        // Read offset from memory
        rc = flash_read(flash_dev, OFFSET_VALUE_ADDR, &offset2, sizeof(offset2));
        if (rc) {
            LOG_ERR("Failed to read from flash memory: %d", rc);
            return;
        }

        // Read control offset from memory
        int stored_offset; 
        rc = flash_read(flash_dev, OFFSET_VALUE_ADDR, &stored_offset, sizeof(stored_offset));
        if (rc) {
            LOG_ERR("Failed to read from flash memory: %d", rc);
            return;
        }

        // Compare stored and calculated offset
        if (offset2 == stored_offset) {
            LOG_INF("Offset matches the value stored in memory");
        } else {
            LOG_INF("Offset does not match the value stored in memory");
        }

        // Read slope from memory
        int rc = flash_read(flash_dev, CALIBRATION_VALUE_ADDR, &slope2, sizeof(slope2));
        if (rc) {
            LOG_ERR("Failed to read from flash memory: %d", rc);
            return;
        }

        // Read control slope from memory
        struct sensor_value stored_slope; 
        rc = flash_read(flash_dev, CALIBRATION_VALUE_ADDR, &stored_slope, sizeof(stored_slope));
        if (rc) {
            LOG_ERR("Failed to read from flash memory: %d", rc);
            return;
        }

        // Compare stored and calculated slopes
        if (stored_slope.val1 == slope2.val1 && stored_slope.val2 == slope2.val2) {
            LOG_INF("Slope matches the value stored in memory");
        } else {
            LOG_INF("Slope does not match the value stored in memory");
        }

        LOG_INF("Slope and offset successfully read from flash memeory");
        LOG_INF("Offset read from flash memory is: %d", offset2);
        LOG_INF("Slope read from flash memory is: %d.%06d", slope2.val1, slope2.val2);

        while(true) {
            deviceEnable();
            measureWithFlash();
            deviceDisable();
		    k_sleep(K_HOURS(6));
        }
    }
}