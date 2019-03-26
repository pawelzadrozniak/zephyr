#include <kernel.h>
#include <usb/usb_common.h>
#include <usb/usb_device.h>
#include <usb_descriptor.h>
#include <usb/class/usb_audio.h>
#include <usb/class/usb_audio_desc.h>

#include <misc/byteorder.h>

//TODO
#define CONFIG_AUDIO_EP_SIZE	256
#define CONFIG_USB_AUDIO_DEVICE_NAME_0 "ASIO_0"
#define CONFIG_USB_AUDIO_MAX_PAYLOAD_SIZE 16

#define LOG_LEVEL 4 //todo
#include <logging/log.h>
LOG_MODULE_REGISTER(usb_audio);


/* Device data structure */
struct usb_audio_dev_data_t {
	/* USB device status code */
	enum usb_dc_status_code usb_status;
	/* Callback function pointer/arg */
	//uart_irq_callback_user_data_t cb;
	void *cb_data;
	struct k_work cb_work;

	struct usb_dev_data common;

	u8_t interface_data[CONFIG_USB_AUDIO_MAX_PAYLOAD_SIZE];
};

static sys_slist_t usb_audio_data_devlist;


#define DEFINE_AUDIO_HEADPHONES_DESCR(x, stream_ep_addr)\
	DEFINE_AUDIO_HEADPHONES_DESCR_TYPES(x, 1)			\
	USBD_CLASS_DESCR_DEFINE(primary, x)				\
	struct usb_audio_config_##x usb_audio_desc_##x = {		\
	.if_control = INITIALIZER_IF_CONTROL,				\
	.control_desc = {						\
		.header = INITIALIZER_IFC_HEADER(x),			\
		.input_terminal = INITIALIZER_IFC_INPUT_TERMINAL(1),	\
		.feature = INITIALIZER_IFC_FEATURE_UNIT(x, 2, 1) ,	\
		.output_terminal = INITIALIZER_IFC_OUTPUT_TERMINAL(3, 2),\
	},								\
	.if_streaming_non_iso = INITIALIZER_IF_STREAMING_NON_ISO,	\
	.if_streaming = INITIALIZER_IF_STREAMING,			\
	.streaming_desc = {						\
		.iface = INITIALIZER_IFS_AS_IFACE(1),			\
		.format = INITIALIZER_IFS_FORMAT_III(x),		\
		.ep = INITIALIZER_IFS_AS_ENDPOINT			\
	},								\
	.if_streaming_ep = INITIALIZER_IF_EP(stream_ep_addr,		\
					     USB_DC_EP_ISOCHRONOUS,	\
					     CONFIG_AUDIO_EP_SIZE,	\
					     0x01)			\
}

#define DEFINE_AUDIO_HEADPHONES_DESCR_TYPES(name, param_if_count)	\
	DEFINE_AUDIO_AS_FORMAT_III_TYPE(name, 1)			\
	DEFINE_AUDIO_HEADER_DESC_TYPE(name, param_if_count)		\
	DEFINE_AUDIO_FEATURE_UNIT_DESC_TYPE(name)			\
	struct usb_audio_config_control_##name {			\
		struct audio_header_desc_##name  header;		\
		struct audio_input_terminal_desc input_terminal;	\
		struct audio_feature_unit_desc_##name feature;		\
		struct audio_output_terminal_desc output_terminal;	\
	} __packed;							\
	struct usb_audio_config_streaming_##name {			\
		struct audio_as_iface_desc iface;			\
		struct audio_as_format_type_iii_desc_##name  format;	\
		struct audio_as_endpoint_desc ep;			\
	} __packed;							\
	struct usb_audio_config_##name {				\
		struct usb_if_descriptor if_control;			\
		struct usb_audio_config_control_##name control_desc;	\
		struct usb_if_descriptor if_streaming_non_iso;		\
		struct usb_if_descriptor if_streaming;			\
		struct usb_audio_config_streaming_##name streaming_desc;\
		struct usb_audio_ep_descriptor if_streaming_ep;		\
	} __packed;


#define DEFINE_AUDIO_EP(x, stream_ep_addr)	\
	static struct usb_ep_cfg_data usb_audio_ep_data_##x[] = {	\
		INITIALIZER_EP_DATA(usb_audio_ep_cb, stream_ep_addr),		\
	}

#define DEFINE_AUDIO_CFG_DATA(x)					\
	USBD_CFG_DATA_DEFINE(usb_audio)					\
	struct usb_cfg_data usb_audio_config_##x = {			\
		.usb_device_description = NULL,				\
		.interface_config = audio_interface_config,		\
		.interface_descriptor = &usb_audio_desc_##x.if_control,	\
		.cb_usb_status_composite =				\
				audio_status_composite_cb,		\
		.interface = {						\
			.class_handler = audio_class_handle_req,	\
			.custom_handler = NULL,				\
			.payload_data = NULL,				\
		},							\
		.num_endpoints = ARRAY_SIZE(usb_audio_ep_data_##x),	\
		.endpoint = usb_audio_ep_data_##x,			\
	};

#define DEFINE_AUDIO_DEV_DATA(x)					\
	static struct usb_audio_dev_data_t audio_dev_data_##x;




