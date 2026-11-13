/**
 * @file lv_draw_sw_line_subpx.c
 *
 * Anti-aliased lines with sub-pixel endpoints, dashes and round caps.
 *
 * The line is a rectangle (round caps: a capsule) around the segment p1-p2.
 * Every pixel gets a coverage from its distance to the rectangle's edges, measured
 * in the rectangle's own frame: u along the line, v across it. Dashes are cut along u.
 * Each row is computed only between its two outer edges, and several rows are
 * blended at once.
 */

/*********************
 *      INCLUDES
 *********************/
#include "../../misc/lv_area_private.h"
#include "blend/lv_draw_sw_blend_private.h"
#include "../lv_draw_private.h"
#include "lv_draw_sw.h"
#include "lv_draw_sw_line_subpx.h"

#if LV_USE_DRAW_SW

/*********************
 *      DEFINES
 *********************/
#define ONE         65536   /*1 px in the 16.16 format used for u and v*/
#define HALF        32768
#define MARGIN      4096    /*1/16 px: keeps the solid middle safely inside*/
#define BLOCK_BUF   1024    /*Bytes of mask on the stack*/
#define BLOCK_ROWS  32

/**********************
 *      TYPEDEFS
 **********************/

/*Where a straight edge crosses the rows: x in 24.8, and its change per row*/
typedef struct {
    int32_t x;
    int32_t step;
} edge_t;

/*One pair of parallel limits, lo < f < hi, f = f_row + s * (x - xa)*/
typedef struct {
    edge_t lo_out, hi_out, lo_in, hi_in;
    int32_t f_row;      /*f at x = xa on the current row*/
    int32_t fy;         /*Change of f per row*/
    int32_t s;          /*Change of f per column*/
    int32_t out_lo, out_hi, in_lo, in_hi;
    int32_t margin;     /*Most f can change along a row*/
    bool flat;          /*The edges are nearly parallel to the rows: check f per row*/
} limit_t;

typedef struct {
    lv_layer_t * layer;
    const lv_area_t * clip_area;
    lv_draw_sw_blend_dsc_t blend_dsc;
    lv_area_t blend_area;
    lv_opa_t * buf;
    int32_t stride;
    int32_t x0;         /*x of buf[0]*/
    int32_t rows_max;
    int32_t rows;
    int32_t y1;
    int32_t ux1, ux2;   /*Union of the spans in the block*/
    int32_t sum_w;
    int32_t xs[BLOCK_ROWS];
    int32_t xe[BLOCK_ROWS];
} block_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static int32_t to_fp8(lv_value_precise_t v);
static uint32_t isqrt64(uint64_t x);
static void limit_init(limit_t * l, int32_t f, int32_t fy, int32_t s, int32_t xa, int32_t w, int32_t lo_out,
                       int32_t hi_out, int32_t lo_in, int32_t hi_in);
