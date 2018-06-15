/*
 * Copyright (c) 2018 Sundar Subramaniyan <sundar.subramaniyan@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file  usb_dc_nrf5.c
 * @brief nRF52840 USB device controller driver
 *
 * The driver implements the low level control routines to deal directly
 * with the nRF52840 USBD peripheral.
 */

#include <soc.h>
#include <string.h>
#include <stdio.h>
#include <kernel.h>
#include <usb/usb_dc.h>
#include <usb/usb_device.h>
#include <clock_control.h>
#include <nrf_power.h>
#include <drivers/clock_control/nrf5_clock_control.h>

#include <drivers/include/nrfx_usbd.h>





#define SYS_LOG_LEVEL CONFIG_SYS_LOG_USB_DRIVER_LEVEL
#define SYS_LOG_DOMAIN "usb/dc"
#include <logging/sys_log.h>

/* TODO: temporary */
#define SYS_LOG_DBGX(...) SYS_LOG_DBG(__VA_ARGS__)

#define USB_BMREQUEST_SETADDRESS 0x05
#define USB_BMREQUESTTYPE_POS    7uL
#define USB_BMREQUESTTYPE_MASK   (1uL << USB_BMREQUESTTYPE_POS)
#define USB_BMREQUESTTYPE_HOSTTODEVICE_MASK  0uL
#define USB_BMREQUESTTYPE_DEVICETOHOST_MASK  (1uL << USB_BMREQUESTTYPE_POS)


#define MAX_EP_BUF_SZ		64UL
#define MAX_ISO_EP_BUF_SZ	1024UL

#define USBD_EPSTATUS_EPIN_MASK		(0x1FF << USBD_EPSTATUS_EPIN0_Pos)
#define USBD_EPSTATUS_EPOUT_MASK	(0x1FF << USBD_EPSTATUS_EPOUT0_Pos)
#define USBD_EPDATASTATUS_EPIN_MASK	(0x7F << USBD_EPDATASTATUS_EPIN1_Pos)
#define USBD_EPDATASTATUS_EPOUT_MASK	(0x7F << USBD_EPDATASTATUS_EPOUT1_Pos)

/** USB Work flags */
#define NRF5_USB_STATE_CHANGE	0
#define NRF5_USB_STATUS_CHANGE	1

/**
 * @brief nRF USBD peripheral states
 */
enum usbd_periph_state {
	USBD_DETACHED,
	USBD_ATTACHED,
	USBD_POWERED,
	USBD_SUSPENDED,
	USBD_DEFAULT,
	USBD_ADDRESS_SET,
	USBD_CONFIGURED,
};

/**
 * @brief Endpoint event types.
 */
enum ep_event_type {
	EP_EVT_SETUP_RECV,
	EP_EVT_RECV_REQ,
	EP_EVT_RECV_COMPLETE,
	EP_EVT_WRITE_COMPLETE,
};


/**
 * @brief Endpoint configuration.
 *
 * @param cb      Endpoint callback.
 * @param max_sz  Max packet size supported by endpoint.
 * @param en      Enable/Disable flag.
 * @param addr    Endpoint address.
 * @param type    Endpoint type.
 */
struct nrf5_usbd_ep_cfg {
	usb_dc_ep_callback cb;
	u32_t max_sz;
	bool en;
	u8_t addr;
	enum usb_dc_ep_type type;

};

/**
 * @brief Endpoint buffer
 *
 * @param len    Remaining length to be read/written.
 * @param block  Mempool block, for freeing up buffer after use.
 * @param data	 Pointer to the data buffer	for the endpoint.
 * @param curr	 Pointer to the current offset in the endpoint buffer.
 */
struct nrf5_usbd_ep_buf {
	u32_t len;
	struct k_mem_block block;
	u8_t *data;
	u8_t *curr;
};

/**
 * @brief Endpoint context
 *
 * @param cfg		Endpoint configuration
 * @param buf		Endpoint buffer
 * @param state		Endpoint's current state
 * @param flags		Endpoint's flags
 */
struct nrf5_usbd_ep_ctx {
	struct nrf5_usbd_ep_cfg cfg;
	struct nrf5_usbd_ep_buf buf;
	volatile bool read_complete;
	volatile bool read_pending;
};

/**
 * @brief Endpoint USB event
 *	  Used by ISR to send events to work handler
 *
 * @param node		Used by the kernel for FIFO management
 * @param ep		Endpoint context pointer that needs service
 * @param evt		Event that has occurred from the USBD peripheral
 * @param block		Mempool block pointer for freeing up after use
 * @param misc_u	Miscellaneous information passed as flags
 */
struct usbd_ep_event {
	sys_snode_t node;
	struct nrf5_usbd_ep_ctx *ep;
	enum ep_event_type evt;
	struct k_mem_block block;
	union {
		u32_t flags;
		u32_t frame_counter;
	} misc_u;
};

/**
 * @brief Fifo element pool
 *	Used for allocating fifo elements to pass from ISR to work handler
 * TODO: The number of FIFO elements is an arbitrary number now but it should
 * be derived from the theoretical number of backlog events possible depending
 * on the number of endpoints configured.
 */
#define FIFO_ELEM_MIN_SZ	sizeof(struct usbd_ep_event)
#define FIFO_ELEM_MAX_SZ	sizeof(struct usbd_ep_event)
#define FIFO_ELEM_COUNT		16
#define FIFO_ELEM_ALIGN		sizeof(unsigned int)

K_MEM_POOL_DEFINE(fifo_elem_pool, FIFO_ELEM_MIN_SZ, FIFO_ELEM_MAX_SZ,
		  FIFO_ELEM_COUNT, FIFO_ELEM_ALIGN);

/**
 * @brief Endpoint buffer pool
 *	Used for allocating buffers for the endpoints' data transfer
 *	Max pool size possible: 3072 Bytes (16 EP * 64B + 2 ISO * 1024B)
 */

/** Number of IN Endpoints configured (including control) */
#define CFG_EPIN_CNT (CONFIG_USBD_NRF5_NUM_IN_EP + \
		      CONFIG_USBD_NRF5_NUM_BIDIR_EP)

/** Number of OUT Endpoints configured (including control) */
#define CFG_EPOUT_CNT (CONFIG_USBD_NRF5_NUM_OUT_EP + \
		      CONFIG_USBD_NRF5_NUM_BIDIR_EP)

/** Number of ISO IN Endpoints */
#define CFG_EP_ISOIN_CNT CONFIG_USBD_NRF5_NUM_ISOIN_EP

