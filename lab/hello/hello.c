#include <linux/kernel.h>
#include <linux/module.h>

static int __init omo_hello_init(void)
{
	pr_info("omo-hello: loaded on the vendor 5.10.201 kernel\n");
	return 0;
}

static void __exit omo_hello_exit(void)
{
	pr_info("omo-hello: unloaded\n");
}

module_init(omo_hello_init);
module_exit(omo_hello_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("WR3000 V2.0 module load test");
