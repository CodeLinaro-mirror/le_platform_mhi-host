/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __QRTR_H_
#define __QRTR_H_

#include <linux/types.h>

#define QRTR_NODE_BCAST 0xffffffffu
#define QRTR_PORT_CTRL  0xfffffffeu

struct sockaddr_qrtr {
	__kernel_sa_family_t sq_family;
	__u32 sq_node;
	__u32 sq_port;
};

enum qrtr_pkt_type {
	QRTR_TYPE_DATA		= 1,
	QRTR_TYPE_HELLO		= 2,
	QRTR_TYPE_BYE		= 3,
	QRTR_TYPE_NEW_SERVER	= 4,
	QRTR_TYPE_DEL_SERVER	= 5,
	QRTR_TYPE_DEL_CLIENT	= 6,
	QRTR_TYPE_RESUME_TX	= 7,
	QRTR_TYPE_EXIT          = 8,
	QRTR_TYPE_PING          = 9,
	QRTR_TYPE_NEW_LOOKUP	= 10,
	QRTR_TYPE_DEL_LOOKUP	= 11,
};

#define QRTR_TYPE_DEL_PROC	13

struct qrtr_ctrl_pkt {
	__le32 cmd;

	union {
		struct {
			__le32 service;
			__le32 instance;
			__le32 node;
			__le32 port;
		} server;

		struct {
			__le32 node;
			__le32 port;
		} client;

		struct {
			__le32 rsvd;
			__le32 node;
		} proc;
	};
} __packed;

struct sk_buff;

/* endpoint node id auto assignment */
#define QRTR_EP_NID_AUTO (-1)

/**
 * struct qrtr_endpoint - endpoint handle
 * @xmit: Callback for outgoing packets
 *
 * The socket buffer passed to the xmit function becomes owned by the endpoint
 * driver.  As such, when the driver is done with the buffer, it should
 * call kfree_skb() on failure, or consume_skb() on success.
 */
struct qrtr_endpoint {
	int (*xmit)(struct qrtr_endpoint *ep, struct sk_buff *skb);
	/* private: not for endpoint use */
	struct qrtr_node *node;
};

int qrtr_endpoint_register(struct qrtr_endpoint *ep, unsigned int nid);

void qrtr_endpoint_unregister(struct qrtr_endpoint *ep);

int qrtr_endpoint_post(struct qrtr_endpoint *ep, const void *data, size_t len);

#endif
