/* SPDX-License-Identifier: GPL-2.0-only */

#include <linux/blkdev.h>
#include <linux/configfs.h>
#include <linux/ctype.h>
#include <linux/log2.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "../core/dev.h"
#include "../utils/ds_control.h"
#include "configfs.h"

/*
 * Device creation protocol:
 *
 *	mkdir /sys/kernel/config/lsv/lsv0
 *	echo /dev/nvme0n1 /dev/nvme1n1 > /sys/kernel/config/lsv/lsv0/back_paths
 *	echo ht		  > /sys/kernel/config/lsv/lsv0/index_ds
 *	echo raid0		  > /sys/kernel/config/lsv/lsv0/layout
 *	echo 4096	  > /sys/kernel/config/lsv/lsv0/cell_size
  *	echo 8192	  > /sys/kernel/config/lsv/lsv0/chunk_size
 *	echo 2097152	  > /sys/kernel/config/lsv/lsv0/segment_size
 *	echo 1		  > /sys/kernel/config/lsv/lsv0/dedup
 *	echo 1		  > /sys/kernel/config/lsv/lsv0/create_new
 *
 * mkdir only allocates a description of a future device. The device itself
 * is instantiated by writing to create_new, once every attribute is set.
 * Attributes are immutable after that; rmdir tears the description down.
 */

static inline struct lsv_cfg_dev *to_lsv_cfg_dev(struct config_item *item)
{
	return container_of(item, struct lsv_cfg_dev, item);
}

/*
 * Copies a userspace-written value into a fixed buffer, trimming the trailing
 * newline that echo appends. Rejects writes once the device was instantiated.
 */
static ssize_t lsv_cfg_store_str(struct lsv_cfg_dev *cfg, char *dst, size_t dst_size, const char *page, size_t count)
{
	ssize_t rc = count;

	if (!count || count >= dst_size)
		return -EINVAL;

	mutex_lock(&cfg->lock);

	if (cfg->created) {
		rc = -EBUSY;
		goto out;
	}

	memcpy(dst, page, count);
	dst[count] = '\0';
	strim(dst);

	if (!dst[0])
		rc = -EINVAL;

out:
	mutex_unlock(&cfg->lock);
	return rc;
}

static ssize_t lsv_cfg_dev_back_paths_show(struct config_item *item, char *page)
{
	struct lsv_cfg_dev *cfg = to_lsv_cfg_dev(item);
	u32 i;
	ssize_t len = 0;
	ssize_t written;

	mutex_lock(&cfg->lock);
	for (i = 0; i < cfg->nr_members; i++)
	{
		written = snprintf(page + len, PAGE_SIZE - len, "%s%c", cfg->back_paths[i], i == cfg->nr_members - 1 ? '\n' : ' ');
		if (written >= PAGE_SIZE - len)
		{
			len = -E2BIG;
			break;
		}
		len += written;
	}
	mutex_unlock(&cfg->lock);

	return len;
}

static ssize_t lsv_cfg_dev_back_paths_store(struct config_item *item, const char *page, size_t count)
{
	struct lsv_cfg_dev *cfg = to_lsv_cfg_dev(item);
	char *page_copy, *cursor, *token;
	u32 nr_members = 0, i = 0;
	const char **back_paths;
	ssize_t rc = count;

	if (!count)
		return -EINVAL;

	page_copy = kstrndup(page, count, GFP_KERNEL);
	if (!page_copy)
		return -ENOMEM;

	cursor = page_copy;
	while ((token = strsep(&cursor, " \t\n")) != NULL)
	{
		if (!token[0])
			continue;
		nr_members++;
	}

	kfree(page_copy);
	if (!nr_members)
		return -EINVAL;

	page_copy = kstrndup(page, count, GFP_KERNEL);
	if (!page_copy)
		return -ENOMEM;

	back_paths = kcalloc(nr_members, sizeof(*back_paths), GFP_KERNEL);
	if (!back_paths)
	{
		kfree(page_copy);
		return -ENOMEM;
	}

	cursor = page_copy;
	while ((token = strsep(&cursor, " \t\n")) != NULL)
	{
		if (!token[0])
			continue;

		back_paths[i] = kstrdup(token, GFP_KERNEL);
		if (!back_paths[i])
		{
			kfree(page_copy);
			rc = -ENOMEM;
			goto free_back_paths;
		}
		i++;
	}
	kfree(page_copy);

	mutex_lock(&cfg->lock);

	if (cfg->created)
	{
		mutex_unlock(&cfg->lock);
		rc = -EBUSY;
		goto free_back_paths;
	}

	if (cfg->back_paths)
	{
		for (i = 0; i < cfg->nr_members; i++)
		{
			kfree(cfg->back_paths[i]);
		}
		kfree(cfg->back_paths);
	}
	cfg->back_paths = back_paths;
	cfg->nr_members = nr_members;

	mutex_unlock(&cfg->lock);
	return rc;

free_back_paths:
	while (i--)
		kfree(back_paths[i]);
	kfree(back_paths);
	return rc;
}
CONFIGFS_ATTR(lsv_cfg_dev_, back_paths);