/** Number of ISO OUT Endpoints */
#define CFG_EP_ISOOUT_CNT CONFIG_USBD_NRF5_NUM_ISOOUT_EP

/** ISO endpoint index */
#define EP_ISOIN_INDEX CFG_EPIN_CNT
#define EP_ISOOUT_INDEX (CFG_EPIN_CNT + CFG_EP_ISOIN_CNT + CFG_EPOUT_CNT)

/** Minimum endpoint buffer size */
#define EP_BUF_MIN_SZ MAX_EP_BUF_SZ

/** Maximum endpoint buffer size */
#if (CFG_EP_ISOIN_CNT || CFG_EP_ISOOUT_CNT)
#define EP_BUF_MAX_SZ MAX_ISO_EP_BUF_SZ
#else
#define EP_BUF_MAX_SZ MAX_EP_BUF_SZ
#endif

/** Total endpoints configured */
#define CFG_EP_CNT (CFG_EPIN_CNT + CFG_EP_ISOIN_CNT + \
			CFG_EPOUT_CNT + CFG_EP_ISOOUT_CNT)

/** Total buffer size for all endpoints */
#define EP_BUF_TOTAL ((CFG_EPIN_CNT * MAX_EP_BUF_SZ) +         \
		      (CFG_EPOUT_CNT * MAX_EP_BUF_SZ) +        \
		      (CFG_EP_ISOIN_CNT * MAX_ISO_EP_BUF_SZ) + \
		      (CFG_EP_ISOOUT_CNT * MAX_ISO_EP_BUF_SZ))

/** Total number of maximum sized buffers needed */
#define EP_BUF_COUNT ((EP_BUF_TOTAL / EP_BUF_MAX_SZ) + \
			((EP_BUF_TOTAL % EP_BUF_MAX_SZ) ? 1 : 0))

/** 4 Byte Buffer alignment required by hardware */
#define EP_BUF_ALIGN sizeof(unsigned int)

K_MEM_POOL_DEFINE(ep_buf_pool, EP_BUF_MIN_SZ, EP_BUF_MAX_SZ,
		  EP_BUF_COUNT, EP_BUF_ALIGN);

/**
 * @brief USBD private structure
 *
 * @param status_cb   Status callback for USB DC notifications
 * @param attached	USBD Attached flag
 * @param ready		USBD Ready flag set after pullup
 * @param state		USBD state
 * @param status_code	Device Status code
 * @param flags		Flags used in work context

 * @param usb_work	USBD work item
 * @param work_queue	FIFO used for queuing up events from ISR
 * @param dma_in_use	Semaphore to restrict access to DMA one at a time

 * @param ep_ctx	Endpoint contexts
 * @param buf		Buffer for the endpoints, aligned to 4 byte boundary
 */
struct nrf5_usbd_ctx {
	usb_dc_status_callback status_cb;

	bool attached;
	bool ready;
	enum usbd_periph_state state;
	enum usb_dc_status_code status_code;
	u32_t flags;

	struct k_work usb_work;
	struct k_fifo work_queue;
	struct k_sem dma_in_use;

	struct nrf5_usbd_ep_ctx ep_ctx[CFG_EP_CNT];
};

static struct nrf5_usbd_ctx usbd_ctx;

void nrfx_usbd_irq_handler(void);


static inline struct nrf5_usbd_ctx *get_usbd_ctx(void)
{
	return &usbd_ctx;
}


static inline nrfx_usbd_ep_t ep_addr_to_nrfx(uint8_t ep)
{
	return (nrfx_usbd_ep_t)ep;
}


static inline uint8_t nrfx_addr_to_ep(nrfx_usbd_ep_t ep)
{
	return (uint8_t)ep;
}


static inline bool ep_is_valid(const u8_t ep)
{
	u8_t ep_num = NRF_USBD_EP_NR_GET(ep);

	if (NRF_USBD_EPIN_CHECK(ep)) {
		if (unlikely(NRF_USBD_EPISO_CHECK(ep))) {
			if (CFG_EP_ISOIN_CNT == 0) {
				return false;
			}
		} else {
			if (ep_num >= CFG_EPIN_CNT) {
				return false;
			}
		}
	} else {
		if (unlikely(NRF_USBD_EPISO_CHECK(ep))) {
			if (CFG_EP_ISOOUT_CNT == 0) {
				return false;
			}
		} else {
			if (ep_num >= CFG_EPOUT_CNT) {
				return false;
			}
		}
	}

	return true;
}


static struct nrf5_usbd_ep_ctx *endpoint_ctx(const u8_t ep)
{
	struct nrf5_usbd_ctx *ctx;
	u8_t ep_num;

	if (!ep_is_valid(ep)) {
		return NULL;
	}

	ctx = get_usbd_ctx();
	ep_num = NRF_USBD_EP_NR_GET(ep);

	if (NRF_USBD_EPIN_CHECK(ep)) {
		if (unlikely(NRF_USBD_EPISO_CHECK(ep))) {
			return &ctx->ep_ctx[EP_ISOIN_INDEX];
		} else {
			return &ctx->ep_ctx[ep_num];
		}
	} else {
		if (unlikely(NRF_USBD_EPISO_CHECK(ep))) {
			return &ctx->ep_ctx[EP_ISOOUT_INDEX];
		} else {
			return &ctx->ep_ctx[CFG_EPIN_CNT +
					    CFG_EP_ISOIN_CNT +
					    ep_num];
		}
	}

	return NULL;
}


static struct nrf5_usbd_ep_ctx *in_endpoint_ctx(const u8_t ep)
{
	return endpoint_ctx(NRF_USBD_EPIN(ep));
}


static struct nrf5_usbd_ep_ctx *out_endpoint_ctx(const u8_t ep)
{
	return endpoint_ctx(NRF_USBD_EPOUT(ep));
}


/**
 * @brief Schedule USBD event processing.
 *
 * Should be called after usbd_evt_put().
 */
static inline void usbd_work_schedule(void)
{
	k_work_submit(&get_usbd_ctx()->usb_work);
}


/**
 * @brief Update USB DC status code.
 *
 * @param status New status code.
 */
static inline void usbd_status_code_update(enum usb_dc_status_code status)
{
	struct nrf5_usbd_ctx *ctx = get_usbd_ctx();

	ctx->status_code = status;
	ctx->flags |= BIT(NRF5_USB_STATUS_CHANGE);
	usbd_work_schedule();
}


/**
 * @brief Allocate USBD event.
 *
 * This function should be called prior to usbd_evt_put().
 *
 * @returns Pointer to the allocated event or NULL if there was no space left.
 */