static inline bool limit_apply(const limit_t * l, int32_t * xs, int32_t * xe, int32_t * is, int32_t * ie);
static inline void limit_next_row(limit_t * l);
static inline void side_run(lv_opa_t * m, int32_t v, int32_t dv, int32_t n, int32_t k);
static void block_flush(block_t * b);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_draw_sw_line_subpx(lv_layer_t * layer, const lv_area_t * clip_area, const lv_draw_line_dsc_t * dsc)
{
    int32_t x1 = to_fp8(dsc->p1.x);
    int32_t y1 = to_fp8(dsc->p1.y);
    int32_t x2 = to_fp8(dsc->p2.x);
    int32_t y2 = to_fp8(dsc->p2.y);
    int32_t dx = x2 - x1;
    int32_t dy = y2 - y1;
    int32_t len8 = (int32_t)isqrt64((uint64_t)((int64_t)dx * dx + (int64_t)dy * dy));
    if(len8 == 0) return;

    /*Unit vector along the line, 16.16*/
    int32_t ux;
    int32_t uy;
    if(LV_ABS(dx) < 32768 && LV_ABS(dy) < 32768) {
        ux = (dx * 65536) / len8;
        uy = (dy * 65536) / len8;
    }
    else {
        ux = (int32_t)(((int64_t)dx << 16) / len8);
        uy = (int32_t)(((int64_t)dy << 16) / len8);
    }

    /*Move the line by (-0.5, -0.5), but across the line only for even widths. So a
     *horizontal or vertical line on whole pixels covers whole pixels, the same as
     *the simple fill in lv_draw_sw_line.c: x1..x2-1 and, with width 2, rows y-1 and y.*/
    int32_t ox;
    int32_t oy;
    if((dsc->width & 1) == 0) {
        ox = -128;
        oy = -128;
    }
    else {
        int32_t a = -(ux + uy) / 2;
        ox = (int32_t)(((int64_t)a * ux) >> 24);
        oy = (int32_t)(((int64_t)a * uy) >> 24);
    }
    x1 += ox;
    y1 += oy;
    x2 += ox;
    y2 += oy;

    bool round = dsc->round_start || dsc->round_end;
    bool dashed = dsc->dash_width && dsc->dash_gap;
    int32_t len = len8 << 8;
    int32_t hw = dsc->width * HALF;
    int32_t cap = round ? hw : 0;

    /*The pixels that can have coverage*/
    int32_t ext = (hw + cap + HALF) >> 8;
    lv_area_t area;
    area.x1 = (LV_MIN(x1, x2) - ext) >> 8;
    area.x2 = (LV_MAX(x1, x2) + ext + 255) >> 8;
    area.y1 = (LV_MIN(y1, y2) - ext) >> 8;
    area.y2 = (LV_MAX(y1, y2) + ext + 255) >> 8;
    if(!lv_area_intersect(&area, &area, clip_area)) return;

    block_t b;
    b.layer = layer;
    b.clip_area = clip_area;
    b.stride = lv_area_get_width(&area);
    b.x0 = area.x1;
    b.rows = 0;
    lv_opa_t stack_buf[BLOCK_BUF];
    if(b.stride <= BLOCK_BUF) {
        b.buf = stack_buf;
        b.rows_max = LV_MIN(BLOCK_BUF / b.stride, BLOCK_ROWS);
    }
    else {
        b.buf = lv_malloc(b.stride);
        LV_ASSERT_MALLOC(b.buf);
        if(b.buf == NULL) return;
        b.rows_max = 1;
    }
    lv_memzero(&b.blend_dsc, sizeof(b.blend_dsc));
    b.blend_dsc.blend_area = &b.blend_area;
    b.blend_dsc.mask_area = &b.blend_area;
    b.blend_dsc.mask_stride = b.stride;
    b.blend_dsc.color = dsc->color;
    b.blend_dsc.opa = dsc->opa;

    int32_t dash_w = dsc->dash_width * ONE;
    int32_t period = (dsc->dash_width + dsc->dash_gap) * ONE;

    /*u and v at the first pixel of the area*/
    int32_t xa = area.x1;
    int32_t rx = (xa << 8) - x1;
    int32_t ry = (area.y1 << 8) - y1;
    int32_t u_row = (int32_t)(((int64_t)rx * ux + (int64_t)ry * uy) >> 8);
    int32_t v_row = (int32_t)(((int64_t)ry * ux - (int64_t)rx * uy) >> 8);

    bool has_inner = !dashed && hw >= ONE;
    limit_t lv;
    limit_t lu;
    int32_t aw = lv_area_get_width(&area);
    limit_init(&lv, v_row, ux, -uy, xa, aw, -hw - HALF, hw + HALF, -hw + HALF + MARGIN, hw - HALF - MARGIN);
    int32_t cap_start = dsc->round_start ? hw : 0;
    int32_t cap_end = dsc->round_end ? hw : 0;
    limit_init(&lu, u_row, uy, ux, xa, aw, -cap_start - HALF, len + cap_end + HALF, HALF + MARGIN, len - HALF - MARGIN);

    int32_t y;
    for(y = area.y1; y <= area.y2; y++, limit_next_row(&lv), limit_next_row(&lu)) {
        int32_t xs = area.x1;
        int32_t xe = area.x2;
        int32_t is = area.x1;
        int32_t ie = area.x2;
        if(!limit_apply(&lv, &xs, &xe, &is, &ie)) continue;
        if(!limit_apply(&lu, &xs, &xe, &is, &ie)) continue;
        if(xs > xe) continue;
        /*A short solid run is cheaper to compute than to memset*/
        if(!has_inner || ie - is < 8) is = xe + 1;
        else {
            if(is < xs) is = xs;
            if(ie > xe) ie = xe;
        }

        /*Start a new block if this row would make it too sparse*/
        if(b.rows) {
            int32_t nx1 = LV_MIN(b.ux1, xs);
            int32_t nx2 = LV_MAX(b.ux2, xe);
            int32_t w = xe - xs + 1;
            if(b.rows == b.rows_max || (nx2 - nx1 + 1) * (b.rows + 1) > 2 * (b.sum_w + w) + 32) {
                block_flush(&b);
            }
        }
        if(b.rows == 0) {
            b.y1 = y;
            b.ux1 = xs;
            b.ux2 = xe;
            b.sum_w = 0;
        }
        else {
            if(xs < b.ux1) b.ux1 = xs;
            if(xe > b.ux2) b.ux2 = xe;
        }
        b.xs[b.rows] = xs;
        b.xe[b.rows] = xe;
        b.sum_w += xe - xs + 1;

        int32_t u = lu.f_row + ux * (xs - xa);
        int32_t v = lv.f_row - uy * (xs - xa);
        int32_t ph = 0;
        if(dashed) {
            ph = u % period;
            if(ph < 0) ph += period;
        }

        lv_opa_t * m = b.buf + b.rows * b.stride + (xs - b.x0);

        /*Away from the ends only the distance across the line matters*/
        int32_t u_end = u + ux * (xe - xs);
        if(!dashed && LV_MIN(u, u_end) >= HALF && LV_MAX(u, u_end) <= len - HALF) {
            int32_t k = hw + HALF;
            if(is <= xe) {
                side_run(m, v, -uy, is - xs, k);
                lv_memset(m + (is - xs), 0xff, ie - is + 1);
                side_run(m + (ie - xs) + 1, v - uy * (ie - xs + 1), -uy, xe - ie, k);
            }
            else {
                side_run(m, v, -uy, xe - xs + 1, k);
            }
            b.rows++;
            continue;
        }

        int32_t x;
        for(x = xs; x <= xe; x++, m++, u += ux, v -= uy) {
            if(x == is) {
                int32_t n = ie - is + 1;
                lv_memset(m, 0xff, n);
                m += n - 1;
                x = ie;
                u += ux * (n - 1);
                v -= uy * (n - 1);
                continue;
            }

            int32_t cv;
            int32_t cu;
            if((dsc->round_start && u < 0) || (dsc->round_end && u > len)) {
                int32_t du = (u < 0 ? -u : u - len) >> 8;
                int32_t dv = (v < 0 ? -v : v) >> 8;
                int32_t d = lv_sqrt32((uint32_t)(du * du + dv * dv)) << 8;
                cv = hw + HALF - d;
                cu = ONE;
            }
            else {
                cv = hw + HALF - (v < 0 ? -v : v);
                /*A round end continues past the end point, so only a flat end fades out*/
                cu = LV_MIN(dsc->round_start ? ONE : u + HALF, dsc->round_end ? ONE : len + HALF - u);
            }
            cv = LV_CLAMP(0, cv, ONE);
            cu = LV_CLAMP(0, cu, ONE);
            int32_t c = ((cv >> 8) * (cu >> 8)) >> 8;

            if(dashed) {
                int32_t cd = LV_MAX(LV_MIN(ph + HALF, dash_w + HALF - ph), ph - period + HALF);
                cd = LV_CLAMP(0, cd, ONE);
                c = (c * (cd >> 8)) >> 8;
                ph += ux;
                while(ph >= period) ph -= period;
                while(ph < 0) ph += period;
            }
            *m = c > 255 ? 255 : (lv_opa_t)c;
        }
        b.rows++;
    }

    if(b.rows) block_flush(&b);
    if(b.buf != stack_buf) lv_free(b.buf);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static int32_t to_fp8(lv_value_precise_t v)
{
#if LV_USE_FLOAT
    float f = v * 256.0f;
    return (int32_t)(f >= 0 ? f + 0.5f : f - 0.5f);
#else
    return v * 256;
#endif
}

static uint32_t isqrt64(uint64_t x)
{
    /*Most lines are short enough for the cheaper 32 bit version*/
    if(x <= UINT32_MAX) {
        uint32_t x32 = (uint32_t)x;
        uint32_t r = 0;
        uint32_t b = (uint32_t)1 << 30;
        while(b > x32) b >>= 2;
        while(b) {
            if(x32 >= r + b) {
                x32 -= r + b;
                r = (r >> 1) + b;
            }
            else {
                r >>= 1;
            }
            b >>= 2;
        }
        return r;
    }

    uint64_t r = 0;
    uint64_t b = (uint64_t)1 << 62;
    while(b > x) b >>= 2;
    while(b) {
        if(x >= r + b) {
            x -= r + b;
            r = (r >> 1) + b;
        }
        else {
            r >>= 1;
        }
        b >>= 2;
    }
    return (uint32_t)r;
}

/*rs = 2^31 / |s|, so a division by s is a multiply. 64 bit divisions are library calls on MCUs*/
static void edge_init(edge_t * e, int32_t f, int32_t fy, int32_t s, int32_t xa, int32_t t, uint32_t rs)
{
    int64_t q = (((int64_t)t - f) * rs) >> 23;
    q = ((int64_t)xa << 8) + (s < 0 ? -q : q);
    /*Saturating is safe: such an edge is so far away that its steps can't bring it into the area*/
    e->x = (int32_t)LV_CLAMP(-(1 << 30), q, (1 << 30));
    q = ((int64_t)fy * rs) >> 23;
    e->step = (int32_t)(s < 0 ? q : -q);
}

static void limit_init(limit_t * l, int32_t f, int32_t fy, int32_t s, int32_t xa, int32_t w, int32_t lo_out,
                       int32_t hi_out, int32_t lo_in, int32_t hi_in)
{
    l->f_row = f;
    l->fy = fy;
    l->s = s;
    l->out_lo = lo_out;
    l->out_hi = hi_out;
    l->in_lo = lo_in;
    l->in_hi = hi_in;

    /*An edge that moves more than 256 px per row is checked per row instead, with a margin
     *for how much f can change along the row*/
    int32_t s_abs = s < 0 ? -s : s;
    int32_t fy_abs = fy < 0 ? -fy : fy;
    l->flat = (int64_t)s_abs * 256 <= fy_abs;
    l->margin = (int32_t)LV_MIN((int64_t)s_abs * (w + 1), INT32_MAX / 4);
    if(l->flat) return;

    /*With a negative slope the upper limit gives the left edge*/
    if(s < 0) {
        int32_t t = lo_out;
        lo_out = hi_out;
        hi_out = t;
        t = lo_in;
        lo_in = hi_in;
        hi_in = t;
    }
    uint32_t rs = 0x80000000u / (uint32_t)(s < 0 ? -s : s);
    edge_init(&l->lo_out, f, fy, s, xa, lo_out, rs);
    edge_init(&l->hi_out, f, fy, s, xa, hi_out, rs);
    edge_init(&l->lo_in, f, fy, s, xa, lo_in, rs);
    edge_init(&l->hi_in, f, fy, s, xa, hi_in, rs);
}

/**
 * Narrow the outer span [xs, xe] and the inner span [is, ie] to this limit on the current row.
 * @return false if the row has no coverage at all
 */
static inline bool limit_apply(const limit_t * l, int32_t * xs, int32_t * xe, int32_t * is, int32_t * ie)
{
    if(l->flat) {
        if(l->f_row <= l->out_lo - l->margin || l->f_row >= l->out_hi + l->margin) return false;
        if(l->f_row < l->in_lo + l->margin || l->f_row > l->in_hi - l->margin) *is = *ie + 1;
        return true;
    }

    int32_t a = l->lo_out.x >> 8;
    int32_t b = (l->hi_out.x + 255) >> 8;
    if(a > *xs) *xs = a;
    if(b < *xe) *xe = b;
    a = (l->lo_in.x + 255) >> 8;
    b = l->hi_in.x >> 8;
    if(a > *is) *is = a;
    if(b < *ie) *ie = b;
    return true;
}

static inline void limit_next_row(limit_t * l)
{
    l->f_row += l->fy;
    if(l->flat) return;
    l->lo_out.x += l->lo_out.step;
    l->hi_out.x += l->hi_out.step;
    l->lo_in.x += l->lo_in.step;
    l->hi_in.x += l->hi_in.step;
}

/*Coverage from the distance across the line only: k - |v|, v changes by dv per pixel*/
static inline void side_run(lv_opa_t * m, int32_t v, int32_t dv, int32_t n, int32_t k)
{
    while(n-- > 0) {
        int32_t c = k - (v < 0 ? -v : v);
        *m++ = c <= 0 ? 0 : c >= ONE ? 255 : (lv_opa_t)(c >> 8);
        v += dv;
    }
}

static void block_flush(block_t * b)
{
    /*Clear what the union adds around each row's own span*/
    int32_t r;
    for(r = 0; r < b->rows; r++) {
        lv_opa_t * row = b->buf + r * b->stride - b->x0;
        /*A select, not a fill: GCC turns a fill loop into a memset call, which costs
         *more than the loop for these few pixels*/
        int32_t xs = b->xs[r];
        int32_t xe = b->xe[r];
        int32_t x;
        for(x = b->ux1; x <= b->ux2; x++) {
            if(x < xs || x > xe) row[x] = 0;
        }
    }

    b->blend_area.x1 = b->ux1;
    b->blend_area.x2 = b->ux2;
    b->blend_area.y1 = b->y1;
    b->blend_area.y2 = b->y1 + b->rows - 1;
    b->blend_dsc.mask_buf = b->buf + (b->ux1 - b->x0);
    b->blend_dsc.mask_res = LV_DRAW_SW_MASK_RES_CHANGED;
    lv_draw_sw_blend(b->layer, b->clip_area, &b->blend_dsc);
    b->rows = 0;
}

#endif /*LV_USE_DRAW_SW*/
