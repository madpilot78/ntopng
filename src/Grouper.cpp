/*
 *
 * (C) 2015-18 - ntop.org
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "ntop_includes.h"

#define FNV_32_PRIME ((u_int32_t)0x01000193)
#define FNV1_32A_INIT ((u_int32_t)0x811c9dc5)

/* *************************************** */

Grouper::Grouper(sortField sf){
  sorter = sf;
  numGroups = 0;
  for(int32_t i = 0; i < HASH_SIZE; i++)
    hash[i] = NULL;
}

/* *************************************** */

Grouper::~Grouper(){
  group *n;

  for(int32_t i = 0; i < HASH_SIZE; i++) {
    while(hash[i]) {
      n = hash[i]->next;
      if(sorter == column_country || sorter == column_os)
        free(hash[i]->group_id.s);
      if(hash[i]->group_label)
        free(hash[i]->group_label);
      free(hash[i]);
      hash[i] = n;
    }
  }
}
/* *************************************** */

/*
 * use FNV hash for strings XXX WIP
 */
u_int32_t fnv_32a(char *str)
{
  unsigned char *s = (unsigned char *)str;
  u_int32_t hval = FNV1_32A_INIT;

  while (*s) {
    hval ^= (u_int32_t)*s++;
    hval *= FNV_32_PRIME;
  }

  return hval % HASH_SIZE;
}

/*
 * very simplistic integer hash
 */
u_int32_t int_hash(int64_t n)
{
  return (u_int32_t)n % HASH_SIZE;
}

/* *************************************** */

/*
 * Comparator callback for group_id.i
 */
int id_i_comparator(const void *_a, const void *_b) {
  const group *a = *(const group **)_a;
  const group *b = *(const group **)_b;

  if(a->group_id.i < b->group_id.i)
    return -1;

  if(a->group_id.i == b->group_id.i)
    return 0;

//  if(a->group_id.i > b->group_id.i)
  return 1;
}

/*
 * Comparator callback for group_id.s
 */
int id_s_comparator(const void *_a, const void *_b) {
  const group *a = *(const group **)_a;
  const group *b = *(const group **)_b;

  return strcmp(a->group_id.s, b->group_id.s);
}

/**
 * Returns group index. Calls newGroup() if no matching group is found.
 *
 * Returns -1 for unsupported sorting criteria, causes host to be skipped.
 */
group * Grouper::inGroup(Host *h) {
  group *k, *m;

  if(h == NULL)
    return NULL;

  k = newGroup(h);
  if(k == NULL)
    return NULL;

  switch(sorter) {
    case column_asn:
    case column_vlan:
    case column_local_network:
    case column_local_network_id:
    case column_pool_id:
    case column_mac:
      m = hash[int_hash(k->group_id.i)]
      while(m) {
        if(m->group_id.i == k->group_id.i) {
          return m;
        }
        m = m->next;
      }
      break;

    case column_country:
    case column_os:
      m = hash[fnv_32a(k->group_id.s)]
      while(m) {
        if(strcmp(m->group_id.s, k->group_id.s) == 0) {
          return m;
        }
        m = m->next;
      }
      break;

    default:
      // Abort on unknown criteria
      free(k);
      return NULL;
      break;
  }

  // No existing group matched, add a new one
  return addGroup(k);
}

/* *************************************** */

/**
 * Creates a new group.
 *
 * Returns a pointer to a new group struct
 */
group * Grouper::newGroup(Host *h) {
  char buf[32];
  group *g;

  if(h == NULL)
    return NULL;

  g = (group *)malloc(sizeof(struct group));
  if(g == NULL) {
    ntop->getTrace()->traceEvent(TRACE_WARNING, "Not enough memory");
    return NULL;
  }
  memset(g, 0, sizeof(struct group));

  switch(sorter) {
  case column_asn:
    g->group_id.i = h->get_asn();
    g->group_label = strdup(h->get_asname() != NULL ? h->get_asname() : (char*)UNKNOWN_ASN);
    break;

  case column_vlan:
    g->group_id.i = h->get_vlan_id();
    sprintf(buf, "%i", h->get_vlan_id());
    g->group_label = strdup(buf);
    break;

  case column_local_network:
  case column_local_network_id:
    g->group_id.i = h->get_local_network_id();
    if(g->group_id.i >= 0)
      g->group_label = strdup(ntop->getLocalNetworkName(h->get_local_network_id()));
    else
      g->group_label = strdup((char*)UNKNOWN_LOCAL_NETWORK);
    break;

  case column_pool_id:
    g->group_id.i = h->get_host_pool();
    sprintf(buf, "%i", h->get_host_pool());
    g->group_label = strdup(buf);
    break;

  case column_mac:
    g->group_id.i = Utils::macaddr_int(h->get_mac());
    g->group_label = strdup(Utils::formatMac(h->get_mac(), buf, sizeof(buf)));
    break;

  case column_country:
    {
      char buf[32], *c = h->get_country(buf, sizeof(buf));
      
      g->group_id.s  = strdup(c);
      g->group_label = strdup(g->group_id.s);
    }
    break;

  case column_os:
    g->group_id.s  = strdup(h->get_os() ? h->get_os() : (char*)UNKNOWN_OS);
    g->group_label = strdup(g->group_id.s);
    break;

  default:
    free(g); // Avoid memory leak
    return NULL;
  };

  return g;
}