static inline struct usbd_ep_event *usbd_evt_alloc(void)
{
	int ret;
	struct usbd_ep_event *ev;
	struct k_mem_block block;

	ret = k_mem_pool_alloc(&fifo_elem_pool, &block,
			       sizeof(struct usbd_ep_event),
			       K_NO_WAIT);
	if (ret < 0) {
		SYS_LOG_DBG("USBD event alloc failed!");
		__ASSERT_NO_MSG(0);
		return NULL;
	}

	ev = (struct usbd_ep_event *)block.data;
	memcpy(&ev->block, &block, sizeof(block));
	ev->misc_u.flags = 0;

	return ev;
}


/**
 * @brief Free previously allocated USBD event.
 *
 * Should be called after usbd_evt_get().
 *
 * @param Pointer to the USBD event structure.
 */
static inline void usbd_evt_free(struct usbd_ep_event *ev)
{
	k_mem_pool_free(&ev->block);
}


/**
 * @brief Enqueue USBD event.
 *
 * @param Pointer to the previously allocated and filled event structure.
 */
static inline void usbd_evt_put(struct usbd_ep_event *ev)
{
	k_fifo_put(&get_usbd_ctx()->work_queue, ev);
}


/**
 * @brief Get next enqueued USBD event if present.
 */
static inline struct usbd_ep_event *usbd_evt_get(void)
{
	return k_fifo_get(&get_usbd_ctx()->work_queue, K_NO_WAIT);
}


/**
 * @brief Drop all enqueued events.
 */
static inline void usbd_evt_flush(void)
{
	struct usbd_ep_event *ev;

	do {
		ev = usbd_evt_get();
		if (ev) {
			usbd_evt_free(ev);
		}
	} while (ev != NULL);
}


/**
 * @brief Install the Interrupt service routine.
 *
 * This installs the interrupt service routine and enables the USBD interrupt.
 *
 * @return N/A
 */
static inline void usbd_install_isr(void)
{
	IRQ_CONNECT(CONFIG_USBD_NRF5_IRQ,
		    CONFIG_USBD_NRF5_IRQ_PRI,
		    nrfx_usbd_irq_handler, 0, 0);
}


void nrfx_usbd_power_event_callback(nrf_power_event_t event)
{
	struct nrf5_usbd_ctx *ctx = get_usbd_ctx();

	switch (event) {
	case NRF_POWER_EVENT_USBDETECTED:
		ctx->state = USBD_ATTACHED;
		break;
	case NRF_POWER_EVENT_USBPWRRDY:
		ctx->state = USBD_POWERED;
		break;
	case NRF_POWER_EVENT_USBREMOVED:
		ctx->state = USBD_DETACHED;
		break;
	default:
		SYS_LOG_DBG("Unknown USB power event");
		return;
	}

	ctx->flags |= BIT(NRF5_USB_STATE_CHANGE);
	k_work_submit(&ctx->usb_work);
}


/**
 * @brief Enable/Disable the HF clock
 *
 * Toggle the HF clock. It needs to be enabled for USBD data exchange
 *
 * @param on		Set true to enable the HF clock, false to disable.
 * @param blocking	Set true to block wait till HF clock stabilizes.
 *
 * @return 0 on success, error number otherwise
 */
static int hf_clock_enable(bool on, bool blocking)
{
	int ret = -ENODEV;
	struct device *clock;

	clock = device_get_binding(CONFIG_CLOCK_CONTROL_NRF5_M16SRC_DRV_NAME);
	if (!clock) {
		SYS_LOG_ERR("NRF5 HF Clock device not found!");
		return ret;
	}

	if (on) {
		ret = clock_control_on(clock, (void *)blocking);
	} else {
		ret = clock_control_off(clock, (void *)blocking);
	}

	if (ret) {
		SYS_LOG_ERR("NRF5 HF clock %s fail: %d",
			    on ? "start" : "stop", ret);
		return ret;
	}

	SYS_LOG_DBG("HF clock %s success", on ? "start" : "stop");

	return ret;
}


static void usbd_enable_endpoints(struct nrf5_usbd_ctx *ctx)
{
	struct nrf5_usbd_ep_ctx *ep_ctx;
	int i;

	for (i = 0; i < NRF_USBD_EPIN_CNT; i++) {
		ep_ctx = in_endpoint_ctx(i);
		__ASSERT_NO_MSG(ep_ctx);

		if (ep_ctx->cfg.en) {
			nrfx_usbd_ep_enable(ep_addr_to_nrfx(ep_ctx->cfg.addr));
		}
	}

	for (i = 0; i < NRF_USBD_EPOUT_CNT; i++) {
		ep_ctx = out_endpoint_ctx(i);
		__ASSERT_NO_MSG(ep_ctx);

		if (ep_ctx->cfg.en) {
			nrfx_usbd_ep_enable(ep_addr_to_nrfx(ep_ctx->cfg.addr));
		}
	}
}


static void usbd_handle_state_change(struct nrf5_usbd_ctx *ctx)
{
	switch (ctx->state) {
	case USBD_ATTACHED:
		SYS_LOG_DBG("USB detected");
		nrfx_usbd_enable();
		break;

	case USBD_POWERED:
		SYS_LOG_DBG("USB Powered");
		ctx->status_code = USB_DC_CONNECTED;
		ctx->flags |= BIT(NRF5_USB_STATUS_CHANGE);
		usbd_enable_endpoints(ctx);
		nrfx_usbd_start(true);
		ctx->ready = true;
		break;

	case USBD_DETACHED:
		SYS_LOG_DBG("USB Removed");
		ctx->ready = false;
		nrfx_usbd_stop();
		ctx->status_code = USB_DC_DISCONNECTED;
		ctx->flags |= BIT(NRF5_USB_STATUS_CHANGE);
		break;

	default:
		break;
	}

	if (ctx->flags) {
		k_work_submit(&ctx->usb_work);
	}
}


static void usbd_handle_status_change(struct nrf5_usbd_ctx *ctx)
{
	if (ctx->status_cb) {
		ctx->status_cb(ctx->status_code, NULL);
	}
}


