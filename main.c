
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

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
    arrpush(p->points, p->points[0]);
    arrpush(p->commands, PATH_CMD_CLOSE);
    return 0;
}
void path_free(path_t* p) {
    arrfree(p->points);
    arrfree(p->commands);
}

float lerp(float a, float b, float t) { return a + (b - a) * t; }
float inverse_lerp(float a, float b, float c) { return (c - a) / (b - a); }

int compare_floats(const void* a, const void* b) {
    float arg1 = *(const float*)a;
    float arg2 = *(const float*)b;

    if (arg1 < arg2) return -1;
    if (arg1 > arg2) return 1;
    return 0;
}

void fill_path(frame_t* frame, path_t* p, color_t color) {
    if (!frame || !p || arrlen(p->points) < 3) return;
    float y_min = INFINITY;
    float y_max = -INFINITY;
    for (int i = 0; i < arrlen(p->points); ++i) {
        y_min = fminf(y_min, p->points[i].y);
        y_max = fmaxf(y_max, p->points[i].y);
    }
    float* xs = NULL;
    for (float y = floorf(y_min) + 0.5f; y < y_max; y++) {
        arrsetlen(xs, 0);
        for (int i = 1; i < arrlen(p->points); ++i) {
            point_t point0 = p->points[i - 1];
            point_t point1 = p->points[i];
            if (point0.y == point1.y) continue;
            if (y < fminf(point0.y, point1.y) || y >= fmaxf(point0.y, point1.y)) continue;
            float t = inverse_lerp(point0.y, point1.y, y);
            arrpush(xs, lerp(point0.x, point1.x, t));
        }
        qsort(xs, arrlen(xs), sizeof(float), compare_floats);
        for (int i = 0; i + 1 < arrlen(xs); i += 2) {
            int x0 = (int)ceilf(xs[i] - 0.5f);
            int x1 = (int)floorf(xs[i + 1] - 0.5f);
            frame_draw_line(frame, x0, y, x1, y, color);
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

    path_t p = {0};
    path_move_to(&p, 50, 10);
    path_line_to(&p, 90, 40);
    path_line_to(&p, 75, 90);
    path_line_to(&p, 25, 90);
    path_close(&p);

    fill_path(&frame, &p, (color_t){0, 0, 0.5, 1});

    path_t q = {0};
    path_move_to(&q, 60, 50);
    path_line_to(&q, 110, 60);
    path_line_to(&q, 80, 110);
    path_close(&q);
    fill_path(&frame, &q, (color_t){0, 1, 0, 0.5});

    frame_to_png(&frame, "out.png");

    path_free(&q);
    path_free(&p);
    free(frame.buffer);
    return 0;
}
