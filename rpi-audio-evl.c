// SPDX-License-Identifier: GPL-2.0
/*
 * @brief Initial version of real-time audio driver for rpi
 * @copyright 2017-2019 Modern Ancient Instruments Networked AB, dba Elk,
 * Stockholm
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>
#include <linux/device.h>

/* EVL headers */
#include <evl/file.h>
#include <evl/flag.h>
#include <evl/clock.h>
#include <evl/thread.h>
#include <evl/uaccess.h>

#include "rpi-audio-evl.h"
#include "elk-pi-config.h"
#include "hifi-berry-config.h"
#include "hifi-berry-pro-config.h"
#include "pcm3168a-elk.h"
#include "pcm5122-elk.h"
#include "pcm1863-elk.h"
#include "bcm2835-i2s-elk.h"

MODULE_AUTHOR("Nitin Kulkarni (nitin@elk.audio)");
MODULE_AUTHOR("Marco Del Fiasco (marco@elk.audio)");
MODULE_DESCRIPTION("EVL audio driver for RPi");
MODULE_LICENSE("GPL");

#define DEFAULT_AUDIO_SAMPLING_RATE			48000
#define DEFAULT_AUDIO_NUM_INPUT_CHANNELS		8
#define DEFAULT_AUDIO_NUM_OUTPUT_CHANNELS		8
#define DEFAULT_AUDIO_NUM_CODEC_CHANNELS		8
#define DEFAULT_AUDIO_N_FRAMES_PER_BUFFER		64
#define DEFAULT_AUDIO_CODEC_FORMAT			INT24_LJ
#define DEFAULT_AUDIO_LOW_LATENCY_VAL			1
#define PLATFORM_TYPE					NATIVE_AUDIO
#define USB_AUDIO_TYPE			NONE
#define SUPPORTED_BUFFER_SIZES 16, 32, 64, 128

static uint audio_ver_maj = AUDIO_EVL_VERSION_MAJ;
static uint audio_ver_min = AUDIO_EVL_VERSION_MIN;
static uint audio_ver_rev = AUDIO_EVL_VERSION_VER;
static uint audio_input_channels = DEFAULT_AUDIO_NUM_INPUT_CHANNELS;
static uint audio_output_channels = DEFAULT_AUDIO_NUM_OUTPUT_CHANNELS;
static uint audio_sampling_rate = DEFAULT_AUDIO_SAMPLING_RATE;
static uint platform_type = PLATFORM_TYPE;
static const uint usb_audio_type = USB_AUDIO_TYPE;

static uint audio_buffer_size = DEFAULT_AUDIO_N_FRAMES_PER_BUFFER;
module_param(audio_buffer_size, uint, 0644);
static char *audio_hat = "elk-pi";
module_param(audio_hat, charp, 0644);
static uint audio_enable_low_latency = DEFAULT_AUDIO_LOW_LATENCY_VAL;
module_param(audio_enable_low_latency, uint, 0644);
static int session_under_runs = 0;
module_param(session_under_runs, int, 0644);
static uint kernel_interrupts = 0;
module_param(kernel_interrupts, uint, 0444);

static const int supported_buffer_sizes[] = {SUPPORTED_BUFFER_SIZES};
static uint num_codec_channels = DEFAULT_AUDIO_NUM_CODEC_CHANNELS;
static uint audio_format = DEFAULT_AUDIO_CODEC_FORMAT;
static unsigned long user_proc_completions = 0;

struct audio_dev_context {
	struct audio_evl_dev *i2s_dev;
	struct audio_channel_info_data* audio_input_info;
	struct audio_channel_info_data* audio_output_info;
	struct evl_file	efile;
	uint64_t user_proc_calls;
};

static ssize_t audio_buffer_size_show(struct class *cls,
                                      struct class_attribute *attr, char *buf) {
  return sprintf(buf, "%d\n", audio_buffer_size);
}

