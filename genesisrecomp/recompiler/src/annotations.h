/*
 * annotations.h — CSV annotation loader interface.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t addr;
    char     name[64];
    char     notes[256];
} Annotation;

typedef struct {
    Annotation *entries;
    int         count;
} AnnotationTable;

bool        annotations_load     (AnnotationTable *out, const char *path);
const char *annotations_get_name (const AnnotationTable *at, uint32_t addr);
const char *annotations_get_notes(const AnnotationTable *at, uint32_t addr);
void        annotations_free     (AnnotationTable *at);
