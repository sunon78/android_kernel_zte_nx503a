/* linux/arch/arm/mach-msm/board-bcm-wifi.c
*/
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <asm/mach-types.h>
#include <asm/gpio.h>
#include <asm/io.h>
#include <linux/skbuff.h>
#include <linux/wlan_plat.h>
#include <linux/partialresume.h>
#include <linux/if.h> /*For IFHWADDRLEN */

#include <linux/gpio.h>

#define BCM_GPIO_WIFI_SHUTDOWN_N	41
#define BCM_GPIO_WIFI_IRQ		42

//#define GET_WIFI_MAC_ADDR_FROM_NV_ITEM	1

int bcm_wifi_power(int on);
int bcm_wifi_reset(int on);
int bcm_wifi_set_carddetect(int on);

#define WLAN_STATIC_SCAN_BUF0		5
#define WLAN_STATIC_SCAN_BUF1		6
#define WLAN_STATIC_DHD_INFO_BUF	7
#define WLAN_SCAN_BUF_SIZE		(64 * 1024)
#define WLAN_DHD_INFO_BUF_SIZE	(16 * 1024)
#define PREALLOC_WLAN_SEC_NUM		4
#define PREALLOC_WLAN_BUF_NUM		160
#define PREALLOC_WLAN_SECTION_HEADER	24

#define WLAN_SECTION_SIZE_0	(PREALLOC_WLAN_BUF_NUM * 128)
#define WLAN_SECTION_SIZE_1	(PREALLOC_WLAN_BUF_NUM * 128)
#define WLAN_SECTION_SIZE_2	(PREALLOC_WLAN_BUF_NUM * 512)
#define WLAN_SECTION_SIZE_3	(PREALLOC_WLAN_BUF_NUM * 1024)

#define DHD_SKB_HDRSIZE			336
#define DHD_SKB_1PAGE_BUFSIZE	((PAGE_SIZE*1)-DHD_SKB_HDRSIZE)
#define DHD_SKB_2PAGE_BUFSIZE	((PAGE_SIZE*2)-DHD_SKB_HDRSIZE)
#define DHD_SKB_4PAGE_BUFSIZE	((PAGE_SIZE*4)-DHD_SKB_HDRSIZE)

#define WLAN_SKB_BUF_NUM	17

static struct sk_buff *wlan_static_skb[WLAN_SKB_BUF_NUM];

struct wlan_mem_prealloc {
	void *mem_ptr;
	unsigned long size;
};

static struct wlan_mem_prealloc wlan_mem_array[PREALLOC_WLAN_SEC_NUM] = {
	{NULL, (WLAN_SECTION_SIZE_0 + PREALLOC_WLAN_SECTION_HEADER)},
	{NULL, (WLAN_SECTION_SIZE_1 + PREALLOC_WLAN_SECTION_HEADER)},
	{NULL, (WLAN_SECTION_SIZE_2 + PREALLOC_WLAN_SECTION_HEADER)},
	{NULL, (WLAN_SECTION_SIZE_3 + PREALLOC_WLAN_SECTION_HEADER)}
};

void *wlan_static_scan_buf0;
void *wlan_static_scan_buf1;
void *wlan_static_dhd_info_buf;

static void *bcm_wifi_mem_prealloc(int section, unsigned long size)
{
	if (section == PREALLOC_WLAN_SEC_NUM)
		return wlan_static_skb;

	if (section == WLAN_STATIC_SCAN_BUF0)
		return wlan_static_scan_buf0;

	if (section == WLAN_STATIC_SCAN_BUF1)
		return wlan_static_scan_buf1;

	if (section == WLAN_STATIC_DHD_INFO_BUF) {
		if (size > WLAN_DHD_INFO_BUF_SIZE) {
			pr_err("request DHD_INFO size(%lu) is bigger than static size(%d).\n", size, WLAN_DHD_INFO_BUF_SIZE);
			return NULL;
		}
		return wlan_static_dhd_info_buf;
	}

	if ((section < 0) || (section > PREALLOC_WLAN_SEC_NUM))
		return NULL;

	if (wlan_mem_array[section].size < size)
		return NULL;

	return wlan_mem_array[section].mem_ptr;
}