/* Work handler */
static void usbd_work_handler(struct k_work *item)
{
	struct nrf5_usbd_ctx *ctx;
	struct usbd_ep_event *ev;

	SYS_LOG_DBGX("[%08X] /work/", (u32_t) k_current_get());

	ctx = CONTAINER_OF(item, struct nrf5_usbd_ctx, usb_work);

	if (ctx->flags) {
		if (ctx->flags & BIT(NRF5_USB_STATE_CHANGE)) {
			usbd_handle_state_change(ctx);
			ctx->flags &= ~BIT(NRF5_USB_STATE_CHANGE);
		}

		if (ctx->flags & BIT(NRF5_USB_STATUS_CHANGE)) {
			usbd_handle_status_change(ctx);
			ctx->flags &= ~BIT(NRF5_USB_STATUS_CHANGE);
		}
	}

	while ((ev = usbd_evt_get()) != NULL) {
		struct nrf5_usbd_ep_ctx *ep_ctx = ev->ep;

		switch (ev->evt) {
		case EP_EVT_SETUP_RECV: {
			__ASSERT_NO_MSG(ep_ctx);
			__ASSERT(ep_ctx->cfg.type == USB_DC_EP_CONTROL,
				 "Invalid event on CTRL EP.");

			struct nrf5_usbd_ep_ctx *ep_ctx = ev->ep;
			struct usb_setup_packet *usbd_setup;
			nrfx_usbd_setup_t drv_setup;

			nrfx_usbd_setup_get(&drv_setup);

			SYS_LOG_DBGX("SETUP: r:%d rt:%d v:%d i:%d l:%d",
					 (int32_t)drv_setup.bmRequest,
					 (int32_t)drv_setup.bmRequestType,
					 (int32_t)drv_setup.wValue,
					 (int32_t)drv_setup.wIndex,
					 (int32_t)drv_setup.wLength);

			/* SETUP packets are handled by USBD hardware.
			 * For compatibility with the USB stack,
			 * SETUP packet must be reassembled.
			 */
			usbd_setup =
				(struct usb_setup_packet *)ep_ctx->buf.data;
			usbd_setup->bmRequestType = drv_setup.bmRequestType;
			usbd_setup->bRequest      = drv_setup.bmRequest;
			usbd_setup->wValue        = drv_setup.wValue;
			usbd_setup->wIndex        = drv_setup.wIndex;
			usbd_setup->wLength       = drv_setup.wLength;
			ep_ctx->buf.len = sizeof(*usbd_setup);

			/* Inform the stack. */
			ep_ctx->cfg.cb(ep_ctx->cfg.addr, USB_DC_EP_SETUP);

			if (((drv_setup.bmRequestType & USB_BMREQUESTTYPE_MASK)
			     == USB_BMREQUESTTYPE_HOSTTODEVICE_MASK)
			    && (drv_setup.wLength)) {
				SYS_LOG_DBGX("setup_data_clear");
				nrfx_usbd_setup_data_clear();
			}
			break;
		}
		case EP_EVT_RECV_REQ: {
			SYS_LOG_DBGX("EP_EVT_RECV_REQ @ %d", ep_ctx->cfg.addr);

			if (!ep_ctx->read_pending) {
				SYS_LOG_DBGX("read: not pending");
				break;
			}
			if (!ep_ctx->read_complete) {
				SYS_LOG_DBGX("read: busy");
				break;
			}

			ep_ctx->read_pending = false;
			ep_ctx->read_complete = false;

			SYS_LOG_DBGX("[%08X] ++sem /o req",
				     (u32_t) k_current_get());
			k_sem_take(&ctx->dma_in_use, K_FOREVER);
			NRFX_USBD_TRANSFER_OUT(transfer, ep_ctx->buf.data,
					       ep_ctx->cfg.max_sz);
			nrfx_err_t err = nrfx_usbd_ep_transfer(
				ep_addr_to_nrfx(ep_ctx->cfg.addr), &transfer);
			if (err != NRFX_SUCCESS) {
				SYS_LOG_ERR(
					"nRF USBD transfer error (OUT): %d.",
					err);
				SYS_LOG_DBGX("--sem /o err");
				k_sem_give(&ctx->dma_in_use);
			}
			break;
		}
		case EP_EVT_RECV_COMPLETE:
			SYS_LOG_DBGX("EP_EVT_RECV_COMPLETE @ %d",
				     ep_ctx->cfg.addr);
			ep_ctx->cfg.cb(ep_ctx->cfg.addr, USB_DC_EP_DATA_OUT);
			break;

		case EP_EVT_WRITE_COMPLETE:
			SYS_LOG_DBGX("EP_EVT_WRITE_COMPLETE @ %d",
				     ep_ctx->cfg.addr);
			if (ep_ctx->cfg.type == USB_DC_EP_CONTROL) {
				nrfx_usbd_setup_clear();
			}
			ep_ctx->cfg.cb(ep_ctx->cfg.addr, USB_DC_EP_DATA_IN);
			break;
		default:
			break;
		}

		usbd_evt_free(ev);
	}
}


static inline bool dev_attached(void)
{
	return get_usbd_ctx()->attached;
}


static inline bool dev_ready(void)
{
	return get_usbd_ctx()->ready;
}


static void endpoint_ctx_init(void)
{
	struct nrf5_usbd_ep_ctx *ep_ctx;
	int ret;
	u32_t i;

	for (i = 0; i < CFG_EPIN_CNT; i++) {
		ep_ctx = in_endpoint_ctx(i);
		__ASSERT_NO_MSG(ep_ctx);

		ret = k_mem_pool_alloc(&ep_buf_pool, &ep_ctx->buf.block,
				       MAX_EP_BUF_SZ, K_NO_WAIT);
		if (ret < 0) {
			SYS_LOG_ERR("EP buffer alloc failed for EPIN%d", i);
			__ASSERT_NO_MSG(0);
		}

		ep_ctx->buf.data = ep_ctx->buf.block.data;
		ep_ctx->buf.curr = ep_ctx->buf.data;

		ep_ctx->read_complete = true;
		ep_ctx->read_pending = false;
	}

	for (i = 0; i < CFG_EPOUT_CNT; i++) {
		ep_ctx = out_endpoint_ctx(i);
		__ASSERT_NO_MSG(ep_ctx);

		ret = k_mem_pool_alloc(&ep_buf_pool, &ep_ctx->buf.block,
				       MAX_EP_BUF_SZ, K_NO_WAIT);
		if (ret < 0) {
			SYS_LOG_ERR("EP buffer alloc failed for EPOUT%d", i);
			__ASSERT_NO_MSG(0);
		}

		ep_ctx->buf.data = ep_ctx->buf.block.data;
		ep_ctx->buf.curr = ep_ctx->buf.data;

		ep_ctx->read_complete = true;
		ep_ctx->read_pending = false;
	}

	if (CFG_EP_ISOIN_CNT) {
		ep_ctx = in_endpoint_ctx(NRF_USBD_EPIN(8));
		__ASSERT_NO_MSG(ep_ctx);

		ret = k_mem_pool_alloc(&ep_buf_pool, &ep_ctx->buf.block,
				       MAX_ISO_EP_BUF_SZ, K_NO_WAIT);
		if (ret < 0) {
			SYS_LOG_ERR("EP buffer alloc failed for ISOIN");
			__ASSERT_NO_MSG(0);
		}

		ep_ctx->buf.data = ep_ctx->buf.block.data;
		ep_ctx->buf.curr = ep_ctx->buf.data;

		ep_ctx->read_complete = true;
		ep_ctx->read_pending = false;
	}

	if (CFG_EP_ISOOUT_CNT) {
		ep_ctx = out_endpoint_ctx(NRF_USBD_EPOUT(8));
		__ASSERT_NO_MSG(ep_ctx);

		ret = k_mem_pool_alloc(&ep_buf_pool, &ep_ctx->buf.block,
				       MAX_ISO_EP_BUF_SZ, K_NO_WAIT);
		if (ret < 0) {
			SYS_LOG_ERR("EP buffer alloc failed for ISOOUT");
			__ASSERT_NO_MSG(0);
		}

		ep_ctx->buf.data = ep_ctx->buf.block.data;
		ep_ctx->buf.curr = ep_ctx->buf.data;

		ep_ctx->read_complete = true;
		ep_ctx->read_pending = false;
	}
}


