// SPDX-License-Identifier: GPL-2.0-only

#include "core/vdev/vdev.h"

static s32 lsv_raid1_geometry(struct lsv_vdev *vdev)
{
	if (vdev->nr_members < 2)
	{
		return -EINVAL;
	}

	vdev->capacity_sectors = lsv_vdev_min_usable_sectors(vdev);
	return 0;
}

/*
 * Reads select an available, up-to-date copy; writes are sent
 * to the participating mirrors.
 */
static s32 lsv_raid1_submit(struct lsv_vdev *v, struct lsv_vio *io)
{
	return -ENOSYS;
}

const struct lsv_layout_ops lsv_layout_raid1_ops = {
	.name = "raid1",
	.type = LSV_LAYOUT_RAID1,
	.geometry = lsv_raid1_geometry,
	.submit = lsv_raid1_submit,
	.destroy = NULL,
};