int __init bcm_init_wifi_mem(void)
{
	int i;
	int j;

	for (i = 0; i < 8; i++) {
		wlan_static_skb[i] = dev_alloc_skb(DHD_SKB_1PAGE_BUFSIZE);
		if (!wlan_static_skb[i])
			goto err_skb_alloc;
	}

	for (; i < 16; i++) {
		wlan_static_skb[i] = dev_alloc_skb(DHD_SKB_2PAGE_BUFSIZE);
		if (!wlan_static_skb[i])
			goto err_skb_alloc;
	}

	wlan_static_skb[i] = dev_alloc_skb(DHD_SKB_4PAGE_BUFSIZE);
	if (!wlan_static_skb[i])
		goto err_skb_alloc;

	for (i = 0 ; i < PREALLOC_WLAN_SEC_NUM ; i++) {
		wlan_mem_array[i].mem_ptr =
				kmalloc(wlan_mem_array[i].size, GFP_KERNEL);

		if (!wlan_mem_array[i].mem_ptr)
			goto err_mem_alloc;
	}

	wlan_static_scan_buf0 = kmalloc(WLAN_SCAN_BUF_SIZE, GFP_KERNEL);
	if (!wlan_static_scan_buf0)
		goto err_mem_alloc;

	wlan_static_scan_buf1 = kmalloc(WLAN_SCAN_BUF_SIZE, GFP_KERNEL);
	if (!wlan_static_scan_buf1)
		goto err_mem_alloc;

	wlan_static_dhd_info_buf = kmalloc(WLAN_DHD_INFO_BUF_SIZE, GFP_KERNEL);
	if (!wlan_static_dhd_info_buf)
		goto err_mem_alloc;

	printk(KERN_INFO"%s: WIFI MEM Allocated\n", __func__);
	return 0;

 err_mem_alloc:
	pr_err("Failed to mem_alloc for WLAN\n");
	for (j = 0 ; j < i ; j++)
		kfree(wlan_mem_array[j].mem_ptr);

	i = WLAN_SKB_BUF_NUM;

 err_skb_alloc:
	pr_err("Failed to skb_alloc for WLAN\n");
	for (j = 0 ; j < i ; j++)
		dev_kfree_skb(wlan_static_skb[j]);

	return -ENOMEM;
}

static int bcm_wifi_cd = 0; /* WIFI virtual 'card detect' status */
static void (*wifi_status_cb)(int card_present, void *dev_id);
static void *wifi_status_cb_devid;
static int detect_flag = 0;

void set_detect_flag(void)
{
		detect_flag = 1;
		printk("Broadcom wifi enter %s, flag = %d \n", __func__, detect_flag);
}

void clear_detect_flag(void)
{
		detect_flag = 0;
		printk("Broadcom wifi enter %s, flag = %d \n", __func__, detect_flag);
}
EXPORT_SYMBOL(clear_detect_flag);

int check_detect_flag(void)
{
		printk("Broadcom wifi enter %s, flag = %d \n", __func__, detect_flag);
		return detect_flag;
}
EXPORT_SYMBOL(check_detect_flag);


int bcm_wifi_status_register(
			void (*callback)(int card_present, void *dev_id),
			void *dev_id)
{
	if (wifi_status_cb)
		return -EAGAIN;
	wifi_status_cb = callback;
	wifi_status_cb_devid = dev_id;
	return 0;
}

EXPORT_SYMBOL(bcm_wifi_status_register);
unsigned int bcm_wifi_status(struct device *dev)
{
	return bcm_wifi_cd;
}


int bcm_wifi_set_carddetect(int val)
{
	pr_info("%s: %d\n", __func__, val);
	bcm_wifi_cd = val;
	if (wifi_status_cb) {
		set_detect_flag();
		wifi_status_cb(val, wifi_status_cb_devid);
	} else
		pr_warning("%s: Nobody to notify\n", __func__);
	return 0;
}


int bcm_wifi_power(int on)
{
	printk("%s:  Broadcom wifi power %d \n", __func__, on);

	if(on) {
		if((gpio_request(BCM_GPIO_WIFI_SHUTDOWN_N,"Broadcom wifi")) < 0) {
			printk("request fail \n");
		} else {
			printk("Broadcom wifi request reg_on  success \n");
			gpio_direction_output(BCM_GPIO_WIFI_SHUTDOWN_N, 0);
			mdelay(10);
			gpio_direction_output(BCM_GPIO_WIFI_SHUTDOWN_N, on);
			mdelay(200);
		}
	} else {
			gpio_direction_output(BCM_GPIO_WIFI_SHUTDOWN_N, 0);
			gpio_free(BCM_GPIO_WIFI_SHUTDOWN_N);
	}

	printk("Broadcom wifi gpio_set_value = %d \n", 
		gpio_get_value(BCM_GPIO_WIFI_SHUTDOWN_N));

	return 0;
}