static ssize_t audio_buffer_size_store(struct class *class,
		struct class_attribute *attr, const char *buf, size_t size)
{
	unsigned long bs;
	ssize_t result;
	result = sscanf(buf, "%lu", &bs);
	if (result != 1)
		return -EINVAL;
	audio_buffer_size = bs;
	return size;
}

static ssize_t audio_hat_show(struct class *cls, struct class_attribute *attr,
                              char *buf) {
  return sprintf(buf, "%s\n", audio_hat);
}

static ssize_t audio_sampling_rate_show(struct class *cls,
                                        struct class_attribute *attr,
                                        char *buf) {
  return sprintf(buf, "%du\n", audio_sampling_rate);
}

static ssize_t audio_ver_maj_show(struct class *cls,
                                       struct class_attribute *attr,
                                       char *buf) {
  return sprintf(buf, "%d\n", audio_ver_maj);
}

static ssize_t audio_ver_min_show(struct class *cls,
                                       struct class_attribute *attr,
                                       char *buf) {
  return sprintf(buf, "%d\n", audio_ver_min);
}

static ssize_t audio_ver_rev_show(struct class *cls,
                                       struct class_attribute *attr,
                                       char *buf) {
  return sprintf(buf, "%d\n", audio_ver_rev);
}

static ssize_t audio_input_channels_show(struct class *cls,
                                         struct class_attribute *attr,
                                         char *buf) {
  return sprintf(buf, "%d\n", audio_input_channels);
}

static ssize_t audio_output_channels_show(struct class *cls,
                                          struct class_attribute *attr,
                                          char *buf) {
  return sprintf(buf, "%d\n", audio_output_channels);
}

static ssize_t platform_type_show(struct class *cls,
                                  struct class_attribute *attr, char *buf) {
  return sprintf(buf, "%d\n", platform_type);
}

static ssize_t usb_audio_type_show(struct class *cls,
				struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", usb_audio_type);
}

static CLASS_ATTR_RW(audio_buffer_size);
static CLASS_ATTR_RO(audio_hat);
static CLASS_ATTR_RO(audio_sampling_rate);
static CLASS_ATTR_RO(audio_ver_maj);
static CLASS_ATTR_RO(audio_ver_min);
static CLASS_ATTR_RO(audio_ver_rev);
static CLASS_ATTR_RO(audio_input_channels);
static CLASS_ATTR_RO(audio_output_channels);
static CLASS_ATTR_RO(platform_type);
static CLASS_ATTR_RO(usb_audio_type);

static struct attribute *audio_evl_class_attrs[] = {
	&class_attr_audio_buffer_size.attr,
	&class_attr_audio_hat.attr,
	&class_attr_audio_sampling_rate.attr,
	&class_attr_audio_ver_maj.attr,
	&class_attr_audio_ver_min.attr,
	&class_attr_audio_ver_rev.attr,
	&class_attr_audio_input_channels.attr,
	&class_attr_audio_output_channels.attr,
	&class_attr_platform_type.attr,
	&class_attr_usb_audio_type.attr,
	NULL,
};
ATTRIBUTE_GROUPS(audio_evl_class);

struct class audio_evl_class = {
    .name = "audio_evl",
    .class_groups = audio_evl_class_groups,
};

