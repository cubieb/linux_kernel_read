/*
 * Northstar PWM function.
 *
 * Copyright (C) 2013, Broadcom Corporation. All Rights Reserved.
 * 
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $Id:$
 */
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <typedefs.h>
#include <bcmutils.h>

typedef struct pwm_para {
	uint32 ch;
	uint32 prescale;
	uint32 periodCnt;
	uint32 dutyHiCnt;
	uint32 outputEnable;
} pwm_para_t;

#define PWM_PARA_NUM ((sizeof(pwm_para_t)) / (sizeof(uint32)))

#define PWM_CHANNEL_NUM				(4)
#define PWM_PRESCALE_MASK			(0x3F)
#define PWM_PERIOD_MASK				(0xFFFF)
#define PWM_DUTYHI_MASK				(0xFFFF)

#define DMU_CRU_GPIO_CTRL0			(0x1800C1C0)
#define CCB_GPIO_AUX_SEL			(0x18001028)

#define PWM_BASE					(0x18002000)
/* pwm reg offset */
#define PWM_CTL						(0x0)
#define PWM_CH_PERIOD_CNT(channel)	(0x4 + (channel * 8))
#define PWM_CH_DUTYHI_CNT(channel)	(0x8 + (channel * 8))
#define PWM_PRESCALE				(0x24)

static bool
pwm_para_is_valid(pwm_para_t *pwm)
{
	bool valid = FALSE;

	/* total 4 pwm channels  */
	if (pwm->ch > (PWM_CHANNEL_NUM - 1))
		goto out;

	if (pwm->prescale & ~PWM_PRESCALE_MASK)
		goto out;

	if (pwm->periodCnt & ~PWM_PERIOD_MASK)
		goto out;

	if (pwm->dutyHiCnt & ~PWM_DUTYHI_MASK)
		goto out;

	valid = 1;
out:
	return valid;
}

/*
 * frequency = pwm input clock 1M/((prescale + 1) x periodCnt) 
 * dutyCycle = dutyHiCnt/periodCnt
 */
static void
pwm_ch_config(pwm_para_t *pwm)
{
	void __iomem *pwm_base;
	uint32 val32;

	pwm_base = REG_MAP(PWM_BASE, 0x1000);

	/* set prescale */
	val32 = readl(pwm_base + PWM_PRESCALE);
	val32 &= ~(PWM_PRESCALE_MASK << (18 - (pwm->ch * 6)));
	val32 |= (pwm->prescale << (18 - (pwm->ch * 6)));
	writel(val32, pwm_base + PWM_PRESCALE);

	/* set periodCnt */
	val32 = readl(pwm_base + PWM_CH_PERIOD_CNT(pwm->ch));
	val32 = pwm->periodCnt;
	writel(val32, pwm_base + PWM_CH_PERIOD_CNT(pwm->ch));

	/* set dutyHiCnt */
	val32 = readl(pwm_base + PWM_CH_DUTYHI_CNT(pwm->ch));
	val32 = pwm->dutyHiCnt;
	writel(val32, pwm_base + PWM_CH_DUTYHI_CNT(pwm->ch));

	REG_UNMAP(pwm_base);
}

/* enable/disable to output on specific pwm channel */
static void
pwm_ch_output(uint32 channel, bool on)
{
	void __iomem *pwm_base;
	uint32 val32;

	pwm_base = REG_MAP(PWM_BASE, 0x1000);

	/* enable/disable pwm output on channel 0~3 */
	val32 = readl(pwm_base + PWM_CTL);
	val32 &= ~(1 << channel);
	writel(val32, pwm_base + PWM_CTL);
	if (on) {
		udelay(10);
		val32 |= (1 << channel);
		writel(val32, pwm_base + PWM_CTL);
	}

	REG_UNMAP(pwm_base);
}

