//#include <linux/ctype.h>
//#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/kprobes.h>
//#include <linux/spinlock.h>
//#include <linux/sizes.h>
//#include <linux/slab.h>
//#include <linux/proc_fs.h>
//#include <linux/configfs.h>
//#include <linux/types.h>
//#include <linux/stat.h>
//#include <linux/limits.h>
//#include <linux/file.h>
//#include <linux/vmalloc.h>
//#include <linux/firmware.h>


MODULE_LICENSE("GPL");

int fdt_check_header(const void *fdt);

static char *dtbpath = NULL;
module_param(dtbpath, charp, 0000);
MODULE_PARM_DESC(dtbpath, "dtb path");

static struct kprobe kp = {
    .symbol_name = "kallsyms_lookup_name"
};

static void debug(void) {
    struct device_node *root, *symchild;
    struct property *prop;
    struct kobj_type *of_node_ktype_ptr;
    unsigned long (*kallsyms_lookup_name_ptr)(const char *);
    int (*__of_attach_node_sysfs_ptr)(struct device_node *) ;

    printk("DEBUG of_root %llx", (u64)of_root);
    if (!of_root) {
        // Create root node
        root = kmalloc(sizeof(struct device_node), GFP_ATOMIC);
        // kallsyms_lookup_name not exported >5.7
        register_kprobe(&kp);
        kallsyms_lookup_name_ptr = (unsigned long (*)(const char*))kp.addr;
        printk("DEBUG kallsyms_lookup_name %px", kp.addr);
        unregister_kprobe(&kp);
        printk("DEBUG kallsyms_lookup_name %px", kallsyms_lookup_name_ptr);
        of_node_ktype_ptr = (struct kobj_type*)kallsyms_lookup_name_ptr("of_node_ktype");
        printk("DEBUG of_node_ktype_ptr %px", of_node_ktype_ptr);
        __of_attach_node_sysfs_ptr = (int (*)(struct device_node*))kallsyms_lookup_name_ptr("__of_attach_node_sysfs");
        printk("DEBUG __of_attach_node_sysfs_ptr %px", __of_attach_node_sysfs_ptr);
        //of_node_init(root);
        kobject_init(&root->kobj, of_node_ktype_ptr);
        root->parent = NULL;
        root->name = "/";
        root->full_name = "/";
        root->phandle = 0;
        root->properties = NULL;
        root->kobj.name = kstrdup("/", GFP_KERNEL);

        symchild = kmalloc(sizeof(struct device_node), GFP_ATOMIC);
        kobject_init(&symchild->kobj, of_node_ktype_ptr);
        symchild->parent = root;
        symchild->name = "/__symbols__";
        symchild->full_name = "/__symbols__";
        symchild->phandle = 0;
        symchild->properties = NULL;
        symchild->kobj.name = kstrdup("/__symbols__", GFP_KERNEL);
        root->child = symchild;

        of_root = root;
        printk("DEBUG attach root sysfs");
        __of_attach_node_sysfs_ptr(root);
        printk("DEBUG attach symbols sysfs");
        __of_attach_node_sysfs_ptr(symchild);

        root = of_find_node_by_path("/__symbols__");
        if (!root) {
            pr_err("DEBUG node not found at '/__symbols__'");
            return;
        }
        for_each_property_of_node(root, prop) {
            printk("DEBUG prop %s\n", prop->name);
        }
    }
}


static int ovcsid;
static int __init of_overlay_init(void)
{
	int ret;
    struct file *dtbfile;
    void *dtb_begin;
    //struct kstat stat;
    loff_t pos = 0;
    loff_t size = 0;
    //mm_segment_t fs;

	pr_info("%s\n", __func__);

    dtbfile = filp_open(dtbpath, O_RDONLY, 0);
    if (IS_ERR(dtbfile)) {
        printk("DTB Open Error");
        ret = PTR_ERR(dtbfile);
        goto out;
    }

    //fs = get_fs();
    //set_fs(KERNEL_DS);

    //vfs_stat(dtbfile, &stat);
    size = i_size_read(dtbfile->f_path.dentry->d_inode);
    printk("DTB size %llx\n", size);

    dtb_begin = kzalloc(size, GFP_KERNEL);
    if (!dtb_begin) {
        printk("kmalloc error for size %llx", size);
        goto cleanup_file;
    }
    printk("DEBUG %llx", (u64)dtb_begin);
    kernel_read(dtbfile, dtb_begin, size, &pos);
    printk("DEBUG %llx, %llx", (u64)dtb_begin, pos);

    printk("TEST Err %d\n", fdt_check_header(dtb_begin));
    debug();
    ret = of_overlay_fdt_apply(dtb_begin, size, &ovcsid);

cleanup_file:
    filp_close(dtbfile, 0);
    //set_fs(fs);
out:
	return ret;
}

static void __exit of_overlay_exit(void) {
    of_overlay_remove(&ovcsid);
}

module_init(of_overlay_init);
module_exit(of_overlay_exit);
