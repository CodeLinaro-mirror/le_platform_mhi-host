/* SPDX-License-Identifier: GPL-2.0-or-later */
/* MHI Network driver - Network over MHI bus
 *
 * Copyright (C) 2021 Linaro Ltd <loic.poulain@linaro.org>
 */

#if BITS_PER_LONG == 64
#include <asm/local64.h>

typedef struct {
	local64_t       v;
} u64_stats_t ;

static inline u64 u64_stats_read(const u64_stats_t *p)
{
	return local64_read(&p->v);
}

static inline void u64_stats_add(u64_stats_t *p, unsigned long val)
{
	local64_add(val, &p->v);
}

static inline void u64_stats_inc(u64_stats_t *p)
{
	local64_inc(&p->v);
}
#else
typedef struct {
	u64             v;
} u64_stats_t;

static inline u64 u64_stats_read(const u64_stats_t *p)
{
	return p->v;
}

static inline void u64_stats_add(u64_stats_t *p, unsigned long val)
{
	p->v += val;
}

static inline void u64_stats_inc(u64_stats_t *p)
{
	p->v++;
}
#endif

struct mhi_net_stats {
	u64_stats_t rx_packets;
	u64_stats_t rx_bytes;
	u64_stats_t rx_errors;
	u64_stats_t rx_dropped;
	u64_stats_t rx_length_errors;
	u64_stats_t tx_packets;
	u64_stats_t tx_bytes;
	u64_stats_t tx_errors;
	u64_stats_t tx_dropped;
	struct u64_stats_sync tx_syncp;
	struct u64_stats_sync rx_syncp;
};

struct mhi_net_dev {
	struct mhi_device *mdev;
	struct net_device *ndev;
	struct sk_buff *skbagg_head;
	struct sk_buff *skbagg_tail;
	const struct mhi_net_proto *proto;
	void *proto_data;
	struct delayed_work rx_refill;
	struct mhi_net_stats stats;
	u32 rx_queue_sz;
	int msg_enable;
	unsigned int mru;
};

struct mhi_net_proto {
	int (*init)(struct mhi_net_dev *mhi_netdev);
	struct sk_buff * (*tx_fixup)(struct mhi_net_dev *mhi_netdev, struct sk_buff *skb);
	void (*rx)(struct mhi_net_dev *mhi_netdev, struct sk_buff *skb);
};

extern const struct mhi_net_proto proto_mbim;
