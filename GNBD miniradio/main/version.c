/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * Firmware version/build helpers sourced from the ESP app descriptor.
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "version.h"

const char *app_version(void)
{
    const esp_app_desc_t *d = esp_app_get_description();
    return (d && d->version[0]) ? d->version : "?";
}

const char *app_project_name(void)
{
    const esp_app_desc_t *d = esp_app_get_description();
    return (d && d->project_name[0]) ? d->project_name : "miniradio";
}

const char *app_idf_version(void)
{
    const esp_app_desc_t *d = esp_app_get_description();
    return (d && d->idf_ver[0]) ? d->idf_ver : "?";
}

const char *app_build_no(void)
{
    static char build[16];
    static bool ready;

    if (ready) {
        return build;
    }
    ready = true;

    static const char *const mon[12] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    int month = 0, day = 0, year = 0, hour = 0, minute = 0, second = 0;
    char mstr[4] = {0};

    const esp_app_desc_t *d = esp_app_get_description();
    if (d) {
        if (sscanf(d->date, "%3s %d %d", mstr, &day, &year) == 3) {
            for (int i = 0; i < 12; ++i) {
                if (strncmp(mstr, mon[i], 3) == 0) {
                    month = i + 1;
                    break;
                }
            }
        }
        sscanf(d->time, "%d:%d:%d", &hour, &minute, &second);
    }

    if (month >= 1 && month <= 12 && day >= 1 && day <= 31) {
        snprintf(build, sizeof(build), "%02d%02d%02d%02d%02d%02d",
                 year % 100, month, day, hour, minute, second);
    } else {
        snprintf(build, sizeof(build), "------------");
    }
    return build;
}
