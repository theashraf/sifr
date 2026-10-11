
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

typedef enum { FILL_RULE_NON_ZERO, FILL_RULE_EVEN_ODD } FILL_RULE;
typedef struct {
    float x, y;
} point_t;
typedef struct {
    float r, g, b, a;
} color_t;
typedef struct {
    uint8_t* buffer;
    uint32_t width, height;
} frame_t;

int frame_to_png(frame_t* f, const char* filename) {
    return f ? !stbi_write_png(filename, f->width, f->height, 3, f->buffer, f->width * 3) : 1;
}
color_t color_premultiply(color_t c) { return (color_t){c.r * c.a, c.g * c.a, c.b * c.a, c.a}; }
float clamp01(float v) { return fminf(fmaxf(v, 0), 1); }
uint8_t color_to_byte(float v) { return (uint8_t)(clamp01(v) * 255); }
void frame_set_pixel(frame_t* f, int x, int y, color_t color) {
    if (x < 0 || y < 0 || x >= (int)f->width || y >= (int)f->height) return;
    uint8_t* px = f->buffer + (y * f->width + x) * 3;
    px[0] = color_to_byte(color.r);
    px[1] = color_to_byte(color.g);
    px[2] = color_to_byte(color.b);
}
void frame_put_pixel(frame_t* f, int x, int y, color_t color) {
    if (x < 0 || y < 0 || x >= (int)f->width || y >= (int)f->height) return;
    color_t src = color_premultiply(color);
    float inv = 1 - src.a;
    uint8_t* px = f->buffer + (y * f->width + x) * 3;
    px[0] = color_to_byte(src.r + px[0] / 255.0f * inv);
    px[1] = color_to_byte(src.g + px[1] / 255.0f * inv);
    px[2] = color_to_byte(src.b + px[2] / 255.0f * inv);
}
void frame_clear(frame_t* f, color_t color) {
    for (int y = 0; y < (int)f->height; ++y)
        for (int x = 0; x < (int)f->width; ++x) frame_set_pixel(f, x, y, color);
}
int frame_draw_line(frame_t* f, float x0, float y0, float x1, float y1, color_t color) {
    if (!f) return 1;
    float dx = x1 - x0;
    float dy = y1 - y0;
    int steps = (int)ceilf(fmaxf(fabsf(dx), fabsf(dy)));
    if (steps == 0) steps = 1;
    for (int i = 0; i <= steps; ++i) {
        float t = (float)i / steps;
        frame_put_pixel(f, (int)roundf(x0 + dx * t), (int)roundf(y0 + dy * t), color);
    }
    return 0;
}
typedef enum { PATH_CMD_MOVE_TO, PATH_CMD_LINE_TO, PATH_CMD_CLOSE } PATH_CMD;
typedef struct {
    PATH_CMD* commands;
    point_t* points;
} path_t;
int path_move_to(path_t* p, float x, float y) {
    if (!p) return 1;
    arrpush(p->points, ((point_t){x, y}));
    arrpush(p->commands, PATH_CMD_MOVE_TO);
    return 0;
}
int path_line_to(path_t* p, float x, float y) {
    if (!p) return 1;
    arrpush(p->points, ((point_t){x, y}));
    arrpush(p->commands, PATH_CMD_LINE_TO);
    return 0;
}
int path_close(path_t* p) {
    if (!p || arrlen(p->points) == 0) return 1;
    int start = (int)arrlen(p->commands) - 1;
    while (start > 0 && p->commands[start] != PATH_CMD_MOVE_TO) --start;
    arrpush(p->points, p->points[start]);
    arrpush(p->commands, PATH_CMD_CLOSE);
    return 0;
}
void path_free(path_t* p) {
    arrfree(p->points);
    arrfree(p->commands);
}

float lerp(float a, float b, float t) { return a + (b - a) * t; }
float inverse_lerp(float a, float b, float c) { return (c - a) / (b - a); }

typedef struct {
    float x;
    int dir;
} crossing_t;
int compare_crossings(const void* a, const void* b) {
    crossing_t arg1 = *(const crossing_t*)a;
    crossing_t arg2 = *(const crossing_t*)b;

    if (arg1.x < arg2.x) return -1;
    if (arg1.x > arg2.x) return 1;
    return 0;
}

void fill_path(frame_t* frame, path_t* p, color_t color, FILL_RULE rule) {
    if (!frame || !p || arrlen(p->points) < 3) return;
    float y_min = INFINITY;
    float y_max = -INFINITY;
    for (int i = 0; i < arrlen(p->points); ++i) {
        y_min = fminf(y_min, p->points[i].y);
        y_max = fmaxf(y_max, p->points[i].y);
    }
    crossing_t* xs = NULL;
    for (float y = floorf(y_min); y < y_max; y++) {
        arrsetlen(xs, 0);
        for (int i = 1; i < arrlen(p->points); ++i) {
            point_t point0 = p->points[i - 1];
            point_t point1 = p->points[i];
            if (p->commands[i] == PATH_CMD_MOVE_TO) continue;
            if (point0.y == point1.y) continue;
            if (y < fminf(point0.y, point1.y) || y >= fmaxf(point0.y, point1.y)) continue;
            float t = inverse_lerp(point0.y, point1.y, y);
            crossing_t crossing = {.x = lerp(point0.x, point1.x, t), .dir = point0.y > point1.y ? 1 : -1};
            arrpush(xs, crossing);
        }
        qsort(xs, arrlen(xs), sizeof(crossing_t), compare_crossings);
        int winding = 0;
        for (int i = 0; i + 1 < arrlen(xs); ++i) {
            winding += xs[i].dir;
            int inside = rule == FILL_RULE_NON_ZERO ? winding != 0 : (i & 1) == 0;
            if (!inside) continue;
            int x0 = (int)ceilf(xs[i].x);
            int x1 = (int)floorf(xs[i + 1].x);
            if (x1 >= x0) frame_draw_line(frame, x0, y, x1, y, color);
        }
    }
    arrfree(xs);
}

int main() {
    const int W = 100;
    const int H = 100;

    frame_t frame = {
        .buffer = (uint8_t*)malloc(W * H * 3),
        .width = W,
        .height = H,
    };
    frame_clear(&frame, (color_t){1, 1, 1, 1});

    FILL_RULE rules[2] = {FILL_RULE_NON_ZERO, FILL_RULE_EVEN_ODD};
    color_t colors[2] = {{0.1f, 0.3f, 0.9f, 1}, {0.9f, 0.2f, 0.2f, 1}};
    for (int i = 0; i < 2; ++i) {
        float cx = 25 + i * 50;

        path_t sq = {0};
        path_move_to(&sq, cx - 20, 30);
        path_line_to(&sq, cx + 20, 30);
        path_line_to(&sq, cx + 20, 70);
        path_line_to(&sq, cx - 20, 70);
        path_close(&sq);
        path_move_to(&sq, cx - 10, 40);
        path_line_to(&sq, cx + 10, 40);
        path_line_to(&sq, cx + 10, 60);
        path_line_to(&sq, cx - 10, 60);
        path_close(&sq);
        fill_path(&frame, &sq, colors[i], rules[i]);
        path_free(&sq);
    }

    frame_to_png(&frame, "out.png");

    free(frame.buffer);
    return 0;
}
