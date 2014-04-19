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

#ifndef incl_HPHP_TIMEZONE_H_
#define incl_HPHP_TIMEZONE_H_

#include "hphp/runtime/base/resource-data.h"
#include "hphp/runtime/base/type-string.h"

extern "C" {
#include <timelib.h>
#include <map>
#include <memory>
}

namespace HPHP {

class Array;
template <typename T> class SmartResource;

struct TimeZoneInfoWrap {
  int type;
  union {
    timelib_tzinfo    *tz;        /* TIMELIB_ZONETYPE_ID */
    timelib_sll       utc_offset; /* TIMELIB_ZONETYPE_OFFSET */
    timelib_abbr_info abbr;       /* TIMELIB_ZONETYPE_ABBR */
  } tzi;
};

typedef std::shared_ptr<TimeZoneInfoWrap> TimeZoneInfo;
typedef std::map<std::string, TimeZoneInfo> MapStringToTimeZoneInfo;
///////////////////////////////////////////////////////////////////////////////

/**
 * Handles all timezone related functions.
 */
class TimeZone : public SweepableResourceData {
public:
  DECLARE_RESOURCE_ALLOCATION(TimeZone);

  /**
   * Get/set current timezone that controls how local time is interpreted.
   */
  static String CurrentName();            // current timezone's name
  static SmartResource<TimeZone> Current(); // current timezone
  static bool SetCurrent(const String& name);   // returns false if invalid

  /**
   * TimeZone database queries.
   */
  static bool IsValid(const String& name);
  static Array GetNames();
  static Array GetAbbreviations();
  static String AbbreviationToName(String abbr, int utcoffset = -1,
                                   bool isdst = true);

public:
  /**
   * Constructing a timezone object by name or a raw pointer (internal).
   */
  TimeZone();
  explicit TimeZone(const String& name);
  explicit TimeZone(timelib_tzinfo *tzi);

  static StaticString& classnameof() {
    static StaticString result("TimeZone");
    return result;
  }
  // overriding ResourceData
  const String& o_getClassNameHook() const { return classnameof(); }

  /**
   * Whether this represents a valid timezone.
   */
  bool isValid() const { return m_tzi.get(); };

  /**
   * Get timezone's name or abbreviation.
   */
  String name() const;
  String abbr(int type = 0) const;

  /**
   * Get timezone's type.
   */
  int zoneType() const { return m_tzi->type; };

  /**
   * Get offset from UTC at the specified timestamp under this timezone.
   */
  int offset(int timestamp) const;

  /**
   * Test whether it was running under DST at specified timestamp.
   */
  bool dst(int timestamp) const;

  /**
   * Query transition times for DST.
   */
  Array transitions() const;

  /**
   * Get information about a timezone
   */
  Array getLocation() const;

  /**
   * Timezone Database version
   */
  static String getVersion() {
    const timelib_tzdb* db = GetDatabase();
    return String(db->version, CopyString);
  }

  /**
   * Make a copy of this timezone object, so it can be changed independently.
   */
  SmartResource<TimeZone> cloneTimeZone() const;

protected:
  friend class DateTime;
  friend class TimeStamp;
  friend class DateInterval;

  /**
   * Returns raw pointer. For internal use only.
   */
  timelib_tzinfo *get() const {
    if (m_tzi && m_tzi->type == TIMELIB_ZONETYPE_ID) {
      return m_tzi->tzi.tz;
    }
    return NULL;
  }

private:
  struct tzinfo_deleter {
    void operator()(TimeZoneInfoWrap *tzi) {
      if (tzi) {
        if (tzi->type == TIMELIB_ZONETYPE_ID) {
          timelib_tzinfo_dtor(tzi->tzi.tz);
        }
        free(tzi);
      }
    }
  };

  static const timelib_tzdb *GetDatabase();

  /**
   * Look up cache and if found return it, otherwise, read it from database.
   */
  static TimeZoneInfo GetTimeZoneInfo(char* name, const timelib_tzdb* db);
  /**
   * only for timelib, don't use it unless you are passing to a timelib func
   */
  static timelib_tzinfo* GetTimeZoneInfoRaw(char* name, const timelib_tzdb* db);

  TimeZoneInfo m_tzi; // raw pointer
};

///////////////////////////////////////////////////////////////////////////////
}

#endif // incl_HPHP_TIMEZONE_H_
