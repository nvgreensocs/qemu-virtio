/*
 * Virtio Balloon Device
 *
 * Copyright IBM, Corp. 2008
 * Copyright (C) 2011 Red Hat, Inc.
 * Copyright (C) 2011 Amit Shah <amit.shah@redhat.com>
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "qemu/iov.h"
#include "qemu-common.h"
#include "virtio.h"
#include "pc.h"
#include "cpu.h"
#include "sysemu/balloon.h"
#include "virtio-balloon.h"
#include "sysemu/kvm.h"
#include "exec/address-spaces.h"

#if defined(__linux__)
#include <sys/mman.h>
#endif

#include "virtio-bus.h"

/*
 * Will be modified later in the serie.
 */
static VirtIOBalloon *to_virtio_balloon(VirtIODevice *vdev)
{
    return (VirtIOBalloon *)vdev;
}

static void balloon_page(void *addr, int deflate)
{
#if defined(__linux__)
    if (!kvm_enabled() || kvm_has_sync_mmu())
        qemu_madvise(addr, TARGET_PAGE_SIZE,
                deflate ? QEMU_MADV_WILLNEED : QEMU_MADV_DONTNEED);
#endif
}

/*
 * reset_stats - Mark all items in the stats array as unset
 *
 * This function needs to be called at device intialization and before
 * before updating to a set of newly-generated stats.  This will ensure that no
 * stale values stick around in case the guest reports a subset of the supported
 * statistics.
 */
static inline void reset_stats(VirtIOBalloon *dev)
{
    int i;
    for (i = 0; i < VIRTIO_BALLOON_S_NR; dev->stats[i++] = -1);
}

static void virtio_balloon_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOBalloon *s = to_virtio_balloon(vdev);
    VirtQueueElement elem;
    MemoryRegionSection section;

    while (virtqueue_pop(vq, &elem)) {
        size_t offset = 0;
        uint32_t pfn;

        while (iov_to_buf(elem.out_sg, elem.out_num, offset, &pfn, 4) == 4) {
            ram_addr_t pa;
            ram_addr_t addr;

            pa = (ram_addr_t)ldl_p(&pfn) << VIRTIO_BALLOON_PFN_SHIFT;
            offset += 4;

            /* FIXME: remove get_system_memory(), but how? */
            section = memory_region_find(get_system_memory(), pa, 1);
            if (!section.size || !memory_region_is_ram(section.mr))
                continue;

            /* Using memory_region_get_ram_ptr is bending the rules a bit, but
               should be OK because we only want a single page.  */
            addr = section.offset_within_region;
            balloon_page(memory_region_get_ram_ptr(section.mr) + addr,
                         !!(vq == s->dvq));
        }

        virtqueue_push(vq, &elem, offset);
        virtio_notify(vdev, vq);
    }
}

static void virtio_balloon_receive_stats(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOBalloon *s = DO_UPCAST(VirtIOBalloon, vdev, vdev);
    VirtQueueElement *elem = &s->stats_vq_elem;
    VirtIOBalloonStat stat;
    size_t offset = 0;

    if (!virtqueue_pop(vq, elem)) {
        return;
    }

    /* Initialize the stats to get rid of any stale values.  This is only
     * needed to handle the case where a guest supports fewer stats than it
     * used to (ie. it has booted into an old kernel).
     */
    reset_stats(s);

    while (iov_to_buf(elem->out_sg, elem->out_num, offset, &stat, sizeof(stat))
           == sizeof(stat)) {
        uint16_t tag = tswap16(stat.tag);
        uint64_t val = tswap64(stat.val);

        offset += sizeof(stat);
        if (tag < VIRTIO_BALLOON_S_NR)
            s->stats[tag] = val;
    }
    s->stats_vq_offset = offset;
}

static void virtio_balloon_get_config(VirtIODevice *vdev, uint8_t *config_data)
{
    VirtIOBalloon *dev = to_virtio_balloon(vdev);
    struct virtio_balloon_config config;

    config.num_pages = cpu_to_le32(dev->num_pages);
    config.actual = cpu_to_le32(dev->actual);

    memcpy(config_data, &config, 8);
}

static void virtio_balloon_set_config(VirtIODevice *vdev,
                                      const uint8_t *config_data)
{
    VirtIOBalloon *dev = to_virtio_balloon(vdev);
    struct virtio_balloon_config config;
    uint32_t oldactual = dev->actual;
    memcpy(&config, config_data, 8);
    dev->actual = le32_to_cpu(config.actual);
    if (dev->actual != oldactual) {
        qemu_balloon_changed(ram_size -
                             (dev->actual << VIRTIO_BALLOON_PFN_SHIFT));
    }
}