/*

struct audio_instance {

    struct audio_subclass_desc const * const p_format_dsc;  //!< Audio class Format descriptor
    struct audio_subclass_desc const * const p_input_dsc;   //!< Audio class Input Terminal descriptor
    struct audio_subclass_desc const * const p_output_dsc;  //!< Audio class Output Terminal descriptor
    struct audio_subclass_desc const * const p_feature_dsc; //!< Audio class Feature Unit descriptor

    uint8_t  delay;     //!< Streaming delay
    uint16_t format;    //!< FormatTag (@ref app_usbd_audio_as_iface_format_tag_t)
    uint16_t ep_size;   //!< Endpoint size

    //app_usbd_audio_subclass_t type_streaming;   //!< Streaming type MIDISTREAMING/AUDIOSTREAMING (@ref app_usbd_audio_subclass_t)

    //app_usbd_audio_user_ev_handler_t user_ev_handler; //!< User event handler
};

*/


static void audio_interface_config(struct usb_desc_header *head,
				      u8_t bInterfaceNumber)
{
	ARG_UNUSED(head);
	LOG_WRN("CFG");

	//loopback_cfg.if0.bInterfaceNumber = bInterfaceNumber;
}

static void audio_status_composite_cb(struct usb_cfg_data *cfg,
					    enum usb_dc_status_code status,
					    const u8_t *param)
{
}


/**
 * @brief Handler called for Class requests not handled by the USB stack.
 *
 * @param pSetup    Information about the request to execute.
 * @param len       Size of the buffer.
 * @param data      Buffer containing the request result.
 *
 * @return  0 on success, negative errno code on fail.
 */
int audio_class_handle_req(struct usb_setup_packet *pSetup,
			   s32_t *len, u8_t **data)
{
	LOG_WRN("Req %d",(u32_t)pSetup->bRequest);
	/*
	struct audio_dev_data_t *dev_data;
	struct usb_dev_data *common;

	common = usb_get_dev_data_by_iface(&cdc_acm_data_devlist,
					   sys_le16_to_cpu(pSetup->wIndex));
	if (common == NULL) {
		LOG_WRN("Device data not found for interface %u",
			sys_le16_to_cpu(pSetup->wIndex));
		return -ENODEV;
	}

	dev_data = CONTAINER_OF(common, struct cdc_acm_dev_data_t, common);

	switch (pSetup->bRequest) {
	case SET_LINE_CODING:
		memcpy(&dev_data->line_coding,
		       *data, sizeof(dev_data->line_coding));
		LOG_DBG("CDC_SET_LINE_CODING %d %d %d %d",
			sys_le32_to_cpu(dev_data->line_coding.dwDTERate),
			dev_data->line_coding.bCharFormat,
			dev_data->line_coding.bParityType,
			dev_data->line_coding.bDataBits);
		break;

	case SET_CONTROL_LINE_STATE:
		dev_data->line_state = (u8_t)sys_le16_to_cpu(pSetup->wValue);
		LOG_DBG("CDC_SET_CONTROL_LINE_STATE 0x%x",
			dev_data->line_state);
		break;

	case GET_LINE_CODING:
		*data = (u8_t *)(&dev_data->line_coding);
		*len = sizeof(dev_data->line_coding);
		LOG_DBG("CDC_GET_LINE_CODING %d %d %d %d",
			sys_le32_to_cpu(dev_data->line_coding.dwDTERate),
			dev_data->line_coding.bCharFormat,
			dev_data->line_coding.bParityType,
			dev_data->line_coding.bDataBits);
		break;

	default:
		LOG_DBG("CDC ACM request 0x%x, value 0x%x",
			pSetup->bRequest, pSetup->wValue);
		return -EINVAL;
	}*/

	return 0;
}




static void usb_audio_ep_cb(u8_t ep, enum usb_dc_ep_cb_status_code ep_status)
{

}



static const struct usb_audio_device_api {
	void (*init)(void);
} usb_audio_api;

static int usb_audio_device_init(struct device *dev)
{
	LOG_DBG("Init USB audio Device: dev %p (%s)", dev, dev->config->name);

	struct usb_audio_dev_data_t * const dev_data = (void*) dev->driver_data;
	int ret = 0;

	dev_data->common.dev = dev;
	sys_slist_append(&usb_audio_data_devlist, &dev_data->common.node);




	struct usb_cfg_data *cfg = (void *)dev->config->config_info;

	cfg->interface.payload_data = dev_data->interface_data;
	cfg->usb_device_description = usb_get_device_descriptor();

	/* Initialize the USB driver with the right configuration */
	ret = usb_set_config(cfg);
	if (ret < 0) {
		LOG_ERR("Failed to config USB");
		return ret;
	}

	/* Enable USB driver */
	ret = usb_enable(cfg);
	if (ret < 0) {
		LOG_ERR("Failed to enable USB");
		return ret;
	}


	return 0;
}

#define DEFINE_AUDIO_DEVICE(x)						\
	DEVICE_AND_API_INIT(usb_audio_device_##x,				\
			    CONFIG_USB_AUDIO_DEVICE_NAME_##x,		\
			    &usb_audio_device_init,			\
			    &audio_dev_data_##x,			\
			    &usb_audio_config_##x, POST_KERNEL,		\
			    CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,	\
			    &usb_audio_api)

DEFINE_AUDIO_EP(0,0x00);
DEFINE_AUDIO_HEADPHONES_DESCR(0,0x00);
DEFINE_AUDIO_CFG_DATA(0);
DEFINE_AUDIO_DEV_DATA(0);
DEFINE_AUDIO_DEVICE(0);
//todo: define device

