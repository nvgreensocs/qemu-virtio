/*
 * VirtioBus
 *
 *  Copyright (C) 2012 : GreenSocs Ltd
 *      http://www.greensocs.com/ , email: info@greensocs.com
 *
 *  Developed by :
 *  Frederic Konrad   <fred.konrad@greensocs.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "hw.h"
#include "qemu/error-report.h"
#include "qdev.h"
#include "virtio-bus.h"
#include "virtio.h"

/* #define DEBUG_VIRTIO_BUS */

#ifdef DEBUG_VIRTIO_BUS
#define DPRINTF(fmt, ...) \
do { printf("virtio_bus: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do { } while (0)
#endif

/* Plug the VirtIODevice */
int virtio_bus_plug_device(VirtIODevice *vdev)
{
    DeviceState *qdev = DEVICE(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(qdev));
    VirtioBusState *bus = VIRTIO_BUS(qbus);
    VirtioBusClass *klass = VIRTIO_BUS_GET_CLASS(bus);
    DPRINTF("%s : plug device.\n", qbus->name);

    bus->vdev = vdev;

    if (klass->device_plugged != NULL) {
        klass->device_plugged(qbus->parent);
    }

    return 0;
}

/* Reset the virtio_bus */
void virtio_bus_reset(VirtioBusState *bus)
{
    DPRINTF("%s : reset device.\n", qbus->name);
    if (bus->vdev != NULL) {
        virtio_reset(bus->vdev);
    }
}

/* Destroy the VirtIODevice */
void virtio_bus_destroy_device(VirtioBusState *bus)
{
    DeviceState *qdev;
    BusState *qbus = BUS(bus);
    VirtioBusClass *klass = VIRTIO_BUS_GET_CLASS(bus);
    DPRINTF("%s : remove device.\n", qbus->name);

    if (bus->vdev != NULL) {
        if (klass->device_unplug != NULL) {
            klass->device_unplug(qbus->parent);
        }
        qdev = DEVICE(bus->vdev);
        qdev_free(qdev);
        bus->vdev = NULL;
    }
}

/* Get the device id of the plugged device. */
uint16_t get_virtio_device_id(VirtioBusState *bus)
{
    assert(bus->vdev != NULL);
    return bus->vdev->device_id;
}

/* Get the nvectors field of the plugged device. */
int get_virtio_device_nvectors(VirtioBusState *bus)
{
    assert(bus->vdev != NULL);
    return bus->vdev->nvectors;
}

/* Set the nvectors field of the plugged device. */
void set_virtio_device_nvectors(VirtioBusState *bus, int nvectors)
{
    assert(bus->vdev != NULL);
    bus->vdev->nvectors = nvectors;
}

/* Get the config_len field of the plugged device. */
size_t get_virtio_device_config_len(VirtioBusState *bus)
{
    assert(bus->vdev != NULL);
    return bus->vdev->config_len;
}

/* Get the features of the plugged device. */
uint32_t get_virtio_device_features(VirtioBusState *bus,
                                    uint32_t requested_features)
{
    VirtioDeviceClass *k;
    assert(bus->vdev != NULL);
    k = VIRTIO_DEVICE_GET_CLASS(bus->vdev);
    assert(k->get_features != NULL);
    return k->get_features(bus->vdev, requested_features);
}

/* Get bad features of the plugged device. */
uint32_t get_virtio_device_bad_features(VirtioBusState *bus)
{
    VirtioDeviceClass *k;
    assert(bus->vdev != NULL);
    k = VIRTIO_DEVICE_GET_CLASS(bus->vdev);
    if (k->bad_features != NULL) {
        return k->bad_features(bus->vdev);
    } else {
        return 0;
    }
}

/* Get config of the plugged device. */
void get_virtio_device_config(VirtioBusState *bus, uint8_t *config)
{
    VirtioDeviceClass *k;
    assert(bus->vdev != NULL);
    k = VIRTIO_DEVICE_GET_CLASS(bus->vdev);
    if (k->get_config != NULL) {
        k->get_config(bus->vdev, config);
    }
}

static const TypeInfo virtio_bus_info = {
    .name = TYPE_VIRTIO_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(VirtioBusState),
    .abstract = true,
    .class_size = sizeof(VirtioBusClass),
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_bus_info);
}

type_init(virtio_register_types)