static uint32_t virtio_balloon_get_features(VirtIODevice *vdev, uint32_t f)
{
    f |= (1 << VIRTIO_BALLOON_F_STATS_VQ);
    return f;
}

static void virtio_balloon_stat(void *opaque, BalloonInfo *info)
{
    VirtIOBalloon *dev = opaque;

#if 0
    /* Disable guest-provided stats for now. For more details please check:
     * https://bugzilla.redhat.com/show_bug.cgi?id=623903
     *
     * If you do enable it (which is probably not going to happen as we
     * need a new command for it), remember that you also need to fill the
     * appropriate members of the BalloonInfo structure so that the stats
     * are returned to the client.
     */
    if (dev->vdev.guest_features & (1 << VIRTIO_BALLOON_F_STATS_VQ)) {
        virtqueue_push(dev->svq, &dev->stats_vq_elem, dev->stats_vq_offset);
        virtio_notify(&dev->vdev, dev->svq);
        return;
    }
#endif

    /* Stats are not supported.  Clear out any stale values that might
     * have been set by a more featureful guest kernel.
     */
    reset_stats(dev);

    info->actual = ram_size - ((uint64_t) dev->actual <<
                               VIRTIO_BALLOON_PFN_SHIFT);
}

static void virtio_balloon_to_target(void *opaque, ram_addr_t target)
{
    VirtIOBalloon *dev = opaque;

    if (target > ram_size) {
        target = ram_size;
    }
    if (target) {
        dev->num_pages = (ram_size - target) >> VIRTIO_BALLOON_PFN_SHIFT;
        virtio_notify_config(&dev->vdev);
    }
}

static void virtio_balloon_save(QEMUFile *f, void *opaque)
{
    VirtIOBalloon *s = opaque;

    virtio_save(&s->vdev, f);

    qemu_put_be32(f, s->num_pages);
    qemu_put_be32(f, s->actual);
}

static int virtio_balloon_load(QEMUFile *f, void *opaque, int version_id)
{
    VirtIOBalloon *s = opaque;
    int ret;

    if (version_id != 1)
        return -EINVAL;

    ret = virtio_load(&s->vdev, f);
    if (ret) {
        return ret;
    }

    s->num_pages = qemu_get_be32(f);
    s->actual = qemu_get_be32(f);
    return 0;
}

static int virtio_balloon_device_init(VirtIODevice *vdev)
{
    DeviceState *qdev = DEVICE(vdev);
    VirtIOBalloon *s = VIRTIO_BALLOON(vdev);
    int ret;

    virtio_init(vdev, "virtio-balloon", VIRTIO_ID_BALLOON, 8);

    /*
     * This will disappear later in the serie.
     * We will use VirtioDeviceClass instead.
     */
    s->vdev.get_config = virtio_balloon_get_config;
    s->vdev.set_config = virtio_balloon_set_config;
    s->vdev.get_features = virtio_balloon_get_features;

    ret = qemu_add_balloon_handler(virtio_balloon_to_target,
                                   virtio_balloon_stat, s);

    if (ret < 0) {
        virtio_common_cleanup(VIRTIO_DEVICE(s));
        return -1;
    }

    s->ivq = virtio_add_queue(vdev, 128, virtio_balloon_handle_output);
    s->dvq = virtio_add_queue(vdev, 128, virtio_balloon_handle_output);
    s->svq = virtio_add_queue(vdev, 128, virtio_balloon_receive_stats);

    reset_stats(s);

    s->qdev = qdev;
    register_savevm(qdev, "virtio-balloon", -1, 1,
                    virtio_balloon_save, virtio_balloon_load, s);

    return 0;
}

static int virtio_balloon_device_exit(DeviceState *qdev)
{
    VirtIOBalloon *s = VIRTIO_BALLOON(qdev);
    VirtIODevice *vdev = VIRTIO_DEVICE(qdev);

    qemu_remove_balloon_handler(s);
    unregister_savevm(qdev, "virtio-balloon", s);
    virtio_common_cleanup(vdev);
    return 0;
}

static Property virtio_balloon_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_balloon_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);
    dc->exit = virtio_balloon_device_exit;
    dc->props = virtio_balloon_properties;
    vdc->init = virtio_balloon_device_init;
    vdc->get_config = virtio_balloon_get_config;
    vdc->set_config = virtio_balloon_set_config;
    vdc->get_features = virtio_balloon_get_features;
}

static const TypeInfo virtio_balloon_info = {
    .name = TYPE_VIRTIO_BALLOON,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VirtIOBalloon),
    .class_init = virtio_balloon_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_balloon_info);
}

type_init(virtio_register_types)