static int audio_driver_open(struct inode *inode, struct file *filp)
{
	int ret = 0;
	struct audio_dev_context *dev_context;
	int chan_num;

	dev_context = kzalloc(sizeof(*dev_context), GFP_KERNEL);
	if (dev_context == NULL)
		return -ENOMEM;

	dev_context->audio_input_info = kcalloc(audio_input_channels,
				sizeof(struct audio_channel_info_data), GFP_KERNEL);
	if (!dev_context->audio_input_info) {
		printk(KERN_ERR "audio_evl: Failed to allocate input chan info\n");
		ret = -ENOMEM;
		goto fail_in_ch;
	}

	dev_context->audio_output_info = kcalloc(audio_output_channels,
				sizeof(struct audio_channel_info_data), GFP_KERNEL);	
	if (!dev_context->audio_output_info) {
		printk(KERN_ERR "audio_evl: Failed to allocate output chan info\n");
		ret = -ENOMEM;
		goto fail_out_ch;
	}

	// fill dummy chan info
	for (chan_num = 0; chan_num < audio_input_channels; chan_num++) {
		struct audio_channel_info_data *audio_input_info =
			&dev_context->audio_input_info[chan_num];

		audio_input_info->sw_ch_id = chan_num;
		audio_input_info->hw_ch_id = chan_num;
		audio_input_info->direction = INPUT_DIRECTION;
		audio_input_info->sample_format = audio_format;
		snprintf((char *)audio_input_info->channel_name,
				AUDIO_CHANNEL_NAME_SIZE - 1,
				"IN-%d",
				chan_num);
		audio_input_info->start_offset_in_words = chan_num;
		audio_input_info->stride_in_words = num_codec_channels;
	}

	for (chan_num = 0; chan_num < audio_output_channels; chan_num++) {
		struct audio_channel_info_data *audio_output_info =
			&dev_context->audio_output_info[chan_num];

		audio_output_info->sw_ch_id = chan_num;
		audio_output_info->hw_ch_id = chan_num;
		audio_output_info->direction = OUTPUT_DIRECTION;
		audio_output_info->sample_format = audio_format;
		snprintf((char *)audio_output_info->channel_name,
				AUDIO_CHANNEL_NAME_SIZE - 1,
				"IN-%d",
				chan_num);
		audio_output_info->start_offset_in_words = chan_num;
		audio_output_info->stride_in_words = num_codec_channels;
	}

	ret = evl_open_file(&dev_context->efile, filp);
	if (ret) {
		goto fail_evl_open_file;
	}
	filp->private_data = dev_context;
	stream_open(inode, filp);

	dev_context->i2s_dev = bcm2835_get_i2s_dev();
	dev_context->i2s_dev->wait_flag = 0;
	dev_context->i2s_dev->kinterrupts = 0;
	dev_context->i2s_dev->buffer_idx = 0;
	evl_init_flag(&dev_context->i2s_dev->event_flag);

	bcm2835_i2s_buffers_setup(audio_buffer_size, audio_output_channels);

	user_proc_completions = 0;
	kernel_interrupts = 0;
	session_under_runs = 0;

	printk(KERN_INFO "audio_evl: audio_driver_open\n");

	return 0;

fail_evl_open_file:
	kfree(dev_context->audio_output_info);
fail_out_ch:
	kfree(dev_context->audio_input_info);
fail_in_ch:
	kfree(dev_context);

	return ret;
}

static int  audio_driver_release(struct inode *inode, struct file *filp)
{
	int i;
	struct audio_dev_context *dev_context = filp->private_data;
	struct audio_evl_buffers *i2s_buffer = dev_context->i2s_dev->buffer;
	int *tx = i2s_buffer->tx_buf;

	evl_destroy_flag(&dev_context->i2s_dev->event_flag);
	if (dev_context->i2s_dev->wait_flag) {
		for (i = 0; i < i2s_buffer->buffer_len/4; i++) {
			tx[i] = 0;
		}
		dev_context->i2s_dev->wait_flag = 0;
	}
	bcm2835_i2s_exit();

	kfree(dev_context->audio_output_info);
	kfree(dev_context->audio_input_info);
	kfree(dev_context);

	printk(KERN_INFO "audio_evl: audio_driver_release\n");

	return 0;
}

static int audio_driver_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct audio_dev_context *dev_context = filp->private_data;
	struct audio_evl_buffers *i2s_buffer = dev_context->i2s_dev->buffer;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	return dma_mmap_coherent(dev_context->i2s_dev->dma_rx->device->dev,
		vma,
		i2s_buffer->rx_buf, i2s_buffer->rx_phys_addr,
		RESERVED_BUFFER_SIZE_IN_PAGES * PAGE_SIZE);
}