static void
pwm_ch_status(uint32 channel)
{
	void __iomem *dmu_cru_gpio_control0, *ccb_gpio_aux_sel, *pwm_base;
	uint32 dmu_cru_gpio_control0_val, ccb_gpio_aux_sel_val, val32;
	pwm_para_t pwm;

	dmu_cru_gpio_control0 = REG_MAP(DMU_CRU_GPIO_CTRL0, sizeof(uint32));
	dmu_cru_gpio_control0_val = readl(dmu_cru_gpio_control0);
	REG_UNMAP(dmu_cru_gpio_control0);

	ccb_gpio_aux_sel = REG_MAP(CCB_GPIO_AUX_SEL, sizeof(uint32));
	ccb_gpio_aux_sel_val = readl(ccb_gpio_aux_sel);
	REG_UNMAP(ccb_gpio_aux_sel);

	pwm_base = REG_MAP(PWM_BASE, 0x1000);
	/* channel */
	pwm.ch = channel;

	/* prescale */
	val32 = readl(pwm_base + PWM_PRESCALE);
	pwm.prescale = (val32 >> (18 - (pwm.ch * 6))) & PWM_PRESCALE_MASK;

	/* periodCnt */
	val32 = readl(pwm_base + PWM_CH_PERIOD_CNT(pwm.ch));
	pwm.periodCnt = val32 & PWM_PERIOD_MASK;

	/* dutyHiCnt */
	val32 = readl(pwm_base + PWM_CH_DUTYHI_CNT(pwm.ch));
	pwm.dutyHiCnt = val32 & PWM_DUTYHI_MASK;

	/* outputEnable */
	val32 = readl(pwm_base + PWM_CTL);
	pwm.outputEnable = (val32 & (1 << pwm.ch)) ? TRUE : FALSE;
	REG_UNMAP(pwm_base);

	printk("GPIO PIN MUX:\n");
	printk("dmu_cru_gpio_control0: \tPWM channel %d func %s\n", pwm.ch,
		(dmu_cru_gpio_control0_val & (1 << (8 + pwm.ch))) ? "off" : "on");
	printk("ccb_gpio_aux_sel: \tPWM channel %d func %s\n", pwm.ch,
		(ccb_gpio_aux_sel_val & (1 << pwm.ch)) ? "on" : "off");
	printk("\nPWM channel %d config:\n", pwm.ch);
	printk("prescale: \t%d\n", pwm.prescale);
	printk("periodCnt: \t%d\n", pwm.periodCnt);
	printk("dutyHiCnt: \t%d\n", pwm.dutyHiCnt);
	printk("outputEnable: \t%s\n\n", pwm.outputEnable ? "Enabled" : "Disabled");
}

static char
*get_token(char **buffer)
{
	char *p = NULL;

	while ((p = strsep(buffer, " ")) != NULL) {
		if (!*p || p[0] == ' ' || p[0] == '\0') {
			continue;
		}
		break;
	}

	return p;
}

/* enable/disable PWM function by setting GPIO MUX */
static int
pwm_enable_write_proc(struct file *file, const char __user *buf,
	unsigned long count, void *data)
{
	char *buffer, *tmp, *p;
	uint32 tokens, channel;
	bool pwmEnable;
	void __iomem *dmu_cru_gpio_control0, *ccb_gpio_aux_sel;
	uint32 val32;

	buffer = kmalloc(count + 1, GFP_KERNEL);
	if (!buffer) {
		printk("%s: kmalloc failed.\n", __FUNCTION__);
		goto out;
	}

	if (copy_from_user(buffer, buf, count)) {
		printk("%s: copy_from_user failed.\n", __FUNCTION__);
		goto out;
	}

	buffer[count] = '\0';
	tmp = buffer;
	tokens = 0;

	if (p = get_token((char **)&tmp)) {
		channel = simple_strtoul(p, NULL, 0);
		tokens++;
	}

	/* check channel */
	if (channel > (PWM_CHANNEL_NUM - 1))
		goto out;

	if (p = get_token((char **)&tmp)) {
		pwmEnable = (simple_strtoul(p, NULL, 0)) ? TRUE : FALSE;
		tokens++;
	}

	if (tokens != 2)
		goto out;

	/* Enable PWM ch 0~3 by clearing bit 8~11 in dmu_cru_gpio_control0 and
	 * setting bit 0~3 in ccb_gpio_aux_sel
	 */
	dmu_cru_gpio_control0 = REG_MAP(DMU_CRU_GPIO_CTRL0, sizeof(uint32));
	val32 = readl(dmu_cru_gpio_control0);
	if (pwmEnable)
		val32 &= ~(1 << (8 + channel));
	else
		val32 |= (1 << (8 + channel));
	writel(val32, dmu_cru_gpio_control0);
	REG_UNMAP(dmu_cru_gpio_control0);

	ccb_gpio_aux_sel = REG_MAP(CCB_GPIO_AUX_SEL, sizeof(uint32));
	val32 = readl(ccb_gpio_aux_sel);
	if (pwmEnable)
		val32 |= (1 << channel);
	else
		val32 &= ~(1 << channel);
	writel(val32, ccb_gpio_aux_sel);
	REG_UNMAP(ccb_gpio_aux_sel);

out:
	if (buffer)
		kfree(buffer);

	return count;	
}

