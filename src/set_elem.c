/*
 * (C) 2012-2013 by Pablo Neira Ayuso <pablo@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This code has been sponsored by Sophos Astaro <http://www.sophos.com>
 */
#include "internal.h"

#include <time.h>
#include <endian.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>

#include <libmnl/libmnl.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nf_tables.h>

#include <libnftables/set.h>

#include "linux_list.h"
#include "expr/data_reg.h"

struct nft_set_elem *nft_set_elem_alloc(void)
{
	struct nft_set_elem *s;

	s = calloc(1, sizeof(struct nft_set_elem));
	if (s == NULL)
		return NULL;

	return s;
}
EXPORT_SYMBOL(nft_set_elem_alloc);

void nft_set_elem_free(struct nft_set_elem *s)
{
	free(s);
}
EXPORT_SYMBOL(nft_set_elem_free);

bool nft_set_elem_attr_is_set(struct nft_set_elem *s, uint16_t attr)
{
	return s->flags & (1 << attr);
}
EXPORT_SYMBOL(nft_set_elem_attr_is_set);

void nft_set_elem_attr_unset(struct nft_set_elem *s, uint16_t attr)
{
	switch (attr) {
	case NFT_SET_ELEM_ATTR_CHAIN:
		if (s->flags & (1 << NFT_SET_ELEM_ATTR_CHAIN)) {
			if (s->data.chain) {
				free(s->data.chain);
				s->data.chain = NULL;
			}
		}
		break;
	case NFT_SET_ELEM_ATTR_FLAGS:
	case NFT_SET_ELEM_ATTR_KEY:	/* NFTA_SET_ELEM_KEY */
	case NFT_SET_ELEM_ATTR_VERDICT:	/* NFTA_SET_ELEM_DATA */
	case NFT_SET_ELEM_ATTR_DATA:	/* NFTA_SET_ELEM_DATA */
		break;
	default:
		return;
	}

	s->flags &= ~(1 << attr);
}
EXPORT_SYMBOL(nft_set_elem_attr_unset);

void nft_set_elem_attr_set(struct nft_set_elem *s, uint16_t attr,
			   const void *data, size_t data_len)
{
	switch(attr) {
	case NFT_SET_ELEM_ATTR_FLAGS:
		s->set_elem_flags = *((uint32_t *)data);
		break;
	case NFT_SET_ELEM_ATTR_KEY:	/* NFTA_SET_ELEM_KEY */
		memcpy(&s->key.val, data, data_len);
		s->key.len = data_len;
		break;
	case NFT_SET_ELEM_ATTR_VERDICT:	/* NFTA_SET_ELEM_DATA */
		s->data.verdict = *((uint32_t *)data);
		break;
	case NFT_SET_ELEM_ATTR_CHAIN:	/* NFTA_SET_ELEM_DATA */
		if (s->data.chain)
			free(s->data.chain);

		s->data.chain = strdup(data);
		break;
	case NFT_SET_ELEM_ATTR_DATA:	/* NFTA_SET_ELEM_DATA */
		memcpy(s->data.val, data, data_len);
		s->data.len = data_len;
		break;
	default:
		return;
	}
	s->flags |= (1 << attr);
}
EXPORT_SYMBOL(nft_set_elem_attr_set);

void nft_set_elem_attr_set_u32(struct nft_set_elem *s, uint16_t attr, uint32_t val)
{
	nft_set_elem_attr_set(s, attr, &val, sizeof(uint32_t));
}
EXPORT_SYMBOL(nft_set_elem_attr_set_u32);

void *nft_set_elem_attr_get(struct nft_set_elem *s, uint16_t attr, size_t *data_len)
{
	switch(attr) {
	case NFT_SET_ELEM_ATTR_FLAGS:
		if (s->flags & (1 << NFT_SET_ELEM_ATTR_FLAGS))
			return &s->set_elem_flags;
		break;
	case NFT_SET_ELEM_ATTR_KEY:	/* NFTA_SET_ELEM_KEY */
		if (s->flags & (1 << NFT_SET_ELEM_ATTR_KEY)) {
			*data_len = s->key.len;
			return &s->key.val;
		}
		break;
	case NFT_SET_ELEM_ATTR_VERDICT:	/* NFTA_SET_ELEM_DATA */
		if (s->flags & (1 << NFT_SET_ELEM_ATTR_VERDICT))
			return &s->data.verdict;
		break;
	case NFT_SET_ELEM_ATTR_CHAIN:	/* NFTA_SET_ELEM_DATA */
		if (s->flags & (1 << NFT_SET_ELEM_ATTR_CHAIN))
			return &s->data.chain;
		break;
	case NFT_SET_ELEM_ATTR_DATA:	/* NFTA_SET_ELEM_DATA */
		if (s->flags & (1 << NFT_SET_ELEM_ATTR_DATA)) {
			*data_len = s->data.len;
			return &s->data.val;
		}
		break;
	default:
		break;
	}
	return NULL;
}
EXPORT_SYMBOL(nft_set_elem_attr_get);

