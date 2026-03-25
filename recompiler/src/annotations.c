/*
 * annotations.c — CSV annotation loader.
 *
 * Reads a <game>_annotations.csv file that associates ROM addresses with
 * human-readable names and notes. Used by code_generator.c to emit
 * comment headers above each recompiled function.
 *
 * CSV format (no header row):
 *   <hex_addr>,<name>,<notes>
 *   000200,Reset_Handler,"Entry point from vector table"
 *   001234,SomeFunc,"Does XYZ"
 *
 * Lines beginning with '#' are comments and are ignored.
 */
#include "annotations.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool annotations_load(AnnotationTable *out, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return false;

    char line[512];
    int capacity = 64;
    out->entries = (Annotation *)malloc(capacity * sizeof(Annotation));
    if (!out->entries) { fclose(f); return false; }
    out->count = 0;

    while (fgets(line, sizeof(line), f)) {
        /* Strip trailing newline */
        int len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        if (len == 0 || line[0] == '#') continue;

        /* Parse: addr,name,notes */
        char addr_str[16] = {0}, name[128] = {0}, notes[256] = {0};
        char *tok = strtok(line, ",");
        if (!tok) continue;
        strncpy(addr_str, tok, sizeof(addr_str) - 1);

        tok = strtok(NULL, ",");
        if (tok) strncpy(name, tok, sizeof(name) - 1);

        tok = strtok(NULL, "\n");
        if (tok) {
            /* Strip surrounding quotes if present */
            if (tok[0] == '"') tok++;
            int nlen = (int)strlen(tok);
            if (nlen > 0 && tok[nlen-1] == '"') tok[nlen-1] = '\0';
            strncpy(notes, tok, sizeof(notes) - 1);
        }

        if (out->count >= capacity) {
            capacity *= 2;
            Annotation *tmp = realloc(out->entries, capacity * sizeof(Annotation));
            if (!tmp) break;
            out->entries = tmp;
        }

        Annotation *a = &out->entries[out->count++];
        a->addr = (uint32_t)strtoul(addr_str, NULL, 16);
        strncpy(a->name,  name,  sizeof(a->name)  - 1);
        strncpy(a->notes, notes, sizeof(a->notes) - 1);
    }

    fclose(f);
    return out->count > 0;
}

const char *annotations_get_name(const AnnotationTable *at, uint32_t addr) {
    for (int i = 0; i < at->count; i++)
        if (at->entries[i].addr == addr)
            return at->entries[i].name[0] ? at->entries[i].name : NULL;
    return NULL;
}

const char *annotations_get_notes(const AnnotationTable *at, uint32_t addr) {
    for (int i = 0; i < at->count; i++)
        if (at->entries[i].addr == addr)
            return at->entries[i].notes[0] ? at->entries[i].notes : NULL;
    return NULL;
}

void annotations_free(AnnotationTable *at) {
    free(at->entries);
    at->entries = NULL;
    at->count   = 0;
}