static ssize_t lsv_cfg_dev_index_ds_show(struct config_item *item, char *page)
{
	struct lsv_cfg_dev *cfg = to_lsv_cfg_dev(item);
	ssize_t len;

	mutex_lock(&cfg->lock);
	len = snprintf(page, PAGE_SIZE, "%s\n", cfg->index_ds);
	mutex_unlock(&cfg->lock);

	return len;
}

static ssize_t lsv_cfg_dev_index_ds_store(struct config_item *item, const char *page, size_t count)
{
	struct lsv_cfg_dev *cfg = to_lsv_cfg_dev(item);
	char buf[LSV_CTL_MAX_DS_NAME_LEN + 1];
	char *name;

	if (!count || count >= sizeof(buf))
		return -EINVAL;

	memcpy(buf, page, count);
	buf[count] = '\0';
	name = strim(buf);

	if (!lsv_ds_check_available(name)) {
		pr_warn("lsv: unknown data structure '%s'\n", name);
		return -EINVAL;
	}

	return lsv_cfg_store_str(cfg, cfg->index_ds, sizeof(cfg->index_ds), name, strlen(name));
}
CONFIGFS_ATTR(lsv_cfg_dev_, index_ds);

static ssize_t lsv_cfg_dev_layout_show(struct config_item *item, char *page)
{
	struct lsv_cfg_dev *cfg = to_lsv_cfg_dev(item);
	ssize_t len;

	mutex_lock(&cfg->lock);
	len = snprintf(page, PAGE_SIZE, "%s\n", cfg->layout);
	mutex_unlock(&cfg->lock);

	return len;
}

static ssize_t lsv_cfg_dev_layout_store(struct config_item *item, const char *page, size_t count)
{
	struct lsv_cfg_dev *cfg = to_lsv_cfg_dev(item);
	char buf[LSV_CTL_MAX_LAYOUT_NAME_LEN + 1];
	char *name;

	if (!count || count >= sizeof(buf))
		return -EINVAL;

	memcpy(buf, page, count);
	buf[count] = '\0';
	name = strim(buf);

	if (!lsv_layout_lookup(name)) {
		pr_warn("lsv: unknown layout '%s'\n", name);
		return -EINVAL;
	}

	return lsv_cfg_store_str(cfg, cfg->layout, sizeof(cfg->layout), name, strlen(name));
}
CONFIGFS_ATTR(lsv_cfg_dev_, layout);

static ssize_t lsv_cfg_dev_segment_size_show(struct config_item *item, char *page)
{
	struct lsv_cfg_dev *cfg = to_lsv_cfg_dev(item);
	ssize_t len;

	mutex_lock(&cfg->lock);
	len = snprintf(page, PAGE_SIZE, "%llu\n", cfg->segment_size);
	mutex_unlock(&cfg->lock);

	return len;
}

static ssize_t lsv_cfg_dev_segment_size_store(struct config_item *item, const char *page, size_t count)
{
	struct lsv_cfg_dev *cfg = to_lsv_cfg_dev(item);
	ssize_t rc = count;
	u64 value;

	if (kstrtou64(page, 0, &value) || !value)
		return -EINVAL;

	mutex_lock(&cfg->lock);

	if (cfg->created)
		rc = -EBUSY;
	else
		cfg->segment_size = value;

	mutex_unlock(&cfg->lock);
	return rc;
}
CONFIGFS_ATTR(lsv_cfg_dev_, segment_size);

