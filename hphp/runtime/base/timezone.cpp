/*
   +----------------------------------------------------------------------+
   | HipHop for PHP                                                       |
   +----------------------------------------------------------------------+
   | Copyright (c) 2010-2014 Facebook, Inc. (http://www.facebook.com)     |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
*/

#include "hphp/runtime/base/timezone.h"

#include "hphp/runtime/base/array-init.h"
#include "hphp/runtime/base/builtin-functions.h"
#include "hphp/runtime/base/datetime.h"
#include "hphp/runtime/base/execution-context.h"
#include "hphp/runtime/base/runtime-error.h"
#include "hphp/runtime/base/type-conversions.h"

#include "hphp/util/logger.h"
#include "hphp/util/text-util.h"

namespace HPHP {

IMPLEMENT_OBJECT_ALLOCATION(TimeZone)
///////////////////////////////////////////////////////////////////////////////

class GuessedTimeZone {
public:
  std::string m_tzid;
  std::string m_warning;

  GuessedTimeZone() {
    time_t the_time = time(0);
    struct tm tmbuf;
    struct tm *ta = localtime_r(&the_time, &tmbuf);
    const char *tzid = nullptr;
    if (ta) {
      tzid = timelib_timezone_id_from_abbr(ta->tm_zone, ta->tm_gmtoff,
                                           ta->tm_isdst);
    }
    if (!tzid) {
      tzid = "UTC";
    }
    m_tzid = tzid;

#define DATE_TZ_ERRMSG \
  "It is not safe to rely on the system's timezone settings. Please use " \
  "the date.timezone setting, the TZ environment variable or the " \
  "date_default_timezone_set() function. In case you used any of those " \
  "methods and you are still getting this warning, you most likely " \
  "misspelled the timezone identifier. "

    string_printf(m_warning, DATE_TZ_ERRMSG
                  "We selected '%s' for '%s/%.1f/%s' instead",
                  tzid, ta ? ta->tm_zone : "Unknown",
                  ta ? (float) (ta->tm_gmtoff / 3600) : 0,
                  ta ? (ta->tm_isdst ? "DST" : "no DST") : "Unknown");
  }
};
static GuessedTimeZone s_guessed_timezone;

///////////////////////////////////////////////////////////////////////////////
// statics

class TimeZoneData {
public:
  TimeZoneData() : Database(nullptr) {}

