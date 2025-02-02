/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * *    * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#define LOG_NIDEBUG 0

#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <stdlib.h>

#define LOG_TAG "QCOM PowerHAL"
#include <utils/Log.h>
#include <hardware/hardware.h>
#include <hardware/power.h>

#include "utils.h"
#include "metadata-defs.h"
#include "hint-data.h"
#include "performance.h"
#include "power-common.h"

#define ARRAY_SIZE(arr) (sizeof((arr)) / sizeof((arr)[0]))

static int display_hint_sent;

int get_number_of_profiles() {
    return 5;
}

static int current_power_profile = PROFILE_BALANCED;

static void set_power_profile(int profile) {

    if (profile == current_power_profile)
        return;

    ALOGV("%s: profile=%d", __func__, profile);

    if (current_power_profile != PROFILE_BALANCED) {
        undo_hint_action(DEFAULT_PROFILE_HINT_ID);
        ALOGV("%s: hint undone", __func__);
    }

    if (profile == PROFILE_POWER_SAVE) {
        int resource_values[] = { CPUS_ONLINE_MPD_OVERRIDE, 0x0A03,
            CPU0_MAX_FREQ_NONTURBO_MAX - 2, CPU1_MAX_FREQ_NONTURBO_MAX - 2,
            CPU2_MAX_FREQ_NONTURBO_MAX - 2, CPU3_MAX_FREQ_NONTURBO_MAX - 2,
            CPU4_MAX_FREQ_NONTURBO_MAX - 2, CPU5_MAX_FREQ_NONTURBO_MAX - 2 };
        perform_hint_action(DEFAULT_PROFILE_HINT_ID,
            resource_values, sizeof(resource_values)/sizeof(resource_values[0]));
        ALOGD("%s: set powersave", __func__);
    } else if (profile == PROFILE_HIGH_PERFORMANCE) {
        int resource_values[] = { SCHED_BOOST_ON, CPUS_ONLINE_MAX, 0x0901, 0x101,
            CPU0_MIN_FREQ_TURBO_MAX, CPU1_MIN_FREQ_TURBO_MAX,
            CPU2_MIN_FREQ_TURBO_MAX, CPU3_MIN_FREQ_TURBO_MAX,
            CPU4_MIN_FREQ_TURBO_MAX, CPU5_MIN_FREQ_TURBO_MAX };
        perform_hint_action(DEFAULT_PROFILE_HINT_ID,
            resource_values, sizeof(resource_values)/sizeof(resource_values[0]));
        ALOGD("%s: set performance mode", __func__);
    } else if (profile == PROFILE_BIAS_POWER) {
        int resource_values[] = { 0x0A03, 0x0902,
            CPU0_MAX_FREQ_NONTURBO_MAX - 2, CPU1_MAX_FREQ_NONTURBO_MAX - 2,
            CPU1_MAX_FREQ_NONTURBO_MAX - 2, CPU2_MAX_FREQ_NONTURBO_MAX - 2,
            CPU4_MAX_FREQ_NONTURBO_MAX, CPU5_MAX_FREQ_NONTURBO_MAX };
        perform_hint_action(DEFAULT_PROFILE_HINT_ID,
            resource_values, sizeof(resource_values)/sizeof(resource_values[0]));
        ALOGD("%s: set bias power mode", __func__);
    } else if (profile == PROFILE_BIAS_PERFORMANCE) {
        int resource_values[] = { CPUS_ONLINE_MAX_LIMIT_MAX,
            CPU4_MIN_FREQ_NONTURBO_MAX + 1, CPU5_MIN_FREQ_NONTURBO_MAX + 1 };
        perform_hint_action(DEFAULT_PROFILE_HINT_ID,
            resource_values, sizeof(resource_values)/sizeof(resource_values[0]));
        ALOGD("%s: set bias perf mode", __func__);
    }

    current_power_profile = profile;
}

extern void interaction(int duration, int num_args, int opt_list[]);

static int process_video_encode_hint(void *metadata)
{
    char governor[80];
    struct video_encode_metadata_t video_encode_metadata;

    if (get_scaling_governor(governor, sizeof(governor)) == -1) {
        ALOGE("Can't obtain scaling governor.");

        return HINT_NONE;
    }

    /* Initialize encode metadata struct fields */
    memset(&video_encode_metadata, 0, sizeof(struct video_encode_metadata_t));
    video_encode_metadata.state = -1;
    video_encode_metadata.hint_id = DEFAULT_VIDEO_ENCODE_HINT_ID;

    if (metadata) {
        if (parse_video_encode_metadata((char *)metadata, &video_encode_metadata) ==
            -1) {
            ALOGE("Error occurred while parsing metadata.");
            return HINT_NONE;
        }
    } else {
        return HINT_NONE;
    }

    if (video_encode_metadata.state == 1) {
        if ((strncmp(governor, INTERACTIVE_GOVERNOR, strlen(INTERACTIVE_GOVERNOR)) == 0) &&
                (strlen(governor) == strlen(INTERACTIVE_GOVERNOR))) {
            /* sched and cpufreq params
             * hispeed freq - 768 MHz
             * target load - 90
             * above_hispeed_delay - 40ms
             * sched_small_tsk - 50
             */
            int resource_values[] = {0x2C07, 0x2F5A, 0x2704, 0x4032};

            perform_hint_action(video_encode_metadata.hint_id,
                    resource_values, sizeof(resource_values)/sizeof(resource_values[0]));
            return HINT_HANDLED;
        }
    } else if (video_encode_metadata.state == 0) {
        if ((strncmp(governor, INTERACTIVE_GOVERNOR, strlen(INTERACTIVE_GOVERNOR)) == 0) &&
                (strlen(governor) == strlen(INTERACTIVE_GOVERNOR))) {
            undo_hint_action(video_encode_metadata.hint_id);
            return HINT_HANDLED;
        }
    }
    return HINT_NONE;
}