static ssize_t lsv_cfg_dev_cell_size_show(struct config_item *item, char *page)
{
	struct lsv_cfg_dev *cfg = to_lsv_cfg_dev(item);
	ssize_t len;

	mutex_lock(&cfg->lock);
	len = snprintf(page, PAGE_SIZE, "%u\n", cfg->cell_size);
	mutex_unlock(&cfg->lock);

	return len;
}

static ssize_t lsv_cfg_dev_cell_size_store(struct config_item *item, const char *page, size_t count)
{
	struct lsv_cfg_dev *cfg = to_lsv_cfg_dev(item);
	ssize_t rc = count;
	u32 value;

	if (kstrtou32(page, 0, &value))
		return -EINVAL;

	if (value < LSV_CELL_SIZE_MIN || value > LSV_CELL_SIZE_MAX || !is_power_of_2(value) || (value & (SECTOR_SIZE - 1)))
		return -EINVAL;

	mutex_lock(&cfg->lock);

	if (cfg->created)
		rc = -EBUSY;
	else
		cfg->cell_size = value;

	mutex_unlock(&cfg->lock);
	return rc;
}
CONFIGFS_ATTR(lsv_cfg_dev_, cell_size);

static ssize_t lsv_cfg_dev_chunk_size_show(struct config_item *item, char *page)
{
	struct lsv_cfg_dev *cfg = to_lsv_cfg_dev(item);
	ssize_t len;

	mutex_lock(&cfg->lock);
	len = snprintf(page, PAGE_SIZE, "%u\n", cfg->chunk_size);
	mutex_unlock(&cfg->lock);

	return len;
}

static ssize_t lsv_cfg_dev_chunk_size_store(struct config_item *item, const char *page, size_t count)
{
	struct lsv_cfg_dev *cfg = to_lsv_cfg_dev(item);
	ssize_t rc = count;
	u32 value;

	if (kstrtou32(page, 0, &value))
		return -EINVAL;

	if (value & (SECTOR_SIZE - 1))
		return -EINVAL;

	mutex_lock(&cfg->lock);

	if (cfg->created)
		rc = -EBUSY;
	else
		cfg->chunk_size = value;

	mutex_unlock(&cfg->lock);
	return rc;
}
CONFIGFS_ATTR(lsv_cfg_dev_, chunk_size);

static ssize_t lsv_cfg_dev_dedup_show(struct config_item *item, char *page)
{
	struct lsv_cfg_dev *cfg = to_lsv_cfg_dev(item);
	ssize_t len;

	mutex_lock(&cfg->lock);
	len = snprintf(page, PAGE_SIZE, "%d\n", cfg->dedup ? 1 : 0);
	mutex_unlock(&cfg->lock);

	return len;
}

static ssize_t lsv_cfg_dev_dedup_store(struct config_item *item, const char *page, size_t count)
{
	struct lsv_cfg_dev *cfg = to_lsv_cfg_dev(item);
	ssize_t rc = count;
	bool value;

	if (kstrtobool(page, &value))
		return -EINVAL;

	mutex_lock(&cfg->lock);

	if (cfg->created)
		rc = -EBUSY;
	else
		cfg->dedup = value;

	mutex_unlock(&cfg->lock);
	return rc;
}
CONFIGFS_ATTR(lsv_cfg_dev_, dedup);

static ssize_t lsv_cfg_dev_create_new_show(struct config_item *item, char *page)
{
	struct lsv_cfg_dev *cfg = to_lsv_cfg_dev(item);

	return snprintf(page, PAGE_SIZE, "%d\n", READ_ONCE(cfg->created) ? 1 : 0);
}

