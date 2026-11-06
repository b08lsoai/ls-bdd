// SPDX-License-Identifier: GPL-2.0-only

#include "core/vdev/vdev.h"

static s32 lsv_linear_geometry(struct lsv_vdev *vdev)
{
	u64 capacity = 0;
	u32 i;

	for (i = 0; i < vdev->nr_members; i++)
	{
		u64 usable = lsv_member_usable_sectors(vdev, &vdev->members[i]);

		if (!usable)
			return -ENOSPC;

		capacity += usable;
	}

	vdev->capacity_sectors = capacity;
	return 0;
}

static s32 lsv_linear_submit(struct lsv_vdev *vdev, struct lsv_vio *io)
{
	struct lsv_vdev_target target;
	sector_t member_offset = io->vsector;
	u32 i;

	for (i = 0; i < vdev->nr_members; i++)
	{
		u64 usable = lsv_member_usable_sectors(vdev, &vdev->members[i]);

		if (member_offset >= usable)
		{
			member_offset -= usable;
			continue;
		}

		target.member = &vdev->members[i];
		target.member_sector = LSV_MEMBER_META_SECTORS + member_offset;

		return lsv_vdev_member_submit(vdev, io, &target);
	}

	return -EIO;
}

const struct lsv_layout_ops lsv_layout_linear_ops = {
	.name = "linear",
	.type = LSV_LAYOUT_LINEAR,
	.geometry = lsv_linear_geometry,
	.submit = lsv_linear_submit,
	.destroy = NULL,
};