static int
pwm_config_write_proc(struct file *file, const char __user *buf,
	unsigned long count, void *data)
{
	char *buffer, *tmp, *p;
	uint32 tokens;
	uint32 *params;
	pwm_para_t pwm;
	int i;

	buffer = kmalloc(count + 1, GFP_KERNEL);
	if (!buffer) {
		printk("%s: kmalloc failed.\n", __FUNCTION__);
		goto out;
	}

	if (copy_from_user(buffer, buf, count)) {
		printk("%s: copy_from_user failed.\n", __FUNCTION__);
		goto out;
	}

	buffer[count] = '\0';
	tmp = buffer;
	tokens = 0;
	params = (uint32 *)&pwm;

	/* get pwm para */
	for (i = 0; i < (PWM_PARA_NUM - 1); i++) {
		if (p = get_token((char **)&tmp)) {
			*params = (uint32)simple_strtoul(p, NULL, 0);
			params++;
			tokens++;
		}
	}

	/* check pwm para */
	if (tokens != (PWM_PARA_NUM - 1) || !pwm_para_is_valid(&pwm))
		goto out;

	/* pwm config */
	pwm_ch_config(&pwm);

out:
	if (buffer)
		kfree(buffer);

	return count;
}

static int
pwm_output_write_proc(struct file *file, const char __user *buf,
	unsigned long count, void *data)
{
	char *buffer, *tmp, *p;
	uint32 tokens, channel;
	bool outputEnable;

	buffer = kmalloc(count + 1, GFP_KERNEL);
	if (!buffer) {
		printk("%s: kmalloc failed.\n", __FUNCTION__);
		goto out;
	}

	if (copy_from_user(buffer, buf, count)) {
		printk("%s: copy_from_user failed.\n", __FUNCTION__);
		goto out;
	}

	buffer[count] = '\0';
	tmp = buffer;
	tokens = 0;

	if (p = get_token((char **)&tmp)) {
		channel = simple_strtoul(p, NULL, 0);
		tokens++;
	}

	/* check channel */
	if (channel > (PWM_CHANNEL_NUM - 1))
		goto out;

	if (p = get_token((char **)&tmp)) {
		outputEnable = (simple_strtoul(p, NULL, 0)) ? TRUE : FALSE;
		tokens++;
	}

	if (tokens != 2)
		goto out;

	pwm_ch_output(channel, outputEnable);

out:
	if (buffer)
		kfree(buffer);

	return count;
}

static int
pwm_status_read_proc(char * buffer, char **start,
	off_t offset, int length, int * eof, void * data)
{
	int i;

	for (i = 0; i < PWM_CHANNEL_NUM; i++)
		pwm_ch_status(i);

	*eof = 1;

	return 0;
}

static void __init
pwm_proc_init(void)
{
	struct proc_dir_entry *pwm_proc_dir, *pwm_enable, *pwm_config, *pwm_status, *pwm_output;

	pwm_proc_dir = proc_mkdir("pwm", NULL);
	if (!pwm_proc_dir) {
		printk(KERN_ERR "%s: Create proc directory failed.\n", __FUNCTION__);
		return;
	}

	pwm_enable = create_proc_entry("pwm/enable", 0, NULL);
	if (!pwm_enable) {
		printk(KERN_ERR "%s: Create proc entry failed.\n", __FUNCTION__);
		return;
	}
	pwm_enable->write_proc = pwm_enable_write_proc;

	pwm_config = create_proc_entry("pwm/config", 0, NULL);
	if (!pwm_config) {
		printk(KERN_ERR "%s: Create proc entry failed.\n", __FUNCTION__);
		return;
	}
	pwm_config->write_proc = pwm_config_write_proc;

	pwm_output = create_proc_entry("pwm/output", 0, NULL);
	if (!pwm_output) {
		printk(KERN_ERR "%s: Create proc entry failed.\n", __FUNCTION__);
		return;
	}
	pwm_output->write_proc = pwm_output_write_proc;

	pwm_status = create_proc_entry("pwm/status", 0, NULL);
	if (!pwm_status) {
		printk(KERN_ERR "%s: Create proc entry failed.\n", __FUNCTION__);
		return;
	}
	pwm_status->read_proc = pwm_status_read_proc;
}
fs_initcall(pwm_proc_init);