int bcm_wifi_reset(int on)
{
	printk("%s: enter\n", __func__);
	bcm_wifi_power(on);
	return 0;
}

#ifdef GET_WIFI_MAC_ADDR_FROM_NV_ITEM
static unsigned char bcm_mac_addr[IFHWADDRLEN] = { 0x11,0x22,0x33,0x44,0x55,0x66 };

static int bcm_wifi_get_mac_addr(unsigned char *buf)
{
	int rc = 0;
	unsigned int data1, data2;

	if (!buf){
		printk("%s, null parameter !!\n", __func__);	
		return -EFAULT;
	}

    	data2 = (1<<31);//for wifi mac   
	rc = msm_proc_comm(PCOM_CUSTOMER_CMD1, &data1, &data2);
	printk("================zbs, rc=%d\n", rc);
	if(!rc) {
		bcm_mac_addr[5] = (unsigned char)((data2>>8)&0xff); 
		bcm_mac_addr[4] = (unsigned char)(data2&0xff); 
 		bcm_mac_addr[3] = (unsigned char)((data1>>24)&0xff); 
		bcm_mac_addr[2] = (unsigned char)((data1>>16)&0xff); 
		bcm_mac_addr[1] = (unsigned char)((data1>>8)&0xff); 
		bcm_mac_addr[0] = (unsigned char)(data1&0xff); 

		memcpy(buf, bcm_mac_addr, IFHWADDRLEN);
		printk("wifi mac: %x:%x:%x:%x:%x:%x\n", bcm_mac_addr[0], bcm_mac_addr[1], bcm_mac_addr[2], bcm_mac_addr[3], bcm_mac_addr[4], bcm_mac_addr[5]);
				
		return 0;
	} else {
 		printk("%s fail !!\n", __func__);		
		return -EFAULT;
	}		
}
#endif

static struct resource bcm_wifi_resources[] = {
	[0] = {
		.name		= "bcmdhd_wlan_irq",
		//.start		= MSM_GPIO_TO_INT(BCM_GPIO_WIFI_IRQ),
		//.end		= MSM_GPIO_TO_INT(BCM_GPIO_WIFI_IRQ),
		.flags          = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL | IORESOURCE_IRQ_SHAREABLE,
		//.flags          = IRQF_TRIGGER_FALLING,

	},
};

#ifdef CONFIG_PARTIALRESUME
static bool smd_partial_resume(struct partial_resume *pr)
{
	return true;
}

#define PR_INIT_STATE		0
#define PR_IN_RESUME_STATE	1
#define PR_RESUME_OK_STATE	2
#define PR_SUSPEND_OK_STATE	3

static DECLARE_COMPLETION(bcm_pk_comp);
static DECLARE_COMPLETION(bcm_wd_comp);
static int bcm_suspend = PR_INIT_STATE;
static spinlock_t bcm_lock;

/*
 * Partial Resume State Machine:
    _______
   / else [INIT]________________________
   \______/   | notify_resume           \
           [IN_RESUME]              wait_for_ready
           /       \ vote_for_suspend   /
   vote_for_resume [SUSPEND_OK]________/
           \       / vote_for_resume  /
          [RESUME_OK]                /
                   \________________/
 */

