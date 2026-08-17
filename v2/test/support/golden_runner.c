/* See golden_runner.h. Plain C, no Unity: the exit code is the verdict. */
#include "support/golden_runner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "colibrino/v2/blink_pipeline.h"

#define MAX_FILES 64
#define MAX_NAME 128
#define MAX_SEGMENTS 256
#define LINE_CAP 512

typedef struct {
  char name[MAX_NAME]; /* relative path as listed in the manifest */
} listed_t;

/* One expectation row of NAME.labels.tsv. A segment with two expectations
 * (for example IMPULSE and CLICK_CANDIDATE) yields two rows that share the
 * same name and window; each row is evaluated independently by the mode
 * that owns its expect_kind. */
typedef enum { EXPECT_IMPULSE = 0, EXPECT_CLICK = 1 } expect_kind_t;
typedef enum { OP_EQ = 0, OP_LE = 1, OP_GE = 2 } expect_op_t;

typedef struct {
  char id[48];
  char kind[24];
  uint32_t start_ms;
  uint32_t end_ms;
  uint32_t tolerance_ms;
  expect_kind_t expect_kind;
  expect_op_t op;
  uint32_t value;
  uint32_t got; /* events of expect_kind inside the widened window */
} segment_t;

static const char *const kCsvHeader = "t_ms,t_us,gx_dps,gy_dps,gz_dps,ax_g,ay_g,az_g";
/* Exactly what sticks3/tools/make_trace_fixture.py::derive_tsv emits
 * (TSV_HEADER); the labels_schema_consistency test pins the two together. */
static const char *const kTsvHeader =
    "# segment_name\tkind\tt_start_ms\tt_end_ms\texpect_kind\top\tvalue\t"
    "tolerance_ms";

static void chomp(char *s) {
  size_t n = strlen(s);
  while (n > 0u && (s[n - 1u] == '\n' || s[n - 1u] == '\r')) {
    s[--n] = '\0';
  }
}

static const char *basename_of(const char *path) {
  const char *slash = strrchr(path, '/');
  return slash != NULL ? slash + 1 : path;
}

/* Reads the manifest into listed[]; returns count or -1. */
static int read_manifest(const char *traces_dir, listed_t *listed) {
  char path[LINE_CAP];
  char line[LINE_CAP];
  int count = 0;
  snprintf(path, sizeof path, "%s/MANIFEST.sha256", traces_dir);
  FILE *f = fopen(path, "r");
  if (f == NULL) {
    printf("FAIL: cannot open traces/MANIFEST.sha256\n");
    return -1;
  }
  while (fgets(line, sizeof line, f) != NULL) {
    chomp(line);
    if (line[0] == '\0') {
      continue;
    }
    const char *sep = strstr(line, "  ");
    if (sep == NULL || count >= MAX_FILES) {
      printf("FAIL: malformed or oversized manifest\n");
      fclose(f);
      return -1;
    }
    snprintf(listed[count].name, MAX_NAME, "%s", sep + 2);
    ++count;
  }
  fclose(f);
  return count;
}

static bool is_listed(const listed_t *listed, int count, const char *name) {
  for (int i = 0; i < count; ++i) {
    if (strcmp(listed[i].name, name) == 0) {
      return true;
    }
  }
  return false;
}

/* All-digit token -> value; anything else ("null", "", "1x", "-1") is a
 * malformed label and must fail closed instead of silently becoming a count.
 * Returns false on a bad token. */
static bool parse_count(const char *tok, uint32_t *out) {
  if (tok[0] == '\0') {
    return false;
  }
  for (const char *c = tok; *c != '\0'; ++c) {
    if (*c < '0' || *c > '9') {
      return false;
    }
  }
  *out = (uint32_t)strtoul(tok, NULL, 10);
  return true;
}