static void endpoint_ctx_deinit(void)
{
	struct nrf5_usbd_ep_ctx *ep_ctx;
	u32_t i;

	for (i = 0; i < CFG_EPIN_CNT; i++) {
		ep_ctx = in_endpoint_ctx(i);
		__ASSERT_NO_MSG(ep_ctx);
		k_mem_pool_free(&ep_ctx->buf.block);
		memset(ep_ctx, 0, sizeof(*ep_ctx));
	}

	for (i = 0; i < CFG_EPOUT_CNT; i++) {
		ep_ctx = out_endpoint_ctx(i);
		__ASSERT_NO_MSG(ep_ctx);
		k_mem_pool_free(&ep_ctx->buf.block);
		memset(ep_ctx, 0, sizeof(*ep_ctx));
	}

	if (CFG_EP_ISOIN_CNT) {
		ep_ctx = in_endpoint_ctx(NRF_USBD_EPIN(8));
		__ASSERT_NO_MSG(ep_ctx);
		k_mem_pool_free(&ep_ctx->buf.block);
		memset(ep_ctx, 0, sizeof(*ep_ctx));
	}

	if (CFG_EP_ISOOUT_CNT) {
		ep_ctx = out_endpoint_ctx(NRF_USBD_EPOUT(8));
		__ASSERT_NO_MSG(ep_ctx);
		k_mem_pool_free(&ep_ctx->buf.block);
		memset(ep_ctx, 0, sizeof(*ep_ctx));
	}
}


/**
 * @brief Enable or disable USBD power interrupt.
 *
 * @param enable New interrupt enable state.
 */
static int usbd_power_int_enable(bool enable)
{
	struct device *dev = device_get_binding(CONFIG_USBD_NRF5_NAME);

	if (!dev) {
		SYS_LOG_ERR("Could not get USBD power device binding.");
		return -ENODEV;
	}

	nrf5_power_usb_power_int_enable(dev, enable);

	return 0;
}


static void usbd_event_transfer_ctrl(nrfx_usbd_evt_t const * const p_event)
{
	struct nrf5_usbd_ep_ctx *ep_ctx =
		endpoint_ctx(p_event->data.eptransfer.ep);
	struct nrf5_usbd_ctx *ctx = get_usbd_ctx();

	if (NRF_USBD_EPIN_CHECK(p_event->data.eptransfer.ep)) {
		SYS_LOG_DBGX("EPTRANSFER CTRL IN %d / %d",
			     p_event->data.eptransfer.ep,
			     p_event->data.eptransfer.status);

		if (p_event->data.eptransfer.status == NRF_USBD_EP_OK) {
			struct usbd_ep_event *ev = usbd_evt_alloc();

			ev->ep = ep_ctx;
			ev->evt = EP_EVT_WRITE_COMPLETE;

			SYS_LOG_DBG("--sem /write");
			k_sem_give(&ctx->dma_in_use);

			SYS_LOG_DBG("+e wc");
			usbd_evt_put(ev);
			usbd_work_schedule();
		}
	} else {
		SYS_LOG_DBGX("EPTRANSFER CTRL OUT %d / %d",
			     p_event->data.eptransfer.ep,
			     p_event->data.eptransfer.status);

		if (p_event->data.eptransfer.status == NRF_USBD_EP_WAITING) {
			struct usbd_ep_event *ev = usbd_evt_alloc();

			ep_ctx->read_pending = true;
			ev->ep = ep_ctx;
			ev->evt = EP_EVT_RECV_REQ;
			usbd_evt_put(ev);

			SYS_LOG_DBGX("+e rr");
			usbd_work_schedule();
		} else if (p_event->data.eptransfer.status == NRF_USBD_EP_OK) {
			struct usbd_ep_event *ev = usbd_evt_alloc();
			nrfx_err_t err_code;

			ev->ep = ep_ctx;
			ev->evt = EP_EVT_RECV_COMPLETE;

			err_code = nrfx_usbd_ep_status_get(
				p_event->data.eptransfer.ep, &ep_ctx->buf.len);

			if ((err_code != NRFX_SUCCESS) &&
				(err_code != (nrfx_err_t)NRF_USBD_EP_OK)) {
				SYS_LOG_ERR("_ep_status_get failed! Code: %d.",
					    err_code);
				__ASSERT_NO_MSG(0);
			}

			SYS_LOG_DBG("  len = %d", ep_ctx->buf.len);
			SYS_LOG_DBG("--sem /read_ok");
			k_sem_give(&ctx->dma_in_use);

			usbd_evt_put(ev);
			SYS_LOG_DBG("+e end");
			usbd_work_schedule();
		}
	}
}