  const timelib_tzdb *Database;
  MapStringToTimeZoneInfo Cache;
};
static IMPLEMENT_THREAD_LOCAL(TimeZoneData, s_timezone_data);

const timelib_tzdb *TimeZone::GetDatabase() {
  const timelib_tzdb *&Database = s_timezone_data->Database;
  if (Database == nullptr) {
    Database = timelib_builtin_db();
  }
  return Database;
}

TimeZoneInfo TimeZone::GetTimeZoneInfo(char* name, const timelib_tzdb* db) {
  MapStringToTimeZoneInfo &Cache = s_timezone_data->Cache;

  MapStringToTimeZoneInfo::const_iterator iter = Cache.find(name);
  if (iter != Cache.end()) {
    return iter->second;
  }

  timelib_time *t = timelib_time_ctor();
  int dst, not_found;
  auto tmp = strdup(name);
  t->z = timelib_parse_zone(&tmp, &dst, t, &not_found, db, timelib_parse_tzfile);
  free(tmp);

  TimeZoneInfoWrap *wrap;
  if (!not_found) {
    wrap = (TimeZoneInfoWrap*) calloc(1, sizeof(TimeZoneInfoWrap));
    wrap->type = t->zone_type;
    switch (t->zone_type) {
      case TIMELIB_ZONETYPE_ID:
        wrap->tzi.tz = t->tz_info;
        break;
      case TIMELIB_ZONETYPE_OFFSET:
        wrap->tzi.utc_offset = t->z;
        break;
      case TIMELIB_ZONETYPE_ABBR:
        wrap->tzi.abbr.dst = t->dst;
        wrap->tzi.abbr.abbr = strdup(t->tz_abbr);
        wrap->tzi.abbr.utc_offset = t->z;
        break;
    }
  }

  timelib_time_dtor(t);

  TimeZoneInfo tzi(wrap, tzinfo_deleter());
  if (tzi) {
    Cache[name] = tzi;
  }
  return tzi;
}

timelib_tzinfo* TimeZone::GetTimeZoneInfoRaw(char* name,
                                             const timelib_tzdb* db) {
  TimeZoneInfoWrap *wrap = GetTimeZoneInfo(name, db).get();
  if (wrap) {
    return wrap->tzi.tz;
  }
  return NULL;
}

bool TimeZone::IsValid(const String& name) {
  return timelib_timezone_id_is_valid((char*)name.data(), GetDatabase());
}

String TimeZone::CurrentName() {
  /* Checking configure timezone */
  String timezone = g_context->getTimeZone();
  if (!timezone.empty()) {
    return timezone;
  }

  /* Check environment variable */
  char *env = getenv("TZ");
  if (env && *env && IsValid(env)) {
    return String(env, CopyString);
  }

  /* Check config setting for default timezone */
  String default_timezone = g_context->getDefaultTimeZone();
  if (!default_timezone.empty() && IsValid(default_timezone.data())) {
    return default_timezone;
  }

  /* Try to guess timezone from system information */
  raise_strict_warning(s_guessed_timezone.m_warning);
  return String(s_guessed_timezone.m_tzid);
}

SmartResource<TimeZone> TimeZone::Current() {
  return NEWOBJ(TimeZone)(CurrentName());
}

bool TimeZone::SetCurrent(const String& zone) {
  if (!IsValid(zone)) {
    raise_notice("Timezone ID '%s' is invalid", zone.data());
    return false;
  }
  g_context->setTimeZone(zone);
  return true;
}

Array TimeZone::GetNames() {
  const timelib_tzdb *tzdb = timelib_builtin_db();
  int item_count = tzdb->index_size;
  const timelib_tzdb_index_entry *table = tzdb->index;

  Array ret;
  for (int i = 0; i < item_count; ++i) {
    ret.append(String(table[i].id, CopyString));
  }
  return ret;
}

const StaticString
  s_dst("dst"),
  s_offset("offset"),
  s_timezone_id("timezone_id"),
  s_ts("ts"),
  s_time("time"),
  s_isdst("isdst"),
  s_abbr("abbr"),
  s_country_code("country_code"),
  s_latitude("latitude"),
  s_longitude("longitude"),
  s_comments("comments");

Array TimeZone::GetAbbreviations() {
  Array ret;
  for (const timelib_tz_lookup_table *entry =
         timelib_timezone_abbreviations_list(); entry->name; entry++) {
    ArrayInit element(3, ArrayInit::Map{});
    element.set(s_dst, (bool)entry->type);
    element.set(s_offset, entry->gmtoffset);
    if (entry->full_tz_name) {
      element.set(s_timezone_id, String(entry->full_tz_name, CopyString));
    } else {
      element.set(s_timezone_id, uninit_null());
    }
    auto& val = ret.lvalAt(String(entry->name));
    forceToArray(val).append(element.create());
  }
  return ret;
}

String TimeZone::AbbreviationToName(String abbr, int utcoffset /* = -1 */,
                                    bool isdst /* = true */) {
  return String(timelib_timezone_id_from_abbr(abbr.data(), utcoffset,
                                              isdst ? -1 : 0),
                CopyString);
}

///////////////////////////////////////////////////////////////////////////////
// class TimeZone

TimeZone::TimeZone() {
  m_tzi = TimeZoneInfo();
}

TimeZone::TimeZone(const String& name) {
  m_tzi = GetTimeZoneInfo((char*)name.data(), GetDatabase());
}

TimeZone::TimeZone(timelib_tzinfo *tzi) {
  
}

SmartResource<TimeZone> TimeZone::cloneTimeZone() const {
  if (!m_tzi) return NEWOBJ(TimeZone)();
  return NEWOBJ(TimeZone)(timelib_tzinfo_clone(get()));
}

String TimeZone::name() const {
  if (!m_tzi) return String();
  switch (m_tzi->type) {
    case TIMELIB_ZONETYPE_ID:
      return String(m_tzi->tzi.tz->name, CopyString);
    case TIMELIB_ZONETYPE_OFFSET:
      {
      char *tmpstr = (char*) malloc(sizeof("UTC+05:00"));
      timelib_sll utc_offset = m_tzi->tzi.utc_offset;
      snprintf(tmpstr, sizeof("+05:00"), "%c%02d:%02d",
               utc_offset > 0 ? '-' : '+',
               abs(utc_offset / 60),
               abs((utc_offset % 60)));
      return String(tmpstr);
      }
    case TIMELIB_ZONETYPE_ABBR:
      return String(m_tzi->tzi.abbr.abbr, CopyString);
  }
  return String();
}

String TimeZone::abbr(int type /* = 0 */) const {
  if (!m_tzi) return String();
  switch (m_tzi->type) {
    case TIMELIB_ZONETYPE_ID:
      {
      timelib_tzinfo *tz = get(); 
      return String(&tz->timezone_abbr[tz->type[type].abbr_idx], CopyString);
      }
    case TIMELIB_ZONETYPE_OFFSET:
    case TIMELIB_ZONETYPE_ABBR:
      return name();
  }
  return String();
}

int TimeZone::offset(int timestamp) const {
  if (!m_tzi) return 0;
  switch (m_tzi->type) {
    case TIMELIB_ZONETYPE_ID:
      {
      timelib_time_offset *offset =
        timelib_get_time_zone_info(timestamp, get());
      int ret = offset->offset;
      timelib_time_offset_dtor(offset);
      return ret;
      }
    case TIMELIB_ZONETYPE_OFFSET:
      return m_tzi->tzi.utc_offset * -60;
    case TIMELIB_ZONETYPE_ABBR:
      return (m_tzi->tzi.abbr.utc_offset - (m_tzi->tzi.abbr.dst*60)) * -60;
  }
  return 0;
}

bool TimeZone::dst(int timestamp) const {
  if (!m_tzi) return false;

  switch (m_tzi->type) {
    case TIMELIB_ZONETYPE_ID:
      {
      timelib_time_offset *offset =
        timelib_get_time_zone_info(timestamp, get());
      bool ret = offset->is_dst;
      timelib_time_offset_dtor(offset);
      return ret;
      }
    case TIMELIB_ZONETYPE_OFFSET:
      return false;
    case TIMELIB_ZONETYPE_ABBR:
      return m_tzi->tzi.abbr.dst == 1;
  }
  return false;
}

Array TimeZone::transitions() const {
  Array ret;
  if (m_tzi && m_tzi->type == TIMELIB_ZONETYPE_ID) {
    timelib_tzinfo *tz = get();
    for (unsigned int i = 0; i < tz->timecnt; ++i) {
      int index = tz->trans_idx[i];
      int timestamp = tz->trans[i];
      DateTime dt(timestamp);
      ttinfo &offset = tz->type[index];
      const char *abbr = tz->timezone_abbr + offset.abbr_idx;

      ret.append(make_map_array(
        s_ts, timestamp,
        s_time, dt.toString(DateTime::DateFormat::ISO8601),
        s_offset, offset.offset,
        s_isdst, (bool)offset.isdst,
        s_abbr, String(abbr, CopyString)
      ));
    }
  }
  return ret;
}

Array TimeZone::getLocation() const {
  Array ret;
  if (!m_tzi || m_tzi->type != TIMELIB_ZONETYPE_ID) return ret;

#ifdef TIMELIB_HAVE_TZLOCATION
  timelib_tzinfo *tz = get();
  ret.set(s_country_code, String(tz->location.country_code, CopyString));
  ret.set(s_latitude,     tz->location.latitude);
  ret.set(s_longitude,    tz->location.longitude);
  ret.set(s_comments,     String(tz->location.comments, CopyString));
#else
  throw NotImplementedException("timelib version too old");
#endif

  return ret;
}

///////////////////////////////////////////////////////////////////////////////
}