static bool bcm_wifi_process_partial_resume(int action)
{
	bool suspend = false;
	int timeout = 0;

	if ((action != WIFI_PR_NOTIFY_RESUME) && (bcm_suspend == PR_INIT_STATE))
		return suspend;

	if (action == WIFI_PR_WAIT_FOR_READY)
		timeout = wait_for_completion_timeout(&bcm_pk_comp,
						      msecs_to_jiffies(50));

	spin_lock(&bcm_lock);
	switch (action) {
	case WIFI_PR_WAIT_FOR_READY:
		suspend = (bcm_suspend == PR_SUSPEND_OK_STATE) && (timeout != 0);
		if (suspend) {
			spin_unlock(&bcm_lock);
			timeout = wait_for_completion_timeout(&bcm_wd_comp,
							msecs_to_jiffies(100));
			spin_lock(&bcm_lock);
			suspend = (timeout != 0);
		}
		bcm_suspend = PR_INIT_STATE;
		break;
	case WIFI_PR_VOTE_FOR_RESUME:
		bcm_suspend = PR_RESUME_OK_STATE;
		complete(&bcm_pk_comp);
		break;
	case WIFI_PR_VOTE_FOR_SUSPEND:
		if (bcm_suspend == PR_IN_RESUME_STATE)
			bcm_suspend = PR_SUSPEND_OK_STATE;
		complete(&bcm_pk_comp);
		break;
	case WIFI_PR_NOTIFY_RESUME:
		INIT_COMPLETION(bcm_pk_comp);
		bcm_suspend = PR_IN_RESUME_STATE;
		break;
	case WIFI_PR_INIT:
		bcm_suspend = PR_INIT_STATE;
		break;
	case WIFI_PR_WD_INIT:
		INIT_COMPLETION(bcm_wd_comp);
		break;
	case WIFI_PR_WD_COMPLETE:
		complete(&bcm_wd_comp);
		break;
	}
	spin_unlock(&bcm_lock);
	return suspend;
}

bool wlan_vote_for_suspend(void)
{
	return bcm_wifi_process_partial_resume(WIFI_PR_VOTE_FOR_SUSPEND);
}
EXPORT_SYMBOL(wlan_vote_for_suspend);

static bool bcm_wifi_partial_resume(struct partial_resume *pr)
{
	bool suspend;

	suspend = bcm_wifi_process_partial_resume(WIFI_PR_WAIT_FOR_READY);
	pr_info("%s: vote %d\n", __func__, suspend);
	return suspend;
}
#endif

static struct wifi_platform_data bcm_wifi_control = {
	.set_power      = bcm_wifi_power,
	.set_reset      = bcm_wifi_reset,
	.set_carddetect = bcm_wifi_set_carddetect,
	.mem_prealloc	= bcm_wifi_mem_prealloc,
#ifdef GET_WIFI_MAC_ADDR_FROM_NV_ITEM	
	.get_mac_addr	= bcm_wifi_get_mac_addr,
#endif 	
#ifdef CONFIG_PARTIALRESUME
	.partial_resume = bcm_wifi_process_partial_resume,
#endif
};

static struct platform_device bcm_wifi_device = {
        .name           = "bcmdhd_wlan",
        .id             = 1,
        .num_resources  = ARRAY_SIZE(bcm_wifi_resources),
        .resource       = bcm_wifi_resources,
        .dev            = {
                .platform_data = &bcm_wifi_control,
        },
};

int __init bcm_wifi_init(void)
{
	int ret;

	pr_err("++++%s++++ \n", __func__);
	gpio_request(BCM_GPIO_WIFI_IRQ, "oob_irq");
	gpio_direction_input(BCM_GPIO_WIFI_IRQ);

	bcm_wifi_resources[0].start = gpio_to_irq(BCM_GPIO_WIFI_IRQ);
	bcm_wifi_resources[0].end = gpio_to_irq(BCM_GPIO_WIFI_IRQ);
	gpio_set_value(BCM_GPIO_WIFI_SHUTDOWN_N, 0);
	bcm_init_wifi_mem();
	ret = platform_device_register(&bcm_wifi_device);
	pr_err("----%s----\n", __func__);
        return ret;
}

#ifdef CONFIG_PARTIALRESUME
static struct partial_resume smd_pr = {
	.irq = 200,
	.partial_resume = smd_partial_resume,
};

static struct partial_resume mpm_pr = {
	.irq = 203,
	.partial_resume = smd_partial_resume,
};

static struct partial_resume wlan_pr = {
	.partial_resume = bcm_wifi_partial_resume,
};

int __init wlan_partial_resume_init(void)
{
	int rc;

	spin_lock_init(&bcm_lock); /* Setup partial resume */
	complete(&bcm_wd_comp);    /* Prepare for case when WD is not set */
	wlan_pr.irq = bcm_wifi_device.resource->start;
	rc = register_partial_resume(&wlan_pr);
	pr_debug("%s: after registering %pF: %d\n", __func__,
		 wlan_pr.partial_resume, rc);
	rc = register_partial_resume(&mpm_pr);
	rc = register_partial_resume(&smd_pr);
	pr_debug("%s: after registering %pF: %d\n", __func__,
		 smd_pr.partial_resume, rc);
	return rc;
}

late_initcall(wlan_partial_resume_init);
#endif


//late_initcall(bcm_wifi_init);