static void usbd_event_transfer_data(nrfx_usbd_evt_t const * const p_event)
{
	struct nrf5_usbd_ctx *ctx = get_usbd_ctx();
	struct nrf5_usbd_ep_ctx *ep_ctx =
		endpoint_ctx(p_event->data.eptransfer.ep);

	if (NRF_USBD_EPIN_CHECK(p_event->data.eptransfer.ep)) {
		SYS_LOG_DBGX("EPTRANSFER IN %d / %d",
			     p_event->data.eptransfer.ep,
			     p_event->data.eptransfer.status);
		if (p_event->data.eptransfer.status == NRF_USBD_EP_OK) {
			struct usbd_ep_event *ev = usbd_evt_alloc();

			ev->ep = ep_ctx;
			ev->evt = EP_EVT_WRITE_COMPLETE;

			SYS_LOG_DBGX("--sem /write");
			k_sem_give(&ctx->dma_in_use);

			usbd_evt_put(ev);
			SYS_LOG_DBGX("+e wc");
			usbd_work_schedule();
		}
	} else {
		SYS_LOG_DBGX("EPTRANSFER OUT %d / %d",
			     p_event->data.eptransfer.ep,
			     p_event->data.eptransfer.status);
		if (p_event->data.eptransfer.status == NRF_USBD_EP_WAITING) {
			struct usbd_ep_event *ev = usbd_evt_alloc();

			ev->ep = ep_ctx;
			ev->evt = EP_EVT_RECV_REQ;
			usbd_evt_put(ev);
			ep_ctx->read_pending = true;
			SYS_LOG_DBGX("+e rr");
			usbd_work_schedule();
		} else if (p_event->data.eptransfer.status == NRF_USBD_EP_OK) {
			struct usbd_ep_event *ev = usbd_evt_alloc();

			ev->ep = ep_ctx;
			ev->evt = EP_EVT_RECV_COMPLETE;

			ep_ctx->buf.len = nrf_usbd_ep_amount_get(
				p_event->data.eptransfer.ep);
			SYS_LOG_DBGX("  len = %d", ep_ctx->buf.len);
			SYS_LOG_DBGX("--sem /read_ok");
			k_sem_give(&ctx->dma_in_use);

			usbd_evt_put(ev);
			SYS_LOG_DBGX("+e end");
			usbd_work_schedule();
		}
	}
}


/**
 * @brief nRFx USBD driver event handler function.
 */
static void usbd_event_handler(nrfx_usbd_evt_t const * const p_event)
{
	struct nrf5_usbd_ep_ctx *ep_ctx;
	struct usbd_ep_event *ev;

	switch (p_event->type) {
	case NRFX_USBD_EVT_SUSPEND:
		SYS_LOG_DBG("SUSPEND state detected.");
		break;
	case NRFX_USBD_EVT_RESUME:
		SYS_LOG_DBG("RESUMING from suspend.");
		break;
	case NRFX_USBD_EVT_WUREQ:
		SYS_LOG_DBG("RemoteWU initiated.");
		break;
	case NRFX_USBD_EVT_RESET:
		SYS_LOG_DBG("USBD Reset.");
		usbd_status_code_update(USB_DC_RESET);
		break;
	case NRFX_USBD_EVT_SOF:
		break;

	case NRFX_USBD_EVT_EPTRANSFER:

		SYS_LOG_DBGX("[%08X] ept", (u32_t)k_current_get());

		ep_ctx = endpoint_ctx(p_event->data.eptransfer.ep);
		switch (ep_ctx->cfg.type) {
		case USB_DC_EP_CONTROL:
			usbd_event_transfer_ctrl(p_event);
			break;
		case USB_DC_EP_BULK:
		case USB_DC_EP_INTERRUPT:
			usbd_event_transfer_data(p_event);
			break;
		case USB_DC_EP_ISOCHRONOUS:
			usbd_event_transfer_data(p_event);
			break;
		default:
			break;
		}
		break;

	case NRFX_USBD_EVT_SETUP: {
			nrfx_usbd_setup_t drv_setup;

			nrfx_usbd_setup_get(&drv_setup);
			if (drv_setup.bmRequest != USB_BMREQUEST_SETADDRESS) {
				/* SetAddress is habdled by USBD hardware.
				 * No software action required.
				 */

				struct nrf5_usbd_ep_ctx *ep_ctx =
					endpoint_ctx(NRF_USBD_EPOUT(0));
				ev = usbd_evt_alloc();
				ev->ep = ep_ctx;
				ev->evt = EP_EVT_SETUP_RECV;
				usbd_evt_put(ev);
				usbd_work_schedule();
			}
		break;
	}

	default:
		break;
	}
}


int usb_dc_attach(void)
{
	struct nrf5_usbd_ctx *ctx = get_usbd_ctx();
	nrfx_err_t err;
	int ret;

	if (ctx->attached) {
		return 0;
	}

	k_work_init(&ctx->usb_work, usbd_work_handler);
	k_fifo_init(&ctx->work_queue);
	k_sem_init(&ctx->dma_in_use, 1, 1);


	usbd_install_isr();

	ret = hf_clock_enable(true, false);
	if (ret) {
		return ret;
	}

	err = nrfx_usbd_init(usbd_event_handler);

	if (err != NRFX_SUCCESS) {
		SYS_LOG_DBG("nRF USBD driver init failed. Code: %d.",
			    (u32_t)err);
		return -EIO;
	}
	ret = usbd_power_int_enable(true);
	if (ret) {
		return ret;
	}
	endpoint_ctx_init();
	ctx->attached = true;

	return ret;
}


int usb_dc_detach(void)
{
	struct nrf5_usbd_ctx *ctx = get_usbd_ctx();
	int ret;

	ctx->flags = 0;
	ctx->state = USBD_DETACHED;
	ctx->status_code = USB_DC_UNKNOWN;

	usbd_evt_flush();
	k_sem_reset(&ctx->dma_in_use);
	endpoint_ctx_deinit();

	/* Following functions may return error if the peripheral
	 * was not initialized before. This can be ignored.
	 */
	(void)nrfx_usbd_disable();
	(void)nrfx_usbd_uninit();

	ret = hf_clock_enable(false, false);
	if (ret) {
		return ret;
	}
	ret = usbd_power_int_enable(false);
	if (ret) {
		return ret;
	}
	ctx->attached = false;
	return ret;
}


int usb_dc_reset(void)
{
	int ret;

	if (!dev_attached() || !dev_ready()) {
		return -ENODEV;
	}

	SYS_LOG_DBG("USBD Reset.");

	ret = usb_dc_detach();
	if (ret) {
		return ret;
	}

	ret = usb_dc_attach();
	if (ret) {
		return ret;
	}

	return 0;
}