static long audio_driver_oob_ioctl(struct file *filp, unsigned int cmd,
				   unsigned long arg)
{
	int result = 0;
	int under_runs;
	int buffer_idx;
	struct audio_dev_context *dev_context = filp->private_data;
	struct audio_evl_dev *dev = dev_context->i2s_dev;

	switch (cmd) {
	case AUDIO_IRQ_WAIT:
		result = evl_wait_flag(&dev->event_flag);
		if (result != 0) {
			printk(KERN_ERR "evl_event_wait failed\n");
			return result;
		}
		buffer_idx = dev->buffer_idx ? 0 : 1;
		result = raw_copy_to_user((void __user *)arg, &buffer_idx,
					  sizeof(buffer_idx));
		if (result) {
			return -EFAULT;
 		}
		kernel_interrupts = dev->kinterrupts;
		user_proc_completions = kernel_interrupts;
		return result;
	case AUDIO_USERPROC_FINISHED:
		kernel_interrupts = dev->kinterrupts;
		under_runs = kernel_interrupts - user_proc_completions;
		if (under_runs) {
			session_under_runs += under_runs;
		}
		break;
	default:
		printk(KERN_WARNING "audio_evl : audio_ioctl_rt: invalid value"
							" %d\n", cmd);
		return -EINVAL;
	}
	return result;
}

static long audio_driver_ioctl(struct file *filp, unsigned int cmd,
			 unsigned long arg)
{
	struct audio_dev_context *dev_context = filp->private_data;
	int result = 0;

	switch(cmd) {
	case AUDIO_PROC_START:
		bcm2835_i2s_start_stop(dev_context->i2s_dev, BCM2835_I2S_START_CMD);
		break;
	case AUDIO_PROC_STOP:
		bcm2835_i2s_start_stop(dev_context->i2s_dev, BCM2835_I2S_STOP_CMD);
		break;
	case AUDIO_GET_INPUT_CHAN_INFO:
		if (dev_context->audio_input_info == NULL) {
			return -ENOENT;
		}
		result = raw_copy_to_user((void *)arg, dev_context->audio_input_info,
					sizeof(struct audio_channel_info_data) *
					audio_input_channels);
		if (result < 0) {
			printk(	KERN_INFO
				"audio_evl: AUDIO_GET_INPUT_CHAN_INFO"
				" failed to copy data to user\n");
			return result;
		}
		break;
	case AUDIO_GET_OUTPUT_CHAN_INFO:
		if (dev_context->audio_output_info == NULL) {
			return -ENOENT;
		}
		result = raw_copy_to_user((void *)arg, dev_context->audio_output_info,
					sizeof(struct audio_channel_info_data) *
					audio_output_channels);
		if (result < 0) {
			printk(	KERN_INFO
				"audio_evl: AUDIO_GET_OUTPUT_CHAN_INFO"
				" failed to copy data to user\n");
			return result;
		}
		break;
	default:
		printk(	KERN_WARNING
			"audio_evl : audio_driver_ioctl: invalid value"
			" %d\n", cmd);
		return -EINVAL;
	}

	return result;
}

static const struct file_operations audio_driver_fops = {
	.open		= audio_driver_open,
	.release	= audio_driver_release,
	.unlocked_ioctl	= audio_driver_ioctl,
	.mmap		= audio_driver_mmap,
	.oob_ioctl	= audio_driver_oob_ioctl,
};

static dev_t rt_audio_devt;
static struct cdev rt_audio_cdev;

