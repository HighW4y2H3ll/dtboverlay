#include <linux/module.h>
#include <linux/of.h>

MODULE_LICENSE("GPL");

static void dump_recurse(struct device_node *n, int depth) {
    int i;
    char buf[256] = {0};
    if (!n) return;

    for (i = 0; i < depth; i++)
        buf[i] = ' ';
    printk("%s> %s", buf, n->name);
}

static int __init of_check_init(void)
{
	int ret = 0;
    struct device_node *n;

    printk("DTB root %s\n", of_root->name);

out:
	return ret;
}

static void __exit of_check_exit(void) {
}

module_init(of_check_init);
module_exit(of_check_exit);
