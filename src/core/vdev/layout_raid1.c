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

static s32 lsv_raid1_submit_read(struct lsv_vdev *vdev, struct lsv_vio *io)
{
	struct lsv_vdev_target target;

	target.member_sector = LSV_MEMBER_META_SECTORS + io->vsector;
	target.member = &vdev->members[0];
	return lsv_vdev_member_submit(vdev, io, &target);
}

static s32 lsv_raid1_submit_write(struct lsv_vdev *vdev, struct lsv_vio *io)
{
	struct lsv_vdev_target target;
	u32 i;
	s32 rc;

	target.member_sector = LSV_MEMBER_META_SECTORS + io->vsector;
	for (i = 0; i < vdev->nr_members; i++)
	{
		target.member = &vdev->members[i];

		rc = lsv_vdev_member_submit(vdev, io, &target);
		if (rc < 0)
		{
			return rc;
		}
	}

	return 0;
}

static s32 lsv_raid1_submit(struct lsv_vdev *vdev, struct lsv_vio *io)
{
	if (bio_op(io->bio) == REQ_OP_READ)
		return lsv_raid1_submit_read(vdev, io);

	if (bio_op(io->bio) == REQ_OP_WRITE)
		return lsv_raid1_submit_write(vdev, io);

	return -EOPNOTSUPP;
}

const struct lsv_layout_ops lsv_layout_raid1_ops = {
	.name = "raid1",
	.type = LSV_LAYOUT_RAID1,
	.geometry = lsv_raid1_geometry,
	.submit = lsv_raid1_submit,
	.destroy = NULL,
};