const char *nft_set_elem_attr_get_str(struct nft_set_elem *s, uint16_t attr)
{
	size_t size;

	return nft_set_elem_attr_get(s, attr, &size);
}
EXPORT_SYMBOL(nft_set_elem_attr_get_str);

uint32_t nft_set_elem_attr_get_u32(struct nft_set_elem *s, uint16_t attr)
{
	size_t size;
	uint32_t val = *((uint32_t *)nft_set_elem_attr_get(s, attr, &size));
	return val;
}
EXPORT_SYMBOL(nft_set_elem_attr_get_u32);

struct nlmsghdr *
nft_set_elem_nlmsg_build_hdr(char *buf, uint16_t cmd, uint16_t family,
			     uint16_t type, uint32_t seq)
{
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfh;

	nlh = mnl_nlmsg_put_header(buf);
	nlh->nlmsg_type = (NFNL_SUBSYS_NFTABLES << 8) | cmd;
	nlh->nlmsg_flags = NLM_F_REQUEST | type;
	nlh->nlmsg_seq = seq;

	nfh = mnl_nlmsg_put_extra_header(nlh, sizeof(struct nfgenmsg));
	nfh->nfgen_family = family;
	nfh->version = NFNETLINK_V0;
	nfh->res_id = 0;

	return nlh;
}
EXPORT_SYMBOL(nft_set_elem_nlmsg_build_hdr);

void nft_set_elem_nlmsg_build_payload(struct nlmsghdr *nlh,
				      struct nft_set_elem *e)
{
	if (e->flags & (1 << NFT_SET_ELEM_ATTR_FLAGS))
		mnl_attr_put_u32(nlh, NFTA_SET_ELEM_FLAGS, htonl(e->set_elem_flags));
	if (e->flags & (1 << NFT_SET_ELEM_ATTR_KEY)) {
		struct nlattr *nest1;

		nest1 = mnl_attr_nest_start(nlh, NFTA_SET_ELEM_KEY);
		mnl_attr_put(nlh, NFTA_DATA_VALUE, e->key.len, e->key.val);
		mnl_attr_nest_end(nlh, nest1);
	}
	if (e->flags & (1 << NFT_SET_ELEM_ATTR_VERDICT)) {
		struct nlattr *nest1, *nest2;

		nest1 = mnl_attr_nest_start(nlh, NFTA_SET_ELEM_DATA);
		nest2 = mnl_attr_nest_start(nlh, NFTA_DATA_VERDICT);
		mnl_attr_put_u32(nlh, NFTA_VERDICT_CODE, htonl(e->data.verdict));
		if (e->flags & (1 << NFT_SET_ELEM_ATTR_CHAIN))
			mnl_attr_put_strz(nlh, NFTA_VERDICT_CHAIN, e->data.chain);

		mnl_attr_nest_end(nlh, nest1);
		mnl_attr_nest_end(nlh, nest2);
	}
	if (e->flags & (1 << NFT_SET_ELEM_ATTR_DATA)) {
		struct nlattr *nest1;

		nest1 = mnl_attr_nest_start(nlh, NFTA_SET_ELEM_DATA);
		mnl_attr_put(nlh, NFTA_DATA_VALUE, e->data.len, e->data.val);
		mnl_attr_nest_end(nlh, nest1);
	}
}