int power_hint_override(__attribute__((unused)) struct power_module *module,
		power_hint_t hint, void *data)
{
    if (hint == POWER_HINT_SET_PROFILE) {
        set_power_profile(*(int32_t *)data);
        return HINT_HANDLED;
    }

    // Skip other hints in custom power modes
    if (current_power_profile == PROFILE_POWER_SAVE ||
            current_power_profile == PROFILE_HIGH_PERFORMANCE) {
        return HINT_HANDLED;
    }

    if (hint == POWER_HINT_INTERACTION) {
        int duration = 500, duration_hint = 0;
        static unsigned long long previous_boost_time = 0;

        if (data) {
            duration_hint = *((int *)data);
        }

        duration = duration_hint > 0 ? duration_hint : 500;

        struct timeval cur_boost_timeval = {0, 0};
        gettimeofday(&cur_boost_timeval, NULL);
        unsigned long long cur_boost_time = cur_boost_timeval.tv_sec * 1000000 + cur_boost_timeval.tv_usec;
        double elapsed_time = (double)(cur_boost_time - previous_boost_time);
        if (elapsed_time > 750000)
            elapsed_time = 750000;
        // don't hint if it's been less than 250ms since last boost
        // also detect if we're doing anything resembling a fling
        // support additional boosting in case of flings
        else if (elapsed_time < 250000 && duration <= 750)
            return HINT_HANDLED;

        previous_boost_time = cur_boost_time;

        if (duration >= 1500) {
            int resources[] = { SCHED_BOOST_ON, 0x20D, 0x101, 0x3E01 };
            interaction(duration, sizeof(resources)/sizeof(resources[0]), resources);
        } else {
            int resources[] = { 0x20D, 0x101, 0x3E01 };
            interaction(duration, sizeof(resources)/sizeof(resources[0]), resources);
        }
        return HINT_HANDLED;
    }

    if (hint == POWER_HINT_LAUNCH_BOOST) {
        int duration = 2000;
        int resources[] = { SCHED_BOOST_ON, 0x20F, 0x101, 0x3E01 };

        interaction(duration, sizeof(resources)/sizeof(resources[0]), resources);

        return HINT_HANDLED;
    }

    if (hint == POWER_HINT_CPU_BOOST) {
        int duration = *(int32_t *)data / 1000;
        int resources[] = { SCHED_BOOST_ON };

        if (duration > 0)
            interaction(duration, sizeof(resources)/sizeof(resources[0]), resources);

        return HINT_HANDLED;
    }

    if (hint == POWER_HINT_VIDEO_ENCODE) {
        return process_video_encode_hint(data);
    }

    return HINT_NONE;
}

int set_interactive_override(__attribute__((unused)) struct power_module *module, int on)
{
    char governor[80];

    if (get_scaling_governor(governor, sizeof(governor)) == -1) {
        ALOGE("Can't obtain scaling governor.");

        return HINT_NONE;
    }

    if (!on) {
        /* Display off */
        if ((strncmp(governor, INTERACTIVE_GOVERNOR, strlen(INTERACTIVE_GOVERNOR)) == 0) &&
            (strlen(governor) == strlen(INTERACTIVE_GOVERNOR))) {
            int resource_values[] = {0x777}; /* 4+0 core config in display off */
            if (!display_hint_sent) {
                perform_hint_action(DISPLAY_STATE_HINT_ID,
                resource_values, sizeof(resource_values)/sizeof(resource_values[0]));
                display_hint_sent = 1;
                return HINT_HANDLED;
            }
        }
    } else {
        /* Display on */
        if ((strncmp(governor, INTERACTIVE_GOVERNOR, strlen(INTERACTIVE_GOVERNOR)) == 0) &&
            (strlen(governor) == strlen(INTERACTIVE_GOVERNOR))) {
            undo_hint_action(DISPLAY_STATE_HINT_ID);
            display_hint_sent = 0;
            return HINT_HANDLED;
        }
    }
    return HINT_NONE;
}
