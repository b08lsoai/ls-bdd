// SPDX-License-Identifier: GPL-2.0-only

#include "core/vdev/vdev.h"

static s32 lsv_raid0_geometry(struct lsv_vdev *vdev)
{
	sector_t min_usable;
	u64 chunks_per_member;

	if (vdev->nr_members < 2)
	{
		return -EINVAL;
	}

	if (!vdev->chunk_sectors || vdev->chunk_sectors & (vdev->cell_sectors - 1))
	{
		return -EINVAL;
	}

	min_usable = lsv_vdev_min_usable_sectors(vdev);
	chunks_per_member = min_usable / vdev->chunk_sectors;
	vdev->capacity_sectors =
		chunks_per_member * vdev->chunk_sectors * vdev->nr_members;
	return 0;
}

static s32 lsv_raid0_submit(struct lsv_vdev *vdev, struct lsv_vio *io)
{
	struct lsv_vdev_target target;
	sector_t chunk = vdev->chunk_sectors;
	sector_t stripe = chunk * vdev->nr_members;
	sector_t member_sector;
	u32 member_index;

	member_index = (io->vsector / vdev->chunk_sectors) % vdev->nr_members;
	target.member = &vdev->members[member_index];

	member_sector = (io->vsector / stripe) * chunk + (io->vsector % chunk);
	target.member_sector = LSV_MEMBER_META_SECTORS + member_sector;

	return lsv_vdev_member_submit(vdev, io, &target);
}

const struct lsv_layout_ops lsv_layout_raid0_ops = {
	.name = "raid0",
	.type = LSV_LAYOUT_RAID0,
	.geometry = lsv_raid0_geometry,
	.submit = lsv_raid0_submit,
	.destroy = NULL,
};