void nft_set_elems_nlmsg_build_payload(struct nlmsghdr *nlh, struct nft_set *s)
{
	struct nft_set_elem *elem;
	struct nlattr *nest1;
	int i = 0;

	if (s->flags & (1 << NFT_SET_ATTR_NAME))
		mnl_attr_put_strz(nlh, NFTA_SET_ELEM_LIST_SET, s->name);
	if (s->flags & (1 << NFT_SET_ATTR_TABLE))
		mnl_attr_put_strz(nlh, NFTA_SET_ELEM_LIST_TABLE, s->table);

	nest1 = mnl_attr_nest_start(nlh, NFTA_SET_ELEM_LIST_ELEMENTS);
	list_for_each_entry(elem, &s->element_list, head) {
		struct nlattr *nest2;

		nest2 = mnl_attr_nest_start(nlh, ++i);
		nft_set_elem_nlmsg_build_payload(nlh, elem);
		mnl_attr_nest_end(nlh, nest2);
	}
	mnl_attr_nest_end(nlh, nest1);
}
EXPORT_SYMBOL(nft_set_elems_nlmsg_build_payload);

static int nft_set_elem_parse_attr_cb(const struct nlattr *attr, void *data)
{
	const struct nlattr **tb = data;
	int type = mnl_attr_get_type(attr);

	if (mnl_attr_type_valid(attr, NFTA_SET_MAX) < 0)
		return MNL_CB_OK;

	switch(type) {
	case NFTA_SET_ELEM_FLAGS:
		if (mnl_attr_validate(attr, MNL_TYPE_U32) < 0) {
			perror("mnl_attr_validate");
			return MNL_CB_ERROR;
		}
		break;
	case NFTA_SET_ELEM_KEY:
	case NFTA_SET_ELEM_DATA:
		if (mnl_attr_validate(attr, MNL_TYPE_NESTED) < 0) {
			perror("mnl_attr_validate");
			return MNL_CB_ERROR;
		}
		break;
	}

	tb[type] = attr;
	return MNL_CB_OK;
}

static int nft_set_elems_parse2(struct nft_set *s, const struct nlattr *nest)
{
	struct nlattr *tb[NFTA_SET_ELEM_MAX+1] = {};
	struct nft_set_elem *e;
	int ret = 0, type;

	e = nft_set_elem_alloc();
	if (e == NULL)
		return -1;

	mnl_attr_parse_nested(nest, nft_set_elem_parse_attr_cb, tb);
	if (tb[NFTA_SET_ELEM_FLAGS]) {
		e->set_elem_flags =
			ntohl(mnl_attr_get_u32(tb[NFTA_SET_ELEM_FLAGS]));
		e->flags |= (1 << NFT_SET_ELEM_ATTR_KEY);
	}
        if (tb[NFTA_SET_ELEM_KEY]) {
		ret = nft_parse_data(&e->key, tb[NFTA_SET_ELEM_KEY], &type);
		e->flags |= (1 << NFT_SET_ELEM_ATTR_KEY);
        }
        if (tb[NFTA_SET_ELEM_DATA]) {
		ret = nft_parse_data(&e->data, tb[NFTA_SET_ELEM_DATA], &type);
		switch(type) {
		case DATA_VERDICT:
			s->flags |= (1 << NFT_SET_ELEM_ATTR_VERDICT);
			break;
		case DATA_CHAIN:
			s->flags |= (1 << NFT_SET_ELEM_ATTR_CHAIN);
			break;
		case DATA_VALUE:
			s->flags |= (1 << NFT_SET_ELEM_ATTR_DATA);
			break;
		}
        }
	if (ret < 0)
		free(e);

	/* Add this new element to this set */
	list_add_tail(&e->head, &s->element_list);

	return ret;
}
EXPORT_SYMBOL(nft_set_elem_nlmsg_parse);

static int
nft_set_elem_list_parse_attr_cb(const struct nlattr *attr, void *data)
{
	const struct nlattr **tb = data;
	int type = mnl_attr_get_type(attr);

	if (mnl_attr_type_valid(attr, NFTA_SET_ELEM_LIST_MAX) < 0)
		return MNL_CB_OK;

	switch(type) {
	case NFTA_SET_ELEM_LIST_TABLE:
	case NFTA_SET_ELEM_LIST_SET:
		if (mnl_attr_validate(attr, MNL_TYPE_STRING) < 0) {
			perror("mnl_attr_validate");
			return MNL_CB_ERROR;
		}
		break;
	case NFTA_SET_ELEM_LIST_ELEMENTS:
		if (mnl_attr_validate(attr, MNL_TYPE_NESTED) < 0) {
			perror("mnl_attr_validate");
			return MNL_CB_ERROR;
		}
		break;
	}

	tb[type] = attr;
	return MNL_CB_OK;
}