int usb_dc_set_address(const u8_t addr)
{
	struct nrf5_usbd_ctx *ctx;

	if (!dev_attached() || !dev_ready()) {
		return -ENODEV;
	}

	/**
	 * Nothing to do here. The USBD HW already takes care of initiating
	 * STATUS stage. Just double check the address for sanity.
	 */
	__ASSERT(addr == (u8_t)NRF_USBD->USBADDR, "USB Address incorrect!");

	ctx = get_usbd_ctx();
	ctx->state = USBD_ADDRESS_SET;

	SYS_LOG_DBG("Address set to: %d.", addr);

	return 0;
}


int usb_dc_ep_check_cap(const struct usb_dc_ep_cfg_data * const ep_cfg)
{
	u8_t ep_idx = NRF_USBD_EP_NR_GET(ep_cfg->ep_addr);

	SYS_LOG_DBG("ep %x, mps %d, type %d", ep_cfg->ep_addr, ep_cfg->ep_mps,
		ep_cfg->ep_type);

	if ((ep_cfg->ep_type == USB_DC_EP_CONTROL) && ep_idx) {
		SYS_LOG_ERR("invalid endpoint configuration");
		return -1;
	}

	if (!NRF_USBD_EP_VALIDATE(ep_cfg->ep_addr)) {
		SYS_LOG_ERR("invalid endpoint index/address");
		return -1;
	}

	if ((ep_cfg->ep_type == USB_DC_EP_ISOCHRONOUS) &&
		(!NRF_USBD_EPISO_CHECK(ep_cfg->ep_addr))) {
		SYS_LOG_WRN("invalid endpoint type");
		return -1;
	}

	return 0;
}


int usb_dc_ep_configure(const struct usb_dc_ep_cfg_data * const ep_cfg)
{
	struct nrf5_usbd_ep_ctx *ep_ctx;

	if (!dev_attached()) {
		return -ENODEV;
	}

	/**
	 * TODO:
	 * For ISO endpoints, application has to use EPIN/OUT 8
	 * but right now there's no standard way of knowing the
	 * ISOIN/ISOOUT endpoint number in advance to configure
	 * accordingly. So either this needs to be chosen in the
	 * menuconfig in application area or perhaps in device tree
	 * at compile time or introduce a new API to read the endpoint
	 * configuration at runtime before configuring them.
	 */
	ep_ctx = endpoint_ctx(ep_cfg->ep_addr);
	if (!ep_ctx) {
		return -EINVAL;
	}

	ep_ctx->cfg.addr = ep_cfg->ep_addr;
	ep_ctx->cfg.type = ep_cfg->ep_type;
	ep_ctx->cfg.max_sz = ep_cfg->ep_mps;

	return 0;
}


int usb_dc_ep_set_stall(const u8_t ep)
{
	struct nrf5_usbd_ep_ctx *ep_ctx;

	if (!dev_attached() || !dev_ready()) {
		return -ENODEV;
	}

	ep_ctx = endpoint_ctx(ep);
	if (!ep_ctx) {
		return -EINVAL;
	}

	switch (ep_ctx->cfg.type) {
	case USB_DC_EP_CONTROL:
		nrfx_usbd_setup_stall();
		break;
	case USB_DC_EP_BULK:
	case USB_DC_EP_INTERRUPT:
		nrfx_usbd_ep_stall(ep_addr_to_nrfx(ep));
		break;
	case USB_DC_EP_ISOCHRONOUS:
		SYS_LOG_ERR("STALL unsupported on ISO endpoint.s");
		return -EINVAL;
	}

	ep_ctx->buf.len = 0;
	ep_ctx->buf.curr = ep_ctx->buf.data;

	SYS_LOG_DBG("STALL on EP %d.", ep);

	return 0;
}


int usb_dc_ep_clear_stall(const u8_t ep)
{

	struct nrf5_usbd_ep_ctx *ep_ctx;

	if (!dev_attached() || !dev_ready()) {
		return -ENODEV;
	}

	ep_ctx = endpoint_ctx(ep);
	if (!ep_ctx) {
		return -EINVAL;
	}

	nrfx_usbd_ep_stall_clear(ep_addr_to_nrfx(ep));
	SYS_LOG_DBG("Unstall on EP %d", ep);

	return 0;
}


int usb_dc_ep_halt(const u8_t ep)
{
	return usb_dc_ep_set_stall(ep);
}


int usb_dc_ep_is_stalled(const u8_t ep, u8_t *const stalled)
{
	struct nrf5_usbd_ep_ctx *ep_ctx;

	if (!dev_attached() || !dev_ready()) {
		return -ENODEV;
	}

	ep_ctx = endpoint_ctx(ep);
	if (!ep_ctx) {
		return -EINVAL;
	}

	*stalled = (u8_t) nrfx_usbd_ep_stall_check(ep_addr_to_nrfx(ep));

	return 0;
}


int usb_dc_ep_enable(const u8_t ep)
{
	struct nrf5_usbd_ep_ctx *ep_ctx;

	if (!dev_attached()) {
		return -ENODEV;
	}

	ep_ctx = endpoint_ctx(ep);
	if (!ep_ctx) {
		return -EINVAL;
	}

	if (ep_ctx->cfg.en) {
		return -EALREADY;
	}

	SYS_LOG_DBG("EP enable: %d.", ep);

	ep_ctx->cfg.en = true;

	/* Defer the endpoint enable if USBD is not ready yet. */
	if (dev_ready()) {
		nrfx_usbd_ep_enable(ep_addr_to_nrfx(ep));
	}

	return 0;
}


int usb_dc_ep_disable(const u8_t ep)
{
	struct nrf5_usbd_ep_ctx *ep_ctx;

	if (!dev_attached() || !dev_ready()) {
		return -ENODEV;
	}

	ep_ctx = endpoint_ctx(ep);
	if (!ep_ctx) {
		return -EINVAL;
	}

	if (!ep_ctx->cfg.en) {
		return -EALREADY;
	}

	SYS_LOG_DBG("EP disable: %d.", ep);

	nrfx_usbd_ep_disable(ep_addr_to_nrfx(ep));
	ep_ctx->cfg.en = false;

	return 0;
}


int usb_dc_ep_flush(const u8_t ep)
{
	struct nrf5_usbd_ep_ctx *ep_ctx;

	if (!dev_attached() || !dev_ready()) {
		return -ENODEV;
	}

	ep_ctx = endpoint_ctx(ep);
	if (!ep_ctx) {
		return -EINVAL;
	}

	ep_ctx->buf.len = 0;
	ep_ctx->buf.curr = ep_ctx->buf.data;

	nrfx_usbd_transfer_out_drop(ep_addr_to_nrfx(ep));

	return 0;
}