/* Parses NAME.labels.tsv (one row per expectation); returns row count or -1. */
static int read_labels(const char *path, const char *shown, segment_t *segs) {
  char line[LINE_CAP];
  int count = 0;
  FILE *f = fopen(path, "r");
  if (f == NULL) {
    printf("FAIL: %s: cannot open labels\n", shown);
    return -1;
  }
  if (fgets(line, sizeof line, f) == NULL) {
    printf("FAIL: %s: empty labels\n", shown);
    fclose(f);
    return -1;
  }
  chomp(line);
  if (strcmp(line, kTsvHeader) != 0) {
    printf("FAIL: %s: unexpected labels header\n", shown);
    fclose(f);
    return -1;
  }
  while (fgets(line, sizeof line, f) != NULL) {
    chomp(line);
    if (line[0] == '\0' || line[0] == '#') {
      continue;
    }
    if (count >= MAX_SEGMENTS) {
      printf("FAIL: %s: too many label rows\n", shown);
      fclose(f);
      return -1;
    }
    char *fields[8] = {0};
    int nf = 0;
    char *cursor = line;
    while (nf < 8) {
      fields[nf++] = cursor;
      char *tab = strchr(cursor, '\t');
      if (tab == NULL) {
        break;
      }
      *tab = '\0';
      cursor = tab + 1;
    }
    if (nf != 8 || strchr(cursor, '\t') != NULL) {
      printf("FAIL: %s: label row %d does not have 8 fields\n", shown,
             count + 1);
      fclose(f);
      return -1;
    }
    segment_t *s = &segs[count];
    memset(s, 0, sizeof *s);
    snprintf(s->id, sizeof s->id, "%s", fields[0]);
    snprintf(s->kind, sizeof s->kind, "%s", fields[1]);
    bool ok = parse_count(fields[2], &s->start_ms) &&
              parse_count(fields[3], &s->end_ms) &&
              parse_count(fields[6], &s->value) &&
              parse_count(fields[7], &s->tolerance_ms);
    if (strcmp(fields[4], "IMPULSE") == 0) {
      s->expect_kind = EXPECT_IMPULSE;
    } else if (strcmp(fields[4], "CLICK_CANDIDATE") == 0) {
      s->expect_kind = EXPECT_CLICK;
    } else {
      ok = false;
    }
    if (strcmp(fields[5], "eq") == 0) {
      s->op = OP_EQ;
    } else if (strcmp(fields[5], "le") == 0) {
      s->op = OP_LE;
    } else if (strcmp(fields[5], "ge") == 0) {
      s->op = OP_GE;
    } else {
      ok = false;
    }
    if (!ok) {
      printf("FAIL: %s: label row %d (%s) has a malformed token\n", shown,
             count + 1, s->id);
      fclose(f);
      return -1;
    }
    if (s->end_ms < s->start_ms) {
      printf("FAIL: %s: segment %s ends before it starts\n", shown, s->id);
      fclose(f);
      return -1;
    }
    ++count;
  }
  fclose(f);
  return count;
}

static bool parse_row(char *line, cv2_imu_sample_t *s) {
  char *fields[8] = {0};
  int nf = 0;
  char *cursor = line;
  while (nf < 8) {
    fields[nf++] = cursor;
    char *comma = strchr(cursor, ',');
    if (comma == NULL) {
      break;
    }
    *comma = '\0';
    cursor = comma + 1;
  }
  if (nf != 8) {
    return false;
  }
  char *end = NULL;
  s->t_ms = (uint32_t)strtoul(fields[0], &end, 10);
  if (end == fields[0]) {
    return false;
  }
  s->t_us = (uint32_t)strtoul(fields[1], NULL, 10);
  for (int i = 0; i < 3; ++i) {
    s->gyro_dps[i] = strtof(fields[2 + i], &end);
    if (end == fields[2 + i]) {
      return false;
    }
    s->accel_g[i] = strtof(fields[5 + i], NULL);
  }
  return true;
}

