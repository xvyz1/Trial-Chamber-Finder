#include <stdio.h>
#include <string.h>

#include "chambergenerator/ChamberGenerator.h"
#include "search/Search.h"
#include "util/Inputs.h"
#include "util/Threads.h"

static int getArea(Area *area) {
    int64_t v;
    static const char *const names[SHAPE_COUNT] = {"# OF CHAMBERS", "SQUARE", "RECTANGLE"};
    if (!getIntEnum("area type", &area->shape, names, SHAPE_COUNT)) return 0;
    if (area->shape == SHAPE_CHAMBERS) {
        if (!getI64Number("number of chambers (1-4)", &v, 1, 1, 1, 4)) return 0;
        area->chambers = (int)v;
    } else if (area->shape == SHAPE_SQUARE) {
        if (!getI64Number("square radius in blocks", &v, 0, 0, 0, 100000)) return 0;
        area->radius = (int)v;
        area->side_w = area->side_h = 2 * (int)v + 1;
    } else {
        if (!getI64Number("rectangle width (X) in blocks", &v, 0, 0, 1, 200000)) return 0;
        area->side_w = (int)v;
        if (!getI64Number("rectangle length (Z) in blocks", &v, 0, 0, 1, 200000)) return 0;
        area->side_h = (int)v;
    }
    return 1;
}

static int getVersion(InputData *in) {
    static const char *names[64];
    static char labels[64][48];
    int n = tc_num_versions();
    if (n > 64) n = 64;
    for (int i = 0; i < n; i++) {
        const char *v = tc_version_name(i);
        snprintf(labels[i], sizeof labels[i], "%s%s", v, strncmp(v, "1.20", 4) == 0 ? " (experimental)" : "");
        names[i] = labels[i];
    }
    return getIntEnum("closest lower Minecraft version", &in->version, names, n);
}

static int getInputData(InputData *in) {
    int64_t v;
    if (!getVersion(in)) return 0;
    if (!getI64Number("numeric world seed", &in->seed, 0, 0, INT64_MIN, INT64_MAX)) return 0;
    if (!getIntEnum("what to count", &in->count_kind, COUNT_NAMES, COUNT_KINDS)) return 0;
    if (in->count_kind == COUNT_SPAWNERS && !getIntSet("spawner mobs to leave out", in->exclude, MOB_NAMES, MOB_COUNT)) return 0;
    if (!getArea(&in->area)) return 0;
    static const char *const count_words[COUNT_KINDS] = {"trial spawners", "vaults", "ominous vaults"};
    char label[96];
    snprintf(label, sizeof label, "minimum count of %s to report", count_words[in->count_kind]);
    int min_default = 1;
    if (in->count_kind == COUNT_SPAWNERS) min_default = 20 * (in->area.shape == SHAPE_CHAMBERS ? in->area.chambers : 1);
    if (!getI64Number(label, &in->min_count, 1, min_default, 1, 200)) return 0;
    if (!getI64Number("number of results to show", &in->show, 1, 20, 1, 100000)) return 0;
    if (in->area.shape == SHAPE_CHAMBERS && in->area.chambers > 1) {
        static const char *const sort_names[SORT_COUNT] = {"AMOUNT_THEN_CLOSENESS", "AMOUNT", "CLOSENESS"};
        static const int sort_value[SORT_COUNT] = {SORT_AMOUNT_THEN_CLOSENESS, SORT_AMOUNT, SORT_CLOSENESS};
        int pick;
        if (!getIntEnum("sort results by", &pick, sort_names, SORT_COUNT)) return 0;
        in->sort = sort_value[pick];
    }
    if (!getI64Number("search radius in blocks", &v, 0, 0, 0, 30000000)) return 0;
    in->search_radius = (int)v;
    if (!getI64Number("search center X", &v, 1, 0, -30000000, 30000000)) return 0;
    in->center_x = (int)v;
    if (!getI64Number("search center Z", &v, 1, 0, -30000000, 30000000)) return 0;
    in->center_z = (int)v;
    int cpus = tc_cpu_count();
    if (!getI64Number("number of threads", &v, 1, cpus < 1024 ? cpus : 1024, 1, 1024)) return 0;
    in->threads = (int)v;
    static const char *const csv_names[2] = {"YES", "NO"};
    int pick;
    if (!getIntEnumDefault("CSV output", &pick, csv_names, 2, 1)) return 0;
    in->csv = pick == 0;
    return 1;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "selftest") == 0) return tc_selftest(1000000) ? 1 : 0;

    InputData in;
    memset(&in, 0, sizeof in);
    int result = 1;
    if (getInputData(&in)) result = search_run(&in);

    printf("Press enter to continue . . .\n");
    getchar();
    return result;
}
