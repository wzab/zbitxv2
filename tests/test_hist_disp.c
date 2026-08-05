#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

#include "../sdr_ui.h"
#include "../hist_disp.h"

const char *field_str(char *label)
{
    (void)label;
    return "DL/RT3REW";
}

void logbook_open(void)
{
}

int logbook_get_grids(void (*callback)(char *, int))
{
    (void)callback;
    return 0;
}

bool logbook_grid_exists(char *id)
{
    (void)id;
    return false;
}

bool logbook_caller_exists(char *id)
{
    (void)id;
    return false;
}

static int check_visible_message(const char *message)
{
    char raw[256];
    char decorated[1000];
    char stripped[1000];

    snprintf(raw, sizeof(raw), "191115  27 -09 2331 ~  %s\n", message);
    int rc = hd_decorate(FONT_FT8_RX, raw, decorated);
    hd_strip_decoration(stripped, decorated);

    if (rc != 0 || strstr(stripped, message) == NULL)
    {
        fprintf(stderr,
            "FAIL display decoration: message='%s' rc=%d decorated='%s' stripped='%s'\n",
            message, rc, decorated, stripped);
        return 1;
    }

    printf("PASS display decoration: %s\n", message);
    return 0;
}

static int check_fail_open(void)
{
    char raw[] = "malformed FT8 console line\n";
    char decorated[1000];

    int rc = hd_decorate(FONT_FT8_RX, raw, decorated);
    if (rc == 0 || strcmp(raw, decorated) != 0)
    {
        fprintf(stderr,
            "FAIL decoration fallback: rc=%d raw='%s' decorated='%s'\n",
            rc, raw, decorated);
        return 1;
    }

    printf("PASS decoration fallback preserves raw text\n");
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += check_visible_message("CQ SP5DAA KO02");
    failures += check_visible_message("CQ OK/SP5DAA");
    failures += check_visible_message("CQ OK/SP5DAA/P");
    failures += check_visible_message("<SP5DAA> DL/RT3REW");
    failures += check_fail_open();

    printf("FT8 display regression: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
