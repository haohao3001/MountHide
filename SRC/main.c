// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/errno.h>

#include "core.h"
#include "mh.h"

/* 加载参数：要隐藏的挂载点列表（默认 /debug_ramdisk），可用 hide_mounts=... 覆盖 */
char *hide_mounts[64] = { "/debug_ramdisk" };
int hide_count = 1;
MODULE_PARM_DESC(hide_mounts, "mountpoints to hide from non root readers");

/* 自动扫描特征：root 路径含该子串的挂载全部登记隐藏；置空则关闭扫描 */
char *scan_feature = "/adb/";
MODULE_PARM_DESC(scan_feature, "root path feature to auto hide, empty disables");

/* uid 可见性规则：allow 列表内的 uid 始终可见；hide 列表内的 uid 永远不可见（hide 优先） */
char *allow_uids = "";
MODULE_PARM_DESC(allow_uids, "comma separated uids that always see the mounts");

char *hide_uids = "";
MODULE_PARM_DESC(hide_uids, "comma separated uids that never see the mounts");

unsigned int restart = 0;
MODULE_PARM_DESC(restart, "apply rules");

static void mh_parse_uids(const char *str, int (*fn)(unsigned int))
{
	char buf[128];
	char *p;
	char *tok;

	if (!str || !*str)
		return;
	strscpy(buf, str, sizeof(buf));
	p = buf;
	while ((tok = strsep(&p, ",")) != NULL) {
		unsigned int uid;

		if (!*tok)
			continue;	/* 跳过空 token（连续逗号/尾部逗号产生） */
		if (kstrtouint(tok, 10, &uid))
			continue;	/* 非数字 token 直接忽略 */
		fn(uid);
	}
}

static DEFINE_MUTEX(mh_cfg_lock);
static void mh_cfg_apply(){
	if (!mh_is_inited())
		return;   
	mutex_lock(&mh_cfg_lock); 
	mh_hide_clear();
	mh_reader_reset();

	mh_parse_uids(allow_uids, mh_reader_allow_uid);
	mh_parse_uids(hide_uids, mh_reader_hide_uid);
	for (int i = 0; i < hide_count; i++) {
		int ret = mh_hide_path(hide_mounts[i]);
		pr_info("[mhsrc] hide %s -> %d\n", hide_mounts[i], ret);
	}

	if (scan_feature && *scan_feature) {
		int ret = mh_hide_scan(scan_feature);
		pr_info("[mhsrc] scan %s -> %d\n", scan_feature, ret);
	}
}

static int mh_restart(const char *val, const struct kernel_param *kp){
	bool on;

	if (kstrtobool(val, &on))
		return -EINVAL;
	if (on)
		mh_cfg_apply();
	return 0;
}

static const struct kernel_param_ops mh_restart_ops = {
    .set = mh_restart,
    .get = param_get_uint,   /* 读侧：复用标准实现（返回 0）*/
};

static unsigned long __nocfi kr_name_to_addr(const char *name)
{
	if (kallrecon_klp)
		return kallrecon_klp(name);
	return 0;
}

static int __init mh_src_init(void)
{
	struct mh_cfg cfg = {
		.resolve = kr_name_to_addr,
	};
	int ret;

	find_kallsyms_base();
	if (!klnum_val || !kallrecon_klp) {
		pr_err("[mhsrc] kallsyms recovery failed\n");
		return -ENODATA;
	}

	ret = mh_init(&cfg);
	if (ret) {
		pr_err("[mhsrc] mh_init failed %d\n", ret);
		return ret;
	}

	ret = mh_proc_enable();
	if (ret) {
		pr_err("[mhsrc] mh_proc_enable failed %d\n", ret);
		mh_exit();	/* 回滚：恢复 show 回调、清规则表、退出 hook 栈 */
		return ret;
	}

	mh_cfg_apply();

	return 0;
}

static void __exit mh_src_exit(void)
{
	mh_exit();
	pr_info("[mhsrc] unloaded\n");
}

module_init(mh_src_init);
module_exit(mh_src_exit);
module_param_array(hide_mounts, charp, &hide_count, 0644);
module_param(scan_feature, charp, 0644);
module_param(allow_uids, charp, 0644);
module_param(hide_uids, charp, 0644);
module_param_cb(restart, &mh_restart_ops, &restart, 0200);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Mount hiding consumer demo for PrivIsolated");