int usb_dc_ep_write(const u8_t ep, const u8_t *const data,
		    const u32_t data_len, u32_t * const ret_bytes)
{
	SYS_LOG_DBGX("ep_write: ep %d, len %d", ep, data_len);
	struct nrf5_usbd_ctx *ctx = get_usbd_ctx();
	struct nrf5_usbd_ep_ctx *ep_ctx;
	u32_t bytes_to_copy;

	if (!dev_attached() || !dev_ready()) {
		return -ENODEV;
	}

	if (NRF_USBD_EPOUT_CHECK(ep)) {
		return -EINVAL;
	}

	ep_ctx = endpoint_ctx(ep);
	if (!ep_ctx) {
		return -EINVAL;
	}


	int sem_status = k_sem_take(&ctx->dma_in_use, K_NO_WAIT);

	/* USBD hardware does not allow scheduling multiple DMA transfers
	 * at a time. Next USB transfer can be triggered after all DMA
	 * operations are complete.
	 */
	if (sem_status != 0) {
		SYS_LOG_DBGX("[%08X] !sem wr", (u32_t)k_current_get());
		return -EAGAIN;
	}

	SYS_LOG_DBGX("[%08X] ++sem wr", (u32_t)k_current_get());

	/* TODO: long transfers */
	bytes_to_copy = min(data_len, ep_ctx->cfg.max_sz);
	memcpy(ep_ctx->buf.data, data, bytes_to_copy);
	ep_ctx->buf.len = bytes_to_copy;

	if (ret_bytes) {
		*ret_bytes = bytes_to_copy;
	}

	if ((ep_ctx->cfg.type == USB_DC_EP_CONTROL)
	    & (nrfx_usbd_last_setup_dir_get() != ep)) {
		SYS_LOG_DBGX(" --sem (**status)");
		k_sem_give(&ctx->dma_in_use);
		nrfx_usbd_setup_clear();
		return 0;
	}
	NRFX_USBD_TRANSFER_IN(transfer, ep_ctx->buf.data, ep_ctx->buf.len);
	nrfx_err_t err = nrfx_usbd_ep_transfer(ep_addr_to_nrfx(ep), &transfer);

	if (err != NRFX_SUCCESS) {
		SYS_LOG_DBGX(" --sem");
		k_sem_give(&ctx->dma_in_use);
		SYS_LOG_ERR("nRF USBD write error: %d.", (u32_t)err);
		__ASSERT_NO_MSG(0);
	}

	return 0;
}


int usb_dc_ep_read_wait(u8_t ep, u8_t *data, u32_t max_data_len,
			u32_t *read_bytes)
{
	SYS_LOG_DBGX("ep_read_wait: ep %d, maxlen %d", ep, max_data_len);

	struct nrf5_usbd_ep_ctx *ep_ctx;
	u32_t bytes_to_copy;

	if (!dev_attached() || !dev_ready()) {
		return -ENODEV;
	}

	if (NRF_USBD_EPIN_CHECK(ep)) {
		return -EINVAL;
	}

	if (!data && max_data_len) {
		return -EINVAL;
	}

	ep_ctx = endpoint_ctx(ep);
	if (!ep_ctx) {
		return -EINVAL;
	}

	bytes_to_copy = min(max_data_len, ep_ctx->buf.len);

	if (!data && !max_data_len) {
		SYS_LOG_DBGX("no-read, %d", ep_ctx->buf.len);
		if (read_bytes) {
			*read_bytes = ep_ctx->buf.len;
		}
		return 0;
	}

	memcpy(data, ep_ctx->buf.curr, bytes_to_copy);
	{
		u32_t n = bytes_to_copy;

		if (n > 32) {
			n = 32;
		}
		char c[128];
		char cs[8];
		u32_t pos = 0;
		u32_t i = 0;

		/* TODO: remove */
		for (i = 0; i < n; ++i) {
			sprintf(cs, "%02X ", data[i]);
			memcpy(&c[pos], cs, 3);
			pos += 3;
		}
		c[pos] = 0;
		SYS_LOG_DBGX("R:%d > %s", bytes_to_copy, c);
	}

	ep_ctx->buf.curr += bytes_to_copy;
	ep_ctx->buf.len -= bytes_to_copy;
	if (read_bytes) {
		*read_bytes = bytes_to_copy;
	}

	return 0;
}


int usb_dc_ep_read_continue(u8_t ep)
{
	struct nrf5_usbd_ep_ctx *ep_ctx;

	if (!dev_attached() || !dev_ready()) {
		return -ENODEV;
	}

	if (NRF_USBD_EPIN_CHECK(ep)) {
		return -EINVAL;
	}

	ep_ctx = endpoint_ctx(ep);
	if (!ep_ctx) {
		return -EINVAL;
	}

	ep_ctx->buf.curr = ep_ctx->buf.data;
	ep_ctx->read_complete = true;

	if (ep_ctx->read_pending) {
		struct usbd_ep_event *ev = usbd_evt_alloc();

		ev->ep = ep_ctx;
		ev->evt = EP_EVT_RECV_REQ;
		usbd_evt_put(ev);
		SYS_LOG_DBGX("+e rr2");
		usbd_work_schedule();
	}
	return 0;
}


int usb_dc_ep_read(const u8_t ep, u8_t *const data,
		   const u32_t max_data_len, u32_t * const read_bytes)
{
	SYS_LOG_DBGX("ep_read: ep %d, maxlen %d", ep, max_data_len);
	int ret;

	ret = usb_dc_ep_read_wait(ep, data, max_data_len, read_bytes);
	if (ret) {
		return ret;
	}

	if (!data && !max_data_len) {
		return ret;
	}

	ret = usb_dc_ep_read_continue(ep);
	return ret;
}


int usb_dc_ep_set_callback(const u8_t ep, const usb_dc_ep_callback cb)
{
	struct nrf5_usbd_ep_ctx *ep_ctx;

	if (!dev_attached()) {
		return -ENODEV;
	}

	ep_ctx = endpoint_ctx(ep);
	if (!ep_ctx) {
		return -EINVAL;
	}

	ep_ctx->cfg.cb = cb;

	return 0;
}


int usb_dc_set_status_callback(const usb_dc_status_callback cb)
{
	get_usbd_ctx()->status_cb = cb;
	return 0;
}


int usb_dc_ep_mps(const u8_t ep)
{
	struct nrf5_usbd_ep_ctx *ep_ctx;

	if (!dev_attached()) {
		return -ENODEV;
	}

	ep_ctx = endpoint_ctx(ep);
	if (!ep_ctx) {
		return -EINVAL;
	}

	return ep_ctx->cfg.max_sz;
}