static ssize_t lsv_cfg_dev_create_new_store(struct config_item *item, const char *page, size_t count)
{
	struct lsv_cfg_dev *cfg = to_lsv_cfg_dev(item);
	struct lsv_dev_params params;
	ssize_t rc = count;
	bool trigger;

	if (kstrtobool(page, &trigger))
		return -EINVAL;

	/* Teardown is done with rmdir, not by writing 0 here. */
	if (!trigger)
		return -EINVAL;

	mutex_lock(&cfg->lock);

	if (cfg->created) {
		rc = -EEXIST;
		goto out;
	}

	if (!cfg->nr_members) {
		rc = -EINVAL;
		goto out;
	}

	if (!strlen(cfg->index_ds)) {
		rc = -EINVAL;
		goto out;
	}

	if (!strlen(cfg->layout)) {
		rc = -EINVAL;
		goto out;
	}

	if (!cfg->cell_size) {
		rc = -EINVAL;
		goto out;
	}

	/*
	 * The configfs directory name is the device identity, so it is handed
	 * to the core explicitly. Everything the core needs travels in a plain
	 * parameter struct: core must not know about configfs.
	 */
	params.name = config_item_name(&cfg->item);
	params.back_paths = cfg->back_paths;
	params.nr_members = cfg->nr_members;
	params.index_ds = cfg->index_ds;
	params.layout = cfg->layout;
	params.cell_size = cfg->cell_size;
	params.chunk_size = cfg->chunk_size;
	params.segment_size = cfg->segment_size;
	params.dedup = cfg->dedup;

	rc = lsv_dev_create(&params, &cfg->dev);
	if (rc)
		goto out;

	cfg->created = true;
	rc = count;

out:
	mutex_unlock(&cfg->lock);
	return rc;
}
CONFIGFS_ATTR(lsv_cfg_dev_, create_new);

static struct configfs_attribute *lsv_cfg_dev_attrs[] = {
	&lsv_cfg_dev_attr_back_paths,
	&lsv_cfg_dev_attr_index_ds,
	&lsv_cfg_dev_attr_layout,
	&lsv_cfg_dev_attr_cell_size,
	&lsv_cfg_dev_attr_chunk_size,
	&lsv_cfg_dev_attr_segment_size,
	&lsv_cfg_dev_attr_dedup,
	&lsv_cfg_dev_attr_create_new,
	NULL,
};

static void lsv_cfg_dev_release(struct config_item *item)
{
	struct lsv_cfg_dev *cfg = to_lsv_cfg_dev(item);
	u32 i;

	if (cfg->created)
		lsv_dev_destroy(cfg->dev);

	mutex_destroy(&cfg->lock);
	for (i = 0; i < cfg->nr_members; i++)
	{
		kfree(cfg->back_paths[i]);
	}
	kfree(cfg->back_paths);
	kfree(cfg);
}

static struct configfs_item_operations lsv_cfg_dev_item_ops = {
	.release = lsv_cfg_dev_release,
};

static const struct config_item_type lsv_cfg_dev_type = {
	.ct_item_ops = &lsv_cfg_dev_item_ops,
	.ct_attrs = lsv_cfg_dev_attrs,
	.ct_owner = THIS_MODULE,
};

static struct config_item *lsv_cfg_make_item(struct config_group *group, const char *name)
{
	struct lsv_cfg_dev *cfg;

	size_t i;

	/* The name becomes gendisk->disk_name, so validate it here. */
	if (strlen(name) > LSV_CTL_MAX_BD_NAME_LENGTH || strlen(name) >= DISK_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	for (i = 0; name[i]; i++)
		if (!isalnum(name[i]) && name[i] != '-' && name[i] != '_')
			return ERR_PTR(-EINVAL);

	cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
	if (!cfg)
		return ERR_PTR(-ENOMEM);

	mutex_init(&cfg->lock);
	config_item_init_type_name(&cfg->item, name, &lsv_cfg_dev_type);

	return &cfg->item;
}

static struct configfs_group_operations lsv_cfg_group_ops = {
	.make_item = lsv_cfg_make_item,
};

static const struct config_item_type lsv_cfg_subsys_type = {
	.ct_group_ops = &lsv_cfg_group_ops,
	.ct_owner = THIS_MODULE,
};

static struct configfs_subsystem lsv_cfg_subsys = {
	.su_group = {
		.cg_item = {
			.ci_namebuf = "lsv",
			.ci_type = &lsv_cfg_subsys_type,
		},
	},
};

s32 lsv_configfs_register(void)
{
	s32 rc;

	config_group_init(&lsv_cfg_subsys.su_group);
	mutex_init(&lsv_cfg_subsys.su_mutex);

	rc = configfs_register_subsystem(&lsv_cfg_subsys);
	if (rc)
		pr_err("lsv: configfs registration failed: %d\n", rc);

	return rc;
}

void lsv_configfs_unregister(void)
{
	configfs_unregister_subsystem(&lsv_cfg_subsys);
}
