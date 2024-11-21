// SPDX-License-Identifier: GPL-2.0
/*
 * @copyright 2017-2024 ELK Audio AB, Stockholm
 */

#ifndef AUDIO_EVL_H
#define AUDIO_EVL_H

#include <linux/io.h>
#include <linux/ioctl.h>
#include <evl/flag.h>

#define EVL_SUBCLASS_GPIO	0
#define DEVICE_NAME		"audio_evl"
#define RTAUDIO_PROFILE_VER	1
#define AUDIO_EVL_VERSION_MAJ	1
#define AUDIO_EVL_VERSION_MIN	1
#define AUDIO_EVL_VERSION_VER	1

#define AUDIO_IOC_MAGIC		'r'

/* ioctl request to wait on dma callback */
#define AUDIO_IRQ_WAIT			_IOR(AUDIO_IOC_MAGIC, 1, int)
/* This ioctl not used anymore but kept for backwards compatibility */
#define AUDIO_IMMEDIATE_SEND		_IOW(AUDIO_IOC_MAGIC, 2, int)
/* ioctl request to start receiving audio callbacks */
#define AUDIO_PROC_START		_IO(AUDIO_IOC_MAGIC, 3)
/* ioctl to inform the driver the user space process has completed */
#define AUDIO_USERPROC_FINISHED		_IOW(AUDIO_IOC_MAGIC, 4, int)
/* ioctl to stop receiving audio callbacks */
#define AUDIO_PROC_STOP			_IO(AUDIO_IOC_MAGIC, 5)
/* ioctl for getting audio channel information */
#define AUDIO_GET_INPUT_CHAN_INFO		_IOWR(AUDIO_IOC_MAGIC, 11, struct audio_channel_info_data)
/* ioctl for getting audio channel information */
#define AUDIO_GET_OUTPUT_CHAN_INFO		_IOWR(AUDIO_IOC_MAGIC, 12, struct audio_channel_info_data)

enum audio_channel_direction {
	INPUT_DIRECTION = 0,
	OUTPUT_DIRECTION = 1,
};

enum codec_sample_format {
	INT24_LJ = 1,
	INT24_I2S,
	INT24_RJ,
	INT24_32RJ,
	INT32,
	BINARY,
};

struct audio_channel_info_req {
	uint32_t buffer_size_in_frames;
	uint8_t sw_ch_id;
	uint8_t direction;
};

#define AUDIO_CHANNEL_NAME_SIZE 32
struct audio_channel_info_data {
	uint8_t sw_ch_id;
	uint8_t hw_ch_id;
	uint8_t direction;
	uint8_t sample_format;
	uint8_t channel_name[AUDIO_CHANNEL_NAME_SIZE];
	uint32_t start_offset_in_words;
	uint32_t stride_in_words;
};
#define AUDIO_CHANNEL_NOT_VALID 255

enum platform_type {
	NATIVE_AUDIO = 1,
	SYNC_WITH_UC_AUDIO,
	ASYNC_WITH_UC_AUDIO,
};

enum usb_audio_type
{
    NONE = 1,
    NATIVE_ALSA,
    EXTERNAL_UC
};

struct audio_evl_buffers {
	uint32_t 	 	*cv_gate_out;
	uint32_t 	 	*cv_gate_in;
	void			*tx_buf;
	void			*rx_buf;
	size_t			buffer_len;
	size_t			period_len;
	dma_addr_t		tx_phys_addr;
	dma_addr_t		rx_phys_addr;
};

/* General audio evl device struct */
struct audio_evl_dev {
	struct device			*dev;
	void __iomem			*i2s_base_addr;
	struct dma_chan			*dma_tx;
	struct dma_chan			*dma_rx;
	struct dma_async_tx_descriptor 	*tx_desc;
	struct dma_async_tx_descriptor	*rx_desc;
	dma_addr_t			fifo_dma_addr;
	unsigned			addr_width;
	unsigned			dma_burst_size;
	struct audio_evl_buffers	*buffer;
	struct evl_flag 	event_flag;
	unsigned			wait_flag;
	unsigned			buffer_idx;
	uint64_t			kinterrupts;
	struct clk			*clk;
	bool				cv_gate_enabled;
	int				clk_rate;
	char 				*audio_hat;
};
#endif
