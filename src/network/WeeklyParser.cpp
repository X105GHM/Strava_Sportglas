#include "WeeklyParser.hpp"
#include "config/Config.hpp"
#include "cJSON.h"
#include <cmath>
#include <cstring>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace
{
    using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

    const cJSON *field(const cJSON *o, const char *key)
    {
        return cJSON_GetObjectItemCaseSensitive(o, key);
    }

    bool unique(const cJSON *node)
    {
        if (cJSON_IsObject(node))
            for (auto a = node->child; a; a = a->next)
            {
                for (auto b = a->next; b; b = b->next)
                    if (a->string && b->string && !std::strcmp(a->string, b->string)) return false;
            }

        for (auto child = node->child; child; child = child->next)
            if (!unique(child)) return false;

        return true;
    }

    bool integer(const cJSON *o, const char *key, uint32_t &out, uint32_t max)
    {
        auto *v = field(o, key);
        if (!cJSON_IsNumber(v) || !std::isfinite(v->valuedouble) || v->valuedouble < 0 || v->valuedouble > max || std::floor(v->valuedouble)
            != v->valuedouble) return false;
        out = static_cast<uint32_t>(v->valuedouble);
        return true;
    }

    bool date(const char *value, std::tm &result)
    {
        if (!value || std::strlen(value) < 10 || value[4] != '-' || value[7] != '-') return false;
        for (unsigned i = 0; i < 10; ++i)
            if (i != 4 && i != 7 && (value[i] < '0' || value[i] > '9')) return false;

        int y, m, d;
        if (std::sscanf(value, "%4d-%2d-%2d", &y, &m, &d) != 3 || y < 2020 || y > 2100 || m < 1 || m > 12 || d < 1 || d > 31) return false;

        result = {};
        result.tm_year = y - 1900;
        result.tm_mon = m - 1;
        result.tm_mday = d;

        auto normalized = result;
        if (std::mktime(&normalized) < 0 || normalized.tm_mon != result.tm_mon || normalized.tm_mday != result.tm_mday) return false;
        result = normalized;
        return true;
    }

    bool timestamp(const char *s, int64_t &epoch, std::tm &local)
    {
        if (!s) return false;

        const auto n = std::strlen(s);
        // Server-Zeitstempel können Mikrosekunden enthalten; die Zeitzone muss angegeben sein.
        if (n < 20 || n > 32 || s[10] != 'T' || s[13] != ':' || s[16] != ':' || !date(s, local)) return false;
        for (auto i : {11, 12, 14, 15, 17, 18})
            if (s[i] < '0' || s[i] > '9') return false;

        int h, m, sec;
        if (std::sscanf(s + 11, "%2d:%2d:%2d", &h, &m, &sec) != 3 || h > 23 || m > 59 || sec > 59) return false;
        local.tm_hour = h;
        local.tm_min = m;
        local.tm_sec = sec;

        std::size_t p = 19;
        if (s[p] == '.')
        {
            ++p;
            auto begin = p;
            while (p < n && s[p] >= '0' && s[p] <= '9')
                ++p;
            if (p == begin || p - begin > 6) return false;
        }

        int offset = 0;
        if (p + 1 == n && s[p] == 'Z')
        {
        }
        else if (p + 6 == n && (s[p] == '+' || s[p] == '-') && s[p + 3] == ':')
        {
            for (auto i : {p + 1, p + 2, p + 4, p + 5})
                if (s[i] < '0' || s[i] > '9') return false;
            int oh = (s[p + 1] - '0') * 10 + s[p + 2] - '0', om = (s[p + 4] - '0') * 10 + s[p + 5] - '0';
            if (oh > 14 || om > 59 || (oh == 14 && om)) return false;
            offset = (oh * 60 + om) * 60 * (s[p] == '+' ? 1 : -1);
        }
        else
            return false;

        epoch = std::mktime(&local) - offset;
        return epoch > 0;
    }
}

bool parseWeekly(const char *body, SavedState &state, int64_t now)
{
    const char *end = nullptr;
    Json root(cJSON_ParseWithOpts(body, &end, true), cJSON_Delete);
    if (!root || !cJSON_IsObject(root.get()) || !unique(root.get())) return false;

    auto *r = root.get();
    uint32_t schema{}, age{}, baseline{};
    if (!integer(r, "schema_version", schema, 1) || schema != 1 || !integer(r, "data_age_seconds", age, 315360000) ||
        !integer(r, "baseline_weeks", baseline, 52) || baseline < 1 || !cJSON_IsBool(field(r, "stale"))) return false;

    auto *generated = field(r, "generated_at");
    int64_t stamp{};
    std::tm local{};
    if (!cJSON_IsString(generated) || !timestamp(generated->valuestring, stamp, local)) return false;
    if (stamp > now + 300 || std::llabs((now - stamp) - age) > 600 || stamp < state.generatedAt) return false;

    auto *week = field(r, "week");
    auto *start = field(week, "start");
    auto *finish = field(week, "end");
    std::tm first{}, last{};
    uint32_t year{}, number{};
    if (!cJSON_IsString(start) || !cJSON_IsString(finish) || std::strlen(start->valuestring) != 10 || std::strlen(finish->valuestring) != 10 
        || !date(start->valuestring, first) || !date(finish->valuestring, last) || first.tm_wday != 1 ||
        !integer(week, "iso_year", year, 2100) || year < 2020 || !integer(week, "iso_week", number, 53) || number < 1)
        return false;
    auto a = std::mktime(&first), b = std::mktime(&last), g = std::mktime(&local);
    if (b - a != 6 * 86400 || g < a || g >= a + 7 * 86400) return false;

    // Der Donnerstag bestimmt das ISO-Wochenjahr; so sind keine plattformspezifischen strftime-Erweiterungen nötig.
    auto thursday = first;
    thursday.tm_mday += 3;
    std::mktime(&thursday);
    auto expectedYear = static_cast<uint32_t>(thursday.tm_year + 1900);
    auto expectedWeek = static_cast<uint32_t>(thursday.tm_yday / 7 + 1);
    if (expectedYear != year || expectedWeek != number) return false;

    WeeklySummary summary{};
    auto *sports = field(r, "sports");
    auto sport = [&](const char *key, SportStats &s)
    {
        auto *item = field(sports, key);
        return cJSON_IsObject(item) && integer(item, "current_m", s.currentMeters, config::kMaxMeters) 
                && integer(item, "average_week_m", s.averageWeekMeters, config::kMaxMeters);
    };
    if (!sport("swim", summary.swim) || !sport("run", summary.run) || !sport("ride", summary.ride)) return false;

    state.summary = summary;
    state.generatedAt = stamp;
    state.hasSummary = true;
    return true;
}