/* Streams one fixture through the pipeline; fills segment counters. */
static bool run_fixture(const char *csv_path, const char *shown,
                        segment_t *segs, int nseg, uint32_t *rows_out) {
  char line[LINE_CAP];
  FILE *f = fopen(csv_path, "r");
  if (f == NULL) {
    printf("FAIL: %s: cannot open trace\n", shown);
    return false;
  }
  if (fgets(line, sizeof line, f) == NULL) {
    printf("FAIL: %s: empty trace\n", shown);
    fclose(f);
    return false;
  }
  chomp(line);
  if (strcmp(line, kCsvHeader) != 0) {
    printf("FAIL: %s: unexpected csv header\n", shown);
    fclose(f);
    return false;
  }
  cv2_blink_pipeline_state_t p;
  cv2_blink_pipeline_init(&p, NULL, NULL);
  uint32_t rows = 0u;
  while (fgets(line, sizeof line, f) != NULL) {
    chomp(line);
    if (line[0] == '\0') {
      continue;
    }
    cv2_imu_sample_t s;
    memset(&s, 0, sizeof s);
    if (!parse_row(line, &s)) {
      printf("FAIL: %s: bad csv row %u\n", shown, rows + 1u);
      fclose(f);
      return false;
    }
    ++rows;
    cv2_blink_event_t b;
    const cv2_gesture_event_t g = cv2_blink_pipeline_step(&p, &s, &b);
    const bool impulse = b.hdr.kind == (uint8_t)CV2_BLINK_IMPULSE;
    const bool click = g.hdr.kind == (uint8_t)CV2_GESTURE_CLICK_CANDIDATE;
    if (!impulse && !click) {
      continue;
    }
    for (int i = 0; i < nseg; ++i) {
      /* Inclusive window on the event's own device timestamp, widened by the
       * row's tolerance on both sides. */
      const uint32_t lo = segs[i].start_ms - segs[i].tolerance_ms;
      const uint32_t hi = segs[i].end_ms + segs[i].tolerance_ms;
      if (CV2_MS_DIFF(s.t_ms, lo) < 0 || CV2_MS_DIFF(hi, s.t_ms) < 0) {
        continue;
      }
      if (impulse && segs[i].expect_kind == EXPECT_IMPULSE) {
        ++segs[i].got;
      }
      if (click && segs[i].expect_kind == EXPECT_CLICK) {
        ++segs[i].got;
      }
    }
  }
  fclose(f);
  *rows_out = rows;
  return rows > 0u;
}

int golden_main(golden_mode_t mode, const char *traces_dir, int oracle_gate) {
  static listed_t listed[MAX_FILES];
  static segment_t segs[MAX_SEGMENTS];
  const char *what = mode == GOLDEN_MODE_IMPULSES ? "impulses" : "clicks";
  const int count = read_manifest(traces_dir, listed);
  if (count < 0) {
    return 1;
  }

  int pairs = 0;
  int failures = 0;
  int checked = 0;
  for (int i = 0; i < count; ++i) {
    const char *name = listed[i].name;
    const size_t len = strlen(name);
    if (len < 5u || strcmp(name + len - 4u, ".csv") != 0) {
      continue;
    }
    char base[MAX_NAME];
    snprintf(base, sizeof base, "%.*s", (int)(len - 4u), name);
    char tsv[MAX_NAME + 16];
    snprintf(tsv, sizeof tsv, "%s.labels.tsv", base);
    if (!is_listed(listed, count, tsv)) {
      printf("FAIL: %s is promoted without %s\n", basename_of(name),
             basename_of(tsv));
      ++failures;
      continue;
    }
    ++pairs;
    char csv_path[LINE_CAP];
    char tsv_path[LINE_CAP];
    snprintf(csv_path, sizeof csv_path, "%s/%s", traces_dir, name);
    snprintf(tsv_path, sizeof tsv_path, "%s/%s", traces_dir, tsv);
    const char *shown = basename_of(name);
    const int nseg = read_labels(tsv_path, shown, segs);
    if (nseg < 0) {
      ++failures;
      continue;
    }
    uint32_t rows = 0u;
    if (!run_fixture(csv_path, shown, segs, nseg, &rows)) {
      ++failures;
      continue;
    }
    for (int k = 0; k < nseg; ++k) {
      const segment_t *s = &segs[k];
      const expect_kind_t owned =
          mode == GOLDEN_MODE_IMPULSES ? EXPECT_IMPULSE : EXPECT_CLICK;
      if (s->expect_kind != owned) {
        continue; /* the other golden executable owns this row */
      }
      ++checked;
      bool met = false;
      const char *opname = "eq";
      if (s->op == OP_EQ) {
        met = s->got == s->value;
      } else if (s->op == OP_LE) {
        met = s->got <= s->value;
        opname = "le";
      } else {
        met = s->got >= s->value;
        opname = "ge";
      }
      if (!met) {
        printf("FAIL: %s segment %s (%s): expected %s %s %u, got %u\n", shown,
               s->id, s->kind, what, opname, s->value, s->got);
        ++failures;
      }
    }
    printf("[REAL] %s: %u rows, %d segments\n", shown, rows, nseg);
  }

  if (pairs == 0 && failures == 0) {
    if (oracle_gate) {
      printf("FAIL: oracle gate requires promoted fixtures under v2/traces/\n");
      return 1;
    }
    printf("SKIP: no fixtures promoted under v2/traces/\n");
    return GOLDEN_EXIT_SKIP;
  }
  printf("[REAL] golden(%s): %d fixture pair(s), %d expectation(s) checked, %d "
         "failure(s)\n",
         what, pairs, checked, failures);
  return failures == 0 ? 0 : 1;
}