static int __init audio_evl_driver_init(void)
{
	int ret;
	struct device *dev;

	ret = class_register(&audio_evl_class);
	if (ret)
		return ret;

	if (!strcmp(audio_hat, "hifi-berry")) {
		printk(KERN_INFO "audio_evl: hifi-berry hat\n");
		if (pcm5122_codec_init(HIFI_BERRY_DAC_MODE,
				HIFI_BERRY_SAMPLING_RATE,
				audio_enable_low_latency)) {
			printk(KERN_ERR "audio_evl: codec init failed\n");
			return -1;
		}
		audio_input_channels = HIFI_BERRY_NUM_INPUT_CHANNELS;
		audio_output_channels = HIFI_BERRY_NUM_OUTPUT_CHANNELS;
		num_codec_channels = HIFI_BERRY_NUM_CODEC_CHANNELS;
		audio_format = HIFI_BERRY_CODEC_FORMAT;
		audio_sampling_rate = HIFI_BERRY_SAMPLING_RATE;
	} else if (!strcmp(audio_hat, "hifi-berry-pro")) {
		printk(KERN_INFO "audio_evl: hifi-berry-pro hat\n");
		if (pcm1863_codec_init(audio_enable_low_latency)) {
			printk(KERN_ERR "audio_evl: pcm3168 codec failed\n");
			return -1;
		}
		if (pcm5122_codec_init(HIFI_BERRY_PRO_DAC_MODE,
					HIFI_BERRY_PRO_SAMPLING_RATE,
					audio_enable_low_latency)) {
			printk(KERN_ERR "audio_evl: pcm5122 codec failed\n");
			return -1;
		}
		audio_input_channels = HIFI_BERRY_PRO_NUM_INPUT_CHANNELS;
		audio_output_channels = HIFI_BERRY_PRO_NUM_OUTPUT_CHANNELS;
		num_codec_channels = HIFI_BERRY_PRO_NUM_CODEC_CHANNELS;
		audio_format = HIFI_BERRY_PRO_CODEC_FORMAT;
		audio_sampling_rate = HIFI_BERRY_PRO_SAMPLING_RATE;
	} else if (!strcmp(audio_hat, "elk-pi")) {
		printk(KERN_INFO "audio_evl: elk-pi hat\n");
		if (pcm3168a_codec_init()) {
			printk(KERN_ERR "audio_evl: codec init failed\n");
			return -1;
		}
		audio_input_channels = ELK_PI_NUM_INPUT_CHANNELS;
		audio_output_channels = ELK_PI_NUM_OUTPUT_CHANNELS;
		num_codec_channels = ELK_PI_NUM_CODEC_CHANNELS;
		audio_format = ELK_PI_CODEC_FORMAT;
		audio_sampling_rate = ELK_PI_SAMPLING_RATE;
	} else {
		printk(KERN_ERR "audio_evl: Unsupported hat\n");
	}

	if (bcm2835_i2s_init(audio_hat)) {
		printk(KERN_ERR "audio_evl: i2s init failed\n");
		return -1;
	}

	ret = alloc_chrdev_region(&rt_audio_devt, 0, 1, "audio_evl");
	if (ret) {
		printk(KERN_ERR "audio_evl:alloc_chrdev_region failed\n");
		goto fail_region;
	}

	cdev_init(&rt_audio_cdev, &audio_driver_fops);
	ret = cdev_add(&rt_audio_cdev, rt_audio_devt, 1);
 	if (ret) {
		goto fail_add;
	}
	dev = device_create(&audio_evl_class, NULL, rt_audio_devt,
				NULL, "audio_evl");
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto fail_dev;
 	}
	printk(KERN_INFO "audio_evl: buffer size = %d\n", audio_buffer_size);
	printk(KERN_INFO "audio_evl: v%d.%d.%d - driver initialized\n",
	       AUDIO_EVL_VERSION_MAJ, AUDIO_EVL_VERSION_MIN,
	       AUDIO_EVL_VERSION_VER);
	return 0;

fail_dev:
	cdev_del(&rt_audio_cdev);
fail_add:
	unregister_chrdev_region(rt_audio_devt, 1);
fail_region:
	class_unregister(&audio_evl_class);

	return ret;
}

static void __exit audio_evl_driver_exit(void)
{
	printk(KERN_INFO "audio_evl: driver exiting...\n");
	if (!strcmp(audio_hat, "hifi-berry")) {
		pcm5122_codec_exit();
	} else if (!strcmp(audio_hat, "elk-pi")) {
		pcm3168a_codec_exit();
	}
	device_destroy(&audio_evl_class, MKDEV(MAJOR(rt_audio_devt), 0));
	cdev_del(&rt_audio_cdev);
	class_unregister(&audio_evl_class);
}

module_init(audio_evl_driver_init)
module_exit(audio_evl_driver_exit)
