/*
 * Copyright (c) 2021 The Linux Foundation. All rights reserved.
 * Not a contribution
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Changes from Qualcomm Innovation Center are provided under the following
 * license:
 * Copyright (c) 2022,2024, Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#include "memtrack_kgsl.h"
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <log/log.h>

#define DEBUG 0

namespace vendor {
namespace qti {
namespace hardware {
namespace memtrack {

bool is_pid_dir(const struct dirent *entry) {
  const char *p;

  for (p = entry->d_name; *p; p++) {
    if (!isdigit(*p))
      return 0;
  }

  return 1;
}

int read_file(char *syspath, size_t *val)
{
  FILE *fp;
  int ret;

  fp = fopen(syspath, "r");
  if (!fp)
    return -errno;

  ret = fscanf(fp, "%zu", val);
  if (ret != 1) {
    fclose(fp);
    return -EINVAL;
  }
  fclose(fp);
  return 0;
}

/* When MemtrackType is GL and pid = 0, return the global total
 * unaccounted GPU-private memory.
 */
int getKgslTotalUnaccountedMemory(MemtrackType type,
                  std::vector<MemtrackRecord> *records) {
  DIR *procdir;
  struct dirent *entry;
  char syspath[128];
  size_t unaccounted_size;
  size_t total_unaccounted_size = 0;
  int ret;

  procdir = opendir("/sys/class/kgsl/kgsl/proc/");
  if (!procdir) {
    ALOGE("memtrack: opendir failed\n");
    return -errno;
  }

  while ((entry = readdir(procdir))) {
    if (!is_pid_dir(entry))
      continue;

    snprintf(syspath, sizeof(syspath),
      "/sys/class/kgsl/kgsl/proc/%s/gpumem_unmapped",
      entry->d_name);

    read_file(syspath, &unaccounted_size);
    total_unaccounted_size += unaccounted_size;
    ALOGI_IF(DEBUG, "memtrack: pid %s, unaccounted_size %u, total_unaccounted_size %u\n",
      entry->d_name, unaccounted_size, total_unaccounted_size);
  }

  closedir(procdir);

  if (total_unaccounted_size > 0) {
    MemtrackRecord rec;
    rec.flags = MemtrackRecord::FLAG_SMAPS_UNACCOUNTED |
        MemtrackRecord::FLAG_NONSECURE |
        MemtrackRecord::FLAG_PRIVATE;
    rec.sizeInBytes = total_unaccounted_size;
    records->push_back(rec);
  }

  return 0;
}

int getKgslMemory(int pid, MemtrackType type,
                  std::vector<MemtrackRecord> *records) {
  char syspath[128];
  size_t accounted_size = 0;
  size_t unaccounted_size = 0;
  FILE *fp;
  int ret;
  if (type == MemtrackType::GL) {
    snprintf(syspath, sizeof(syspath),
             "/sys/class/kgsl/kgsl/proc/%d/gpumem_mapped", pid);

    ret = read_file(syspath, &accounted_size);
    if (ret)
      return ret;

    snprintf(syspath, sizeof(syspath),
             "/sys/class/kgsl/kgsl/proc/%d/gpumem_unmapped", pid);
    ret = read_file(syspath, &unaccounted_size);
    if (ret)
      return ret;

    if (accounted_size > 0) {
        MemtrackRecord rec;
        rec.flags = MemtrackRecord::FLAG_SMAPS_ACCOUNTED |
                    MemtrackRecord::FLAG_NONSECURE |
                    MemtrackRecord::FLAG_PRIVATE;
        rec.sizeInBytes = accounted_size;
        records->push_back(rec);
    }
    if (unaccounted_size > 0) {
        MemtrackRecord rec;
        rec.flags = MemtrackRecord::FLAG_SMAPS_UNACCOUNTED |
                    MemtrackRecord::FLAG_NONSECURE |
                    MemtrackRecord::FLAG_PRIVATE;
        rec.sizeInBytes = unaccounted_size;
        records->push_back(rec);
    }



  } else if (type == MemtrackType::GRAPHICS) {
    snprintf(syspath, sizeof(syspath),
             "/sys/class/kgsl/kgsl/proc/%d/imported_mem", pid);

    ret = read_file(syspath, &unaccounted_size);
    if (ret)
      return ret;

    if (unaccounted_size > 0) {
        MemtrackRecord rec;
        rec.flags = MemtrackRecord::FLAG_SMAPS_UNACCOUNTED |
                    MemtrackRecord::FLAG_NONSECURE |
                    MemtrackRecord::FLAG_PRIVATE;
        rec.sizeInBytes = unaccounted_size;
        records->push_back(rec);
    }
  }
  if (records && !records->empty()) {
    for (auto &v : *records) {
      ALOGI_IF(DEBUG, "getKgslMemory: pid: %d type: %s flags: 0x%x size: %ld bytes", pid,
            aidl::android::hardware::memtrack::toString(type).c_str(), v.flags,
            v.sizeInBytes);
    }
  }
  return 0;
}

} // namespace memtrack
} // namespace hardware
} // namespace qti
} // namespace vendor
