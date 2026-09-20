#include <stdio.h>
#include <string.h>

#include "chambergenerator/ChamberGenerator.h"
#include "search/Search.h"
#include "util/Inputs.h"
#include "util/Threads.h"

static int getArea(Area *area) {
    int64_t v;
    if (!getIntEnum("area type", &area->shape, SHAPE_NAMES, SHAPE_COUNT)) return 0;
    if (area->shape == SHAPE_CHAMBERS) {
        if (!getI64Number("number of chambers (1-4)", &v, 1, 1, 1, 4)) return 0;
        area->chambers = (int)v;
    } else if (area->shape == SHAPE_SQUARE) {
        if (!getI64Number("square radius in blocks", &v, 0, 0, 0, 100000)) return 0;
        area->radius = (int)v;
        area->side_w = area->side_h = 2 * (int)v + 1;
    } else if (area->shape == SHAPE_RECTANGLE) {
        if (!getI64Number("rectangle width (X) in blocks", &v, 0, 0, 1, 200000)) return 0;
        area->side_w = (int)v;
        if (!getI64Number("rectangle length (Z) in blocks", &v, 0, 0, 1, 200000)) return 0;
        area->side_h = (int)v;
    } else {
        if (!getI64Number("circle radius in blocks", &v, 0, 0, 0, 100000)) return 0;
        area->radius = (int)v;
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
    int min_default = in->count_kind == COUNT_SPAWNERS ? 25 : 1;
    if (!getI64Number("minimum count to report", &in->min_count, 1, min_default, 1, 200)) return 0;
    if (!getI64Number("number of results to show", &in->show, 1, 10, 1, 100000)) return 0;
    if (in->area.shape == SHAPE_CHAMBERS && in->area.chambers > 1 &&
        !getIntEnum("sort results by", &in->sort, SORT_NAMES, SORT_COUNT)) return 0;
    if (!getI64Number("search radius in blocks", &v, 0, 0, 0, 30000000)) return 0;
    in->search_radius = (int)v;
    if (!getI64Number("search center X", &v, 1, 0, -30000000, 30000000)) return 0;
    in->center_x = (int)v;
    if (!getI64Number("search center Z", &v, 1, 0, -30000000, 30000000)) return 0;
    in->center_z = (int)v;
    int cpus = tc_cpu_count();
    if (!getI64Number("number of threads", &v, 1, cpus < 1024 ? cpus : 1024, 1, 1024)) return 0;
    in->threads = (int)v;
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
