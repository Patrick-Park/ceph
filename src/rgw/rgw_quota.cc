
#include "include/utime.h"
#include "common/lru_map.h"

#include "rgw_common.h"
#include "rgw_rados.h"
#include "rgw_quota.h"

#define dout_subsys ceph_subsys_rgw


int RGWBucketStatsCache::fetch_bucket_totals(rgw_bucket& bucket, RGWBucketStats& stats)
{
  RGWBucketInfo bucket_info;

  uint64_t bucket_ver;
  uint64_t master_ver;

  map<RGWObjCategory, RGWBucketStats> bucket_stats;
  int r = store->get_bucket_stats(bucket, &bucket_ver, &master_ver, bucket_stats, NULL);
  if (r < 0) {
    ldout(store->ctx(), 0) << "could not get bucket info for bucket=" << bucket.name << dendl;
    return r;
  }

  map<RGWObjCategory, RGWBucketStats>::iterator iter;
  for (iter = bucket_stats.begin(); iter != bucket_stats.end(); ++iter) {
    RGWBucketStats& s = iter->second;
    stats.num_kb += s.num_kb;
    stats.num_kb_rounded += s.num_kb_rounded;
    stats.num_objects += s.num_objects;
  }

  return 0;
}

int RGWBucketStatsCache::get_bucket_stats(rgw_bucket& bucket, RGWBucketStats& stats) {
  RGWQuotaBucketStats qs;
  if (stats_map.find(bucket, qs)) {
    if (qs.expiration > ceph_clock_now(store->ctx())) {
      stats = qs.stats;
      return 0;
    }
  }

  int ret = fetch_bucket_totals(bucket, qs.stats);
  if (ret < 0 && ret != -ENOENT)
    return ret;

  qs.expiration = ceph_clock_now(store->ctx()) + store->ctx()->_conf->rgw_bucket_quota_ttl;

  stats_map.add(bucket, qs);

  return 0;
}


void RGWBucketStatsCache::adjust_bucket_stats(rgw_bucket& bucket, int objs_delta, uint64_t added_bytes, uint64_t removed_bytes)
{
  RGWQuotaBucketStats qs;
  if (!stats_map.find(bucket, qs)) {
    /* not have cached info, can't update anything */
    return;
  }

  uint64_t rounded_kb_added = rgw_rounded_kb(added_bytes);
  uint64_t rounded_kb_removed = rgw_rounded_kb(removed_bytes);

  qs.stats.num_kb_rounded += (rounded_kb_added - rounded_kb_removed);
  qs.stats.num_kb += (added_bytes - removed_bytes) / 1024;
  qs.stats.num_objects += objs_delta;

  stats_map.add(bucket, qs);
}


