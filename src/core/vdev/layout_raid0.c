// SPDX-License-Identifier: GPL-2.0-only

#include "core/vdev/vdev.h"

/*
 * Capacity = nr_members * floor(min_usable_sectors / chunk_sectors) * chunk_sectors.
 * chunk_sectors must be > 0 and a multiple of the cell size in sectors.
 */
static s32 lsv_raid0_geometry(struct lsv_vdev *v)
{
	return -ENOSYS;
}

/*
 * Map io->vsector to (member, member_sector).
 * member_sector includes the selected member's metadata reserve.
 */
static s32 lsv_raid0_submit(struct lsv_vdev *v, struct lsv_vio *io)
{
	return -ENOSYS;
}

const struct lsv_layout_ops lsv_layout_raid0_ops = {
	.name = "raid0",
	.type = LSV_LAYOUT_RAID0,
	.geometry = lsv_raid0_geometry,
	.submit = lsv_raid0_submit,
	.destroy = NULL,
};