static int nft_set_elems_parse(struct nft_set *s, const struct nlattr *nest)
{
	struct nlattr *attr;
	int ret = 0;

	mnl_attr_for_each_nested(attr, nest) {
		if (mnl_attr_get_type(attr) != NFTA_LIST_ELEM)
			return -1;

		ret = nft_set_elems_parse2(s, attr);
	}
	return ret;
}

int nft_set_elems_nlmsg_parse(const struct nlmsghdr *nlh, struct nft_set *s)
{
	struct nlattr *tb[NFTA_SET_ELEM_LIST_MAX+1] = {};
	struct nfgenmsg *nfg = mnl_nlmsg_get_payload(nlh);
	int ret = 0;

	mnl_attr_parse(nlh, sizeof(*nfg), nft_set_elem_list_parse_attr_cb, tb);
	if (tb[NFTA_SET_ELEM_LIST_TABLE]) {
		s->table =
			strdup(mnl_attr_get_str(tb[NFTA_SET_ELEM_LIST_TABLE]));
		s->flags |= (1 << NFT_SET_ATTR_TABLE);
	}
	if (tb[NFTA_SET_ELEM_LIST_SET]) {
		s->name =
			strdup(mnl_attr_get_str(tb[NFTA_SET_ELEM_LIST_SET]));
		s->flags |= (1 << NFT_SET_ATTR_NAME);
	}
        if (tb[NFTA_SET_ELEM_LIST_ELEMENTS])
	 	ret = nft_set_elems_parse(s, tb[NFTA_SET_ELEM_LIST_ELEMENTS]);

	return ret;
}
EXPORT_SYMBOL(nft_set_elems_nlmsg_parse);

int nft_set_elem_snprintf(char *buf, size_t size, struct nft_set_elem *e,
			  uint32_t type, uint32_t flags)
{
	int ret, len = size, offset = 0, i;

	ret = snprintf(buf, size, "flags=%u key=", e->set_elem_flags);
	SNPRINTF_BUFFER_SIZE(ret, size, len, offset);

	for (i=0; i<e->key.len/sizeof(uint32_t); i++) {
		ret = snprintf(buf+offset, len, "%.8x ", e->key.val[i]);
		SNPRINTF_BUFFER_SIZE(ret, size, len, offset);
	}

	ret = snprintf(buf+offset, size, "data=");
	SNPRINTF_BUFFER_SIZE(ret, size, len, offset);

	for (i=0; i<e->data.len/sizeof(uint32_t); i++) {
		ret = snprintf(buf+offset, len, "%.8x ", e->data.val[i]);
		SNPRINTF_BUFFER_SIZE(ret, size, len, offset);
	}

	return offset;
}
EXPORT_SYMBOL(nft_set_elem_snprintf);

int nft_set_elem_foreach(struct nft_set *s,
			 int (*cb)(struct nft_set_elem *e, void *data),
			 void *data)
{
	struct nft_set_elem *elem;
	int ret;

	list_for_each_entry(elem, &s->element_list, head) {
		ret = cb(elem, data);
		if (ret < 0)
			return ret;
	}
	return 0;
}
EXPORT_SYMBOL(nft_set_elem_foreach);

struct nft_set_elems_iter {
	struct list_head		*list;
	struct nft_set_elem		*cur;
};

struct nft_set_elems_iter *nft_set_elems_iter_create(struct nft_set *s)
{
	struct nft_set_elems_iter *iter;

	iter = calloc(1, sizeof(struct nft_set_elems_iter));
	if (iter == NULL)
		return NULL;

	iter->list = &s->element_list;
	iter->cur = list_entry(s->element_list.next, struct nft_set_elem, head);

	return iter;
}
EXPORT_SYMBOL(nft_set_elems_iter_create);

struct nft_set_elem *nft_set_elems_iter_cur(struct nft_set_elems_iter *iter)
{
	return iter->cur;
}
EXPORT_SYMBOL(nft_set_elems_iter_cur);

struct nft_set_elem *nft_set_elems_iter_next(struct nft_set_elems_iter *iter)
{
	struct nft_set_elem *s = iter->cur;

	iter->cur = list_entry(iter->cur->head.next, struct nft_set_elem, head);
	if (&iter->cur->head == iter->list->next)
		return NULL;

	return s;
}
EXPORT_SYMBOL(nft_set_elems_iter_next);

void nft_set_elems_iter_destroy(struct nft_set_elems_iter *iter)
{
	free(iter);
}
EXPORT_SYMBOL(nft_set_elems_iter_destroy);
