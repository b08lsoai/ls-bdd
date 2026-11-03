// SPDX-License-Identifier: GPL-2.0-only

#include "core/vdev/vdev.h"

/*
 * Capacity = minimum usable capacity across members.
 */
static s32 lsv_raid1_geometry(struct lsv_vdev *v)
{
	return -ENOSYS;
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
