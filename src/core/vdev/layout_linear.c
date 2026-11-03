// SPDX-License-Identifier: GPL-2.0-only

#include "core/vdev/vdev.h"

/*
 * v->capacity_sectors = sum of lsv_member_usable_sectors()
 * over all members. Round each member
 * down to whole cells before summing.
 */
static s32 lsv_linear_geometry(struct lsv_vdev *v)
{
	return -ENOSYS;
}

/*
 * Map io->vsector to (member, member_sector).
 * member_sector includes the selected member's metadata reserve.
 */
static s32 lsv_linear_submit(struct lsv_vdev *v, struct lsv_vio *io)
{
	return -ENOSYS;
}

const struct lsv_layout_ops lsv_layout_linear_ops = {
	.name = "linear",
	.type = LSV_LAYOUT_LINEAR,
	.geometry = lsv_linear_geometry,
	.submit = lsv_linear_submit,
	.destroy = NULL,
};
