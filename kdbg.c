#include <linux/module.h>
#include <linux/kprobes.h>

static struct kprobe kp = {
    //.symbol_name = "do_mmap",
    .symbol_name = "__arm64_sys_getdents64",
};

static int dump_contextidr(struct kprobe *p, struct pt_regs *regs) {
    u32 contextidr;
    asm volatile(
            "mrs %0, CONTEXTIDR_EL1\n"
            : "=r" (contextidr)
            );
    printk(KERN_INFO "DEBUG contextidr %x, cpu %u\n", contextidr, smp_processor_id());
    return 0;
}

static int __init kdbg_init(void) {
    int ret = 0;
    kp.pre_handler = dump_contextidr;
    ret = register_kprobe(&kp);
    if (ret < 0) {
        printk(KERN_INFO "register kprobe failed");
    }
    return ret;
}

static void __exit kdbg_exit(void) {
    unregister_kprobe(&kp);
}

module_init(kdbg_init)
module_exit(kdbg_exit)
MODULE_LICENSE("GPL");