/* *************************************** */

group * Grouper::addGroup(group *g)
{
  group *pos = NULL;

  if(g == NULL)
    return NULL;

  switch(sorter) {
    case column_asn:
    case column_vlan:
    case column_local_network:
    case column_local_network_id:
    case column_pool_id:
    case column_mac:
      pos = hash[int_hash(k->group_id.i)];
      break;

    case column_country:
    case column_os:
      pos = hash[fnv_32a(k->group_id.i)];
      break;

    default:
      // Unknown criteria, abort operation
      free(g);
      return NULL;
      break;
  }

  g->next = pos;
  pos = g;
  numGroups++;

  return pos;
}

/* *************************************** */

/**
 * Increments stats in the correct group for host h, as reported by inGroup().
 */
int8_t Grouper::incStats(Host *h) {
  char buf[32], *c = h->get_country(buf, sizeof(buf));
  group *g;

  g = inGroup(h);

  if(h == NULL || g == NULL)
    return -1;

  g->stats.num_hosts++,
    g->stats.bytes_sent += h->getNumBytesSent(),
    g->stats.bytes_rcvd += h->getNumBytesRcvd(),
    g->stats.num_flows += h->getNumActiveFlows(),
    g->stats.num_dropped_flows += h->getNumDroppedFlows(),
    g->stats.num_alerts += h->getNumAlerts(),
    g->stats.throughput_bps += h->getBytesThpt(),
    g->stats.throughput_pps += h->getPacketsThpt(),
    g->stats.throughput_trend_bps_diff += h->getThptTrendDiff();

  if(g->stats.first_seen == 0 || h->get_first_seen() < g->stats.first_seen)
    g->stats.first_seen = h->get_first_seen();
  if(h->get_last_seen() > g->stats.last_seen)
    g->stats.last_seen = h->get_last_seen();

  if(c) {
    strncpy(g->stats.country, c, sizeof(g->stats.country));
    g->stats.country[sizeof(g->stats.country) - 1] = '\0';
  }

  return 0;
}

/* *************************************** */

/*
 * Recursively add groups to LUA tables.
 */
void Grouper::lua(lua_State* vm) {
  group *g;
  group **groups = (group **)calloc(numGroups, sozeof(group *));

  for(int32_t i = 0, j = 0; i < HASH_SIZE; i++) {
    g = hash[i];
    while(g) {
      groups[j++] = hash[i];
      g = hash[i]->next;
    }
  }

  switch(sorter) {
    case column_asn:
    case column_vlan:
    case column_local_network:
    case column_local_network_id:
    case column_pool_id:
    case column_mac:
      qsort(groups, numGroups, sizeof(group *), id_i_comparator);
      break;

    case column_country:
    case column_os:
      qsort(groups, numGroups, sizeof(group *), id_s_comparator);
      break;

    default:
      // Unknown criteria, return empty list
      return;
      break;
  }

  for(int32_t i = 0; i < numGroups; i++) {
    lua_newtable(vm);

    lua_push_str_table_entry(vm,   "name", groups[i]->group_label);
    lua_push_uint64_table_entry(vm,   "bytes.sent", groups[i]->stats.bytes_sent);
    lua_push_uint64_table_entry(vm,   "bytes.rcvd", groups[i]->stats.bytes_rcvd);
    lua_push_uint64_table_entry(vm,   "seen.first", groups[i]->stats.first_seen);
    lua_push_uint64_table_entry(vm,   "seen.last", groups[i]->stats.last_seen);
    lua_push_uint64_table_entry(vm,   "num_hosts", groups[i]->stats.num_hosts);
    lua_push_uint64_table_entry(vm,   "num_flows", groups[i]->stats.num_flows);
    lua_push_uint64_table_entry(vm,   "num_dropped_flows", groups[i]->stats.num_dropped_flows);
    lua_push_uint64_table_entry(vm,   "num_alerts", groups[i]->stats.num_alerts);
    lua_push_float_table_entry(vm, "throughput_bps", max_val(groups[i]->stats.throughput_bps, 0));
    lua_push_float_table_entry(vm, "throughput_pps", max_val(groups[i]->stats.throughput_pps, 0));
    lua_push_float_table_entry(vm, "throughput_trend_bps_diff", max_val(groups[i]->stats.throughput_trend_bps_diff, 0));
    lua_push_str_table_entry(vm,   "country", strlen(groups[i]->stats.country) ? groups[i]->stats.country : (char*)"");

    switch(sorter) {
      case column_mac:
        lua_push_str_table_entry(vm, "id", groups[i]->group_label);
        break;

      case column_asn:
      case column_vlan:
      case column_local_network:
      case column_local_network_id:
      case column_pool_id:
        lua_push_int32_table_entry(vm, "id", groups[i]->group_id.i);
        break;

      case column_country:
      case column_os:
        lua_push_str_table_entry(vm, "id", groups[i]->group_id.s);
        break;

      default:
        // Unknown criteria, use 'unknown' string
        lua_push_str_table_entry(vm, "id", "unknown");
        break;
    }

    lua_rawseti(vm, -2, i); /* Use indexes to preserve order */
  }
}
