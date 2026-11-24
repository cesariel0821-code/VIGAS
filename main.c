#include <fxcg/display.h>
#include <fxcg/keyboard.h>
#include <string.h>

extern void EnableStatusArea(int opt);

#define SCREEN_W 384
#define SCREEN_H 216

/* Colores RGB565 */
#define C_WHITE   0xFFFF
#define C_BLACK   0x0000
#define C_NAVY    0x0949
#define C_BLUE    0x02B5
#define C_LBLUE   0xD6FF
#define C_SLATE   0x2965
#define C_GRAY    0x8410
#define C_LGRAY   0xE71C
#define C_BG      0xF7BE
#define C_BORDER  0xBDF7
#define C_RED     0xD800
#define C_LRED    0xFDCB
#define C_GREEN   0x0480
#define C_DGREEN  0x0320
#define C_ORANGE  0xEC00

/* Tipos de viga */
enum {
    BEAM_SIMPLE = 0,     /* Apoyada en x=0 y x=L */
    BEAM_OVERHANG = 1,   /* Con voladizos: apoyos en xA y xB */
    BEAM_CANT_L = 2,     /* Empotrada en x=0, libre en x=L */
    BEAM_CANT_R = 3,     /* Libre en x=0, empotrada en x=L */
    BEAM_FIXED_BOTH = 4, /* Doble empotrada (hiperestatica) */
    BEAM_PROP_L = 5      /* Empotrada en x=0, articulada en x=L */
};

/* Tipos de carga */
enum {
    LOAD_POINT = 0,
    LOAD_DIST = 1,
    LOAD_MOMENT = 2
};

#define MAX_LOADS 16

typedef struct {
    int type;
    double val1; /* P (kN), w1 (kN/m), o M (kN*m) */
    double val2; /* w2 (kN/m) para carga distribuida */
    double pos1; /* x o x_inicio (m) */
    double pos2; /* x_fin (m) para carga distribuida */
} Load;

/* Estado global de la viga */
static double beam_L = 6.0;
static int beam_type = BEAM_SIMPLE;
static double sup_xA = 0.0;
static double sup_xB = 6.0;
static Load loads[MAX_LOADS];
static int num_loads = 0;

/* Resultados calculados */
static double res_RA = 0.0, res_RB = 0.0;
static double res_MA = 0.0, res_MB = 0.0;
static double res_xA = 0.0, res_xB = 6.0;
static int has_supA = 1, has_supB = 1;
static int has_momA = 0, has_momB = 0;

/* Navegacion UI */
static int active_tab = 0; /* 0:DATOS, 1:CARGA, 2:REACC, 3:V y M, 4:ECUAC, 5:NUEVO */
static int cursor_datos = 0;
static int cursor_carga = 0;
static int scroll_carga = 0;
static int scroll_ecuac = 0;
static int cursor_nuevo = 0;
static double diag_cursor_x = 3.0;

/* Utilidades matematicas */
static double my_fabs(double x) { return x < 0.0 ? -x : x; }

static double my_round(double x) {
    return x >= 0.0 ? (double)(long long)(x + 0.5) : (double)(long long)(x - 0.5);
}

static void fmt_num(char *buf, double val, int dec) {
    /* buf necesita 32 bytes. Rechaza NaN y valores enormes, y saca las
       cifras dividiendo entre 10, que es constante: las rutinas de
       division de enteros con divisor variable del shim de compilacion
       no respetan la convencion de registros de SH4 y corrompen al
       que las llama. */
    int idx = 0;
    if (!(val == val)) { strcpy(buf, "?"); return; }
    if (!(val > -1e15 && val < 1e15)) { strcpy(buf, "?"); return; }
    if (dec < 0) dec = 0;
    if (dec > 6) dec = 6;
    double mult = 1.0;
    for (int i = 0; i < dec; i++) mult *= 10.0;
    if (my_fabs(val) < 0.5 / mult) val = 0.0;
    if (val < 0.0) { buf[idx++] = '-'; val = -val; }
    long long scaled = (long long)(val * mult + 0.5);
    char fd[8];
    for (int i = 0; i < dec; i++) {
        fd[i] = (char)('0' + (int)(scaled % 10));
        scaled /= 10;
    }
    char tmp[32];
    int tlen = 0;
    if (scaled == 0) tmp[tlen++] = '0';
    else while (scaled > 0 && tlen < 20) {
        tmp[tlen++] = (char)('0' + (int)(scaled % 10));
        scaled /= 10;
    }
    for (int i = tlen - 1; i >= 0; i--) buf[idx++] = tmp[i];
    if (dec > 0) {
        buf[idx++] = '.';
        for (int i = dec - 1; i >= 0; i--) buf[idx++] = fd[i];
    }
    buf[idx] = '\0';
}

static int parse_num(const char *s, double *out) {
    int i = 0;
    int sign = 1;
    int has_digits = 0;
    double int_val = 0.0;
    double frac_val = 0.0;
    double frac_div = 1.0;

    while (s[i] == ' ') i++;
    if (s[i] == '-') {
        sign = -1;
        i++;
    } else if (s[i] == '+') {
        i++;
    }
    while (s[i] >= '0' && s[i] <= '9') {
        has_digits = 1;
        int_val = int_val * 10.0 + (s[i] - '0');
        i++;
    }
    if (s[i] == '.' || s[i] == ',') {
        i++;
        while (s[i] >= '0' && s[i] <= '9') {
            has_digits = 1;
            frac_div *= 10.0;
            frac_val += (s[i] - '0') / frac_div;
            i++;
        }
    }
    if (!has_digits) return 0;
    *out = sign * (int_val + frac_val);
    return 1;
}

static void str_cat(char *dst, const char *src) {
    int i = 0, j = 0;
    while (dst[i]) i++;
    while (src[j]) dst[i++] = src[j++];
    dst[i] = '\0';
}

/* Primitivas graficas directas a VRAM */
static unsigned short *VP = 0;
static unsigned short *vram_ptr(void) {
    if (!VP) VP = (unsigned short *)GetVRAMAddress();
    return VP;
}

static void fill_rect(int x, int y, int w, int h, unsigned short color) {
    unsigned short *v = vram_ptr();
    /* recorta antes de sumar: x+w podria desbordar el entero y acabar
       escribiendo fuera de la memoria de pantalla */
    if (w <= 0 || h <= 0) return;
    if (x >= SCREEN_W || y >= SCREEN_H) return;
    if (x < -SCREEN_W) { w += x + SCREEN_W; x = -SCREEN_W; }
    if (y < -SCREEN_H) { h += y + SCREEN_H; y = -SCREEN_H; }
    if (w <= 0 || h <= 0) return;
    if (w > 2 * SCREEN_W) w = 2 * SCREEN_W;
    if (h > 2 * SCREEN_H) h = 2 * SCREEN_H;
    int x2 = x + w, y2 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x2 > SCREEN_W) x2 = SCREEN_W;
    if (y2 > SCREEN_H) y2 = SCREEN_H;
    for (int py = y; py < y2; py++) {
        unsigned short *row = v + py * SCREEN_W;
        for (int px = x; px < x2; px++) row[px] = color;
    }
}

static void draw_rect(int x, int y, int w, int h, unsigned short color) {
    fill_rect(x, y, w, 1, color);
    fill_rect(x, y + h - 1, w, 1, color);
    fill_rect(x, y, 1, h, color);
    fill_rect(x + w - 1, y, 1, h, color);
}

static void put_px(int x, int y, unsigned short c) {
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;
    vram_ptr()[y * SCREEN_W + x] = c;
}

static unsigned short get_px(int x, int y) {
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return C_BG;
    return vram_ptr()[y * SCREEN_W + x];
}

static void draw_line(int x0, int y0, int x1, int y1, unsigned short color) {
    int dx = x1 > x0 ? (x1 - x0) : (x0 - x1);
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? (y0 - y1) : (y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int guard = 0;
    while (guard++ < 4000) {
        put_px(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* ---- metricas reales de la fuente, medidas al arrancar ---- */
static int FW = 10;
static int FH = 16;
static int FOFF = 0;
static unsigned char CWT[96];
static int TOPH = 22, TABY = 192, CY0 = 26, CY1 = 188, HDR = 19, ROW = 22;

static int tw_px(const char *s) {
    int w = 0;
    for (int i = 0; s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        w += (c >= 32 && c < 127) ? CWT[c - 32] : FW;
    }
    return w;
}

static void draw_text(int x, int y, const char *txt, unsigned short fg, unsigned short bg, int transp) {
    int px = x, py = y - FOFF;
    PrintMini(&px, &py, txt, transp ? 0x02 : 0x00, 0xFFFFFFFF, 0, 0, fg, bg, 1, 0);
}

static void draw_text_bold(int x, int y, const char *txt, unsigned short fg, unsigned short bg, int transp) {
    draw_text(x, y, txt, fg, bg, transp);
    draw_text(x + 1, y, txt, fg, bg, 1);
}

static void calibrate(void) {
    char buf[2];
    buf[1] = 0;
    for (int c = 32; c < 127; c++) {
        fill_rect(0, 0, SCREEN_W, 60, C_WHITE);
        buf[0] = (char)c;
        int x = 0, y = 10;
        PrintMini(&x, &y, buf, 0x02, 0xFFFFFFFF, 0, 0, C_BLACK, C_WHITE, 1, 0);
        CWT[c - 32] = (x > 0 && x < 40) ? (unsigned char)x : 10;
    }
    FW = CWT['M' - 32];
    if (FW < 4 || FW > 20) FW = 10;
    fill_rect(0, 0, SCREEN_W, 60, C_WHITE);
    int x = 0, y = 10;
    PrintMini(&x, &y, "AgjQ|_", 0x02, 0xFFFFFFFF, 0, 0, C_BLACK, C_WHITE, 1, 0);
    int top = -1, bot = -1;
    for (int py = 0; py < 60; py++) {
        for (int px = 0; px < SCREEN_W; px++)
            if (get_px(px, py) != C_WHITE) {
                if (top < 0) top = py;
                bot = py;
                break;
            }
    }
    if (top >= 0 && bot > top && bot - top < 40) {
        FH = bot - top + 1;
        FOFF = top - 10;
    }
    fill_rect(0, 0, SCREEN_W, 60, C_WHITE);
    TOPH = FH + 7;
    TABY = SCREEN_H - (FH + 9);
    CY0 = TOPH + 4;
    CY1 = TABY - 4;
    HDR = FH + 5;
    ROW = FH + 6;
}

static void card(int x, int y, int w, int h, const char *title) {
    fill_rect(x, y, w, h, C_WHITE);
    draw_rect(x, y, w, h, C_BORDER);
    fill_rect(x, y, w, HDR, C_SLATE);
    draw_text(x + 5, y + 1, title, C_WHITE, C_SLATE, 1);
}

/* barra de desplazamiento al borde derecho */
static void scroll_bar(int total, int vis, int sc, int ty) {
    if (total <= vis || vis <= 0) return;
    int tx = 363;
    int th = vis * ROW - 6;
    if (th < 20) th = 20;
    fill_rect(tx, ty, 9, th, C_LGRAY);
    draw_rect(tx, ty, 9, th, C_GRAY);
    int hh = (int)((double)th * (double)vis / (double)total);
    if (hh < 10) hh = 10;
    if (hh > th) hh = th;
    int oy = (int)((double)(th - hh) * (double)sc / (double)(total - vis));
    if (oy < 0) oy = 0;
    if (oy > th - hh) oy = th - hh;
    fill_rect(tx + 1, ty + oy, 7, hh, C_BLUE);
}

static int list_vis(void) {
    int h = CY1 - CY0 - HDR - 6;
    int v = 0;
    while (v < 20 && (v + 1) * ROW <= h) v++;
    if (v < 1) v = 1;
    return v;
}

/* dibuja un texto partiendolo en varias lineas si no cabe */
static int draw_wrapped(int x, int y, int w, const char *txt, unsigned short col) {
    char line[96], word[48], test[160];
    line[0] = '\0';
    int i = 0, has = 0, guard = 0;
    while (txt[i] && guard++ < 40) {
        while (txt[i] == ' ') i++;
        if (!txt[i]) break;
        int wl = 0;
        while (txt[i] && txt[i] != ' ' && wl < 47) word[wl++] = txt[i++];
        word[wl] = '\0';
        strcpy(test, line);
        if (has) str_cat(test, " ");
        str_cat(test, word);
        if (has && tw_px(test) > w) {
            draw_text_bold(x, y, line, col, C_WHITE, 1);
            y += ROW;
            strcpy(line, word);
        } else {
            strcpy(line, test);
        }
        has = 1;
    }
    if (has) {
        draw_text_bold(x, y, line, col, C_WHITE, 1);
        y += ROW;
    }
    return y;
}

/* fila etiqueta + valor alineado a la derecha */
static void row2(int y, int x, int w, const char *lbl, const char *val,
                 unsigned short cl, unsigned short cv, unsigned short bg) {
    draw_text(x, y, lbl, cl, bg, 1);
    int tx = x + w - tw_px(val);
    int mn = x + tw_px(lbl) + FW;
    if (tx < mn) tx = mn;
    draw_text_bold(tx, y, val, cv, bg, 1);
}

/* Motor de analisis estructural (isostaticas e hiperestaticas) */
static void eval_ext_cantilever(double x, double *V_out, double *M_out) {
    double V = 0.0, M = 0.0;
    for (int i = 0; i < num_loads; i++) {
        Load *ld = &loads[i];
        if (ld->type == LOAD_POINT) {
            if (x < ld->pos1 - 1e-9) {
                V += ld->val1;
                M -= ld->val1 * (ld->pos1 - x);
            }
        } else if (ld->type == LOAD_MOMENT) {
            if (x < ld->pos1 - 1e-9) {
                M -= ld->val1;
            }
        } else if (ld->type == LOAD_DIST) {
            double a = ld->pos1, b = ld->pos2;
            double d = b - a;
            if (d > 1e-9 && x < b - 1e-9) {
                if (x <= a) {
                    double W = 0.5 * (ld->val1 + ld->val2) * d;
                    double denom = ld->val1 + ld->val2;
                    double xc = (my_fabs(denom) > 1e-9)
                                ? (a + (d / 3.0) * (ld->val1 + 2.0 * ld->val2) / denom)
                                : (0.5 * (a + b));
                    V += W;
                    M -= W * (xc - x);
                } else {
                    double rem = b - x;
                    double wx = ld->val1 + (ld->val2 - ld->val1) * ((x - a) / d);
                    V += 0.5 * (wx + ld->val2) * rem;
                    M -= (rem * rem) * (wx + 2.0 * ld->val2) / 6.0;
                }
            }
        }
    }
    *V_out = V;
    *M_out = M;
}

static void solve_beam(void) {
    double L = beam_L;
    if (L < 0.01) L = 0.01;
    if (diag_cursor_x < 0.0) diag_cursor_x = 0.0;
    if (diag_cursor_x > L) diag_cursor_x = L;

    double V0, M0;
    eval_ext_cantilever(0.0, &V0, &M0);

    res_RA = 0.0; res_RB = 0.0;
    res_MA = 0.0; res_MB = 0.0;
    has_supA = 0; has_supB = 0;
    has_momA = 0; has_momB = 0;

    if (beam_type == BEAM_SIMPLE) {
        res_xA = 0.0; res_xB = L;
        has_supA = 1; has_supB = 1;
        res_RB = -M0 / L;
        res_RA = V0 - res_RB;
    } else if (beam_type == BEAM_OVERHANG) {
        if (sup_xA < 0.0) sup_xA = 0.0;
        if (sup_xB > L) sup_xB = L;
        if (sup_xB <= sup_xA + 0.01) sup_xB = sup_xA + 0.01;
        res_xA = sup_xA; res_xB = sup_xB;
        has_supA = 1; has_supB = 1;
        /* Momento externo respecto a xA: M_ext(0) + V_ext(0) * xA */
        double M_about_A = M0 + V0 * res_xA;
        res_RB = -M_about_A / (res_xB - res_xA);
        res_RA = V0 - res_RB;
    } else if (beam_type == BEAM_CANT_L) {
        res_xA = 0.0; res_xB = L;
        has_supA = 1; has_momA = 1;
        res_RA = V0;
        res_MA = M0; /* Momento interno en x=0 (reaccion empotramiento = -res_MA) */
    } else if (beam_type == BEAM_CANT_R) {
        res_xA = 0.0; res_xB = L;
        has_supB = 1; has_momB = 1;
        res_RB = V0;
        res_MB = M0 + V0 * L; /* Momento interno justo a la izquierda de L */
    } else if (beam_type == BEAM_PROP_L) {
        res_xA = 0.0; res_xB = L;
        has_supA = 1; has_supB = 1; has_momA = 1;
        /* Integracion exacta de M_ext(x)*(L - x) mediante Simpson */
        int N = 200;
        double h = L / N;
        double sum_d = 0.0;
        for (int i = 0; i <= N; i++) {
            double x = i * h;
            double vx, mx;
            eval_ext_cantilever(x, &vx, &mx);
            double w = (i == 0 || i == N) ? 1.0 : ((i % 2 == 1) ? 4.0 : 2.0);
            sum_d += w * mx * (L - x);
        }
        double deltaL = (h / 3.0) * sum_d;
        res_RB = -3.0 * deltaL / (L * L * L);
        res_RA = V0 - res_RB;
        res_MA = M0 + res_RB * L;
    } else if (beam_type == BEAM_FIXED_BOTH) {
        res_xA = 0.0; res_xB = L;
        has_supA = 1; has_supB = 1; has_momA = 1; has_momB = 1;
        int N = 200;
        double h = L / N;
        double sum_d = 0.0, sum_t = 0.0;
        for (int i = 0; i <= N; i++) {
            double x = i * h;
            double vx, mx;
            eval_ext_cantilever(x, &vx, &mx);
            double w = (i == 0 || i == N) ? 1.0 : ((i % 2 == 1) ? 4.0 : 2.0);
            sum_d += w * mx * (L - x);
            sum_t += w * mx;
        }
        double deltaL = (h / 3.0) * sum_d;
        double thetaL = (h / 3.0) * sum_t;
        double L2 = L * L, L3 = L2 * L, L4 = L2 * L2;
        res_RB = (12.0 / L4) * (0.5 * L2 * thetaL - L * deltaL);
        res_MB = (12.0 / L4) * (0.5 * L2 * deltaL - (L3 / 3.0) * thetaL);
        res_RA = V0 - res_RB;
        res_MA = M0 + res_RB * L + res_MB;
    }
}

static void eval_VM(double x, double *V_out, double *M_out) {
    double V_ext, M_ext;
    eval_ext_cantilever(x, &V_ext, &M_ext);
    /* A partir de la viga en voladizo en x=0, superponemos reacciones en xB y luego xA */
    double V = V_ext;
    double M = M_ext;
    if (has_supB && x < res_xB - 1e-9) {
        V -= res_RB;                     /* dM/dx: la reaccion resta */
        M += res_RB * (res_xB - x);
    }
    if (has_momB && x < res_xB - 1e-9) {
        M += res_MB;
    }
    if (has_supA && x < res_xA - 1e-9) {
        V -= res_RA;                     /* dM/dx: la reaccion resta */
        M += res_RA * (res_xA - x);
    }
    if (has_momA && x < res_xA - 1e-9) {
        M += res_MA;
    }
    *V_out = V;
    *M_out = M;
}

/* Editor numerico interactivo con soporte inmediato de teclado */
static int edit_number(const char *title, const char *unit, double *target, int allow_neg) {
    char buf[32];
    fmt_num(buf, *target, 2);
    int len = (int)strlen(buf);

    while (1) {
        int bw = 352, bh = HDR + 6 * ROW;
        if (bh > 150) bh = 150;
        int bx = 16, by = (SCREEN_H - bh) / 2;
        fill_rect(bx + 3, by + 3, bw, bh, C_GRAY);
        fill_rect(bx, by, bw, bh, C_WHITE);
        draw_rect(bx, by, bw, bh, C_NAVY);
        fill_rect(bx, by, bw, HDR, C_NAVY);
        draw_text_bold(bx + 8, by + 2, title, C_WHITE, C_NAVY, 1);

        int y = by + HDR + 6;
        fill_rect(bx + 12, y, bw - 24, ROW + 4, C_LBLUE);
        draw_rect(bx + 12, y, bw - 24, ROW + 4, C_BLUE);
        char disp[48];
        strcpy(disp, buf);
        str_cat(disp, "_");
        draw_text_bold(bx + 20, y + 3, disp, C_BLACK, C_LBLUE, 1);
        draw_text(bx + bw - 24 - tw_px(unit), y + 3, unit, C_NAVY, C_LBLUE, 1);

        y += ROW + 10;
        draw_text(bx + 14, y, "0-9 . escribe   DEL borra", C_SLATE, C_WHITE, 1);
        y += ROW;
        if (allow_neg) draw_text(bx + 14, y, "(-) cambia el signo", C_SLATE, C_WHITE, 1);
        y += ROW;
        draw_text(bx + 14, y, "EXE acepta   EXIT cancela", C_BLUE, C_WHITE, 1);
        Bdisp_PutDisp_DD();

        int key = 0;
        GetKey(&key);

        if (key == KEY_CTRL_EXE) {
            double v;
            if (parse_num(buf, &v)) {
                if (!allow_neg && v < 0.0) v = -v;
                if (!(v == v)) v = 0.0;
                if (v > 1e9) v = 1e9;
                if (v < -1e9) v = -1e9;
                *target = v;
                return 1;
            }
        } else if (key == KEY_CTRL_EXIT) {
            return 0;
        } else if (key == KEY_CTRL_DEL) {
            if (len > 0) { len--; buf[len] = '\0'; }
        } else if (key == KEY_CTRL_AC) {
            len = 0; buf[0] = '\0';
        } else if (key >= KEY_CHAR_0 && key <= KEY_CHAR_9) {
            if (len < 12) { buf[len++] = (char)('0' + (key - KEY_CHAR_0)); buf[len] = '\0'; }
        } else if (key == KEY_CHAR_DP || key == KEY_CHAR_COMMA) {
            int hd = 0;
            for (int i = 0; i < len; i++) if (buf[i] == '.') hd = 1;
            if (!hd && len < 12) {
                if (len == 0) buf[len++] = '0';
                buf[len++] = '.'; buf[len] = '\0';
            }
        } else if (allow_neg && (key == 0x87 || key == KEY_CHAR_MINUS || key == '-')) {
            if (len == 0) { buf[len++] = '-'; buf[len] = '\0'; }
            else if (buf[0] == '-') {
                for (int i = 0; i < len; i++) buf[i] = buf[i + 1];
                len--;
            } else if (len < 12) {
                for (int i = len; i >= 0; i--) buf[i + 1] = buf[i];
                buf[0] = '-';
                len++;
            }
        }
    }
}

/* Dibujo del esquema de la viga */
static void draw_beam_schematic(int x0, int y_beam, int width, int show_reactions) {
    double L = beam_L > 0.01 ? beam_L : 1.0;

    /* Cargas distribuidas */
    for (int i = 0; i < num_loads; i++) {
        Load *ld = &loads[i];
        if (ld->type == LOAD_DIST) {
            int xa = x0 + (int)my_round((ld->pos1 / L) * width);
            int xb = x0 + (int)my_round((ld->pos2 / L) * width);
            if (xb <= xa) xb = xa + 2;
            double maxw = my_fabs(ld->val1) > my_fabs(ld->val2) ? my_fabs(ld->val1) : my_fabs(ld->val2);
            if (maxw < 1e-6) maxw = 1.0;
            int h1 = (int)my_round((my_fabs(ld->val1) / maxw) * 16.0) + 4;
            int h2 = (int)my_round((my_fabs(ld->val2) / maxw) * 16.0) + 4;
            draw_line(xa, y_beam - 3 - h1, xb, y_beam - 3 - h2, C_ORANGE);
            draw_line(xa, y_beam - 3, xa, y_beam - 3 - h1, C_ORANGE);
            draw_line(xb, y_beam - 3, xb, y_beam - 3 - h2, C_ORANGE);
            for (int px = xa; px <= xb; px += 10) {
                double t = (double)(px - xa) / (double)(xb - xa);
                int ht = (int)my_round(h1 + t * (h2 - h1));
                draw_line(px, y_beam - 3 - ht, px, y_beam - 3, C_ORANGE);
                draw_line(px - 2, y_beam - 6, px, y_beam - 3, C_ORANGE);
                draw_line(px + 2, y_beam - 6, px, y_beam - 3, C_ORANGE);
            }
        }
    }

    /* Cuerpo de la viga */
    fill_rect(x0, y_beam - 2, width + 1, 5, C_NAVY);

    /* Cargas puntuales y momentos */
    for (int i = 0; i < num_loads; i++) {
        Load *ld = &loads[i];
        int xp = x0 + (int)my_round((ld->pos1 / L) * width);
        if (ld->type == LOAD_POINT) {
            if (ld->val1 >= 0.0) {
                draw_line(xp, y_beam - 24, xp, y_beam - 3, C_RED);
                draw_line(xp + 1, y_beam - 24, xp + 1, y_beam - 3, C_RED);
                draw_line(xp - 3, y_beam - 7, xp, y_beam - 3, C_RED);
                draw_line(xp + 3, y_beam - 7, xp, y_beam - 3, C_RED);
            } else {
                draw_line(xp, y_beam + 3, xp, y_beam + 24, C_RED);
                draw_line(xp - 3, y_beam + 7, xp, y_beam + 3, C_RED);
                draw_line(xp + 3, y_beam + 7, xp, y_beam + 3, C_RED);
            }
        } else if (ld->type == LOAD_MOMENT) {
            draw_rect(xp - 6, y_beam - 9, 13, 19, C_GREEN);
            if (ld->val1 >= 0.0) {
                draw_line(xp + 6, y_beam - 4, xp + 3, y_beam - 8, C_GREEN);
                draw_line(xp + 6, y_beam - 4, xp + 9, y_beam - 8, C_GREEN);
            } else {
                draw_line(xp - 6, y_beam - 4, xp - 9, y_beam - 8, C_GREEN);
                draw_line(xp - 6, y_beam - 4, xp - 3, y_beam - 8, C_GREEN);
            }
        }
    }

    /* Apoyos */
    int pxA = x0 + (int)my_round((res_xA / L) * width);
    int pxB = x0 + (int)my_round((res_xB / L) * width);

    if (beam_type == BEAM_CANT_L || beam_type == BEAM_FIXED_BOTH || beam_type == BEAM_PROP_L) {
        fill_rect(x0 - 4, y_beam - 14, 4, 29, C_BLACK);
        for (int dy = -12; dy <= 12; dy += 4)
            draw_line(x0 - 8, y_beam + dy + 3, x0 - 4, y_beam + dy, C_BLACK);
    } else if (has_supA) {
        for (int dy = 0; dy <= 9; dy++)
            draw_line(pxA - dy / 2, y_beam + 3 + dy, pxA + dy / 2, y_beam + 3 + dy, C_BLUE);
        draw_line(pxA - 6, y_beam + 13, pxA + 6, y_beam + 13, C_BLUE);
    }

    if (beam_type == BEAM_CANT_R || beam_type == BEAM_FIXED_BOTH) {
        fill_rect(x0 + width + 1, y_beam - 14, 4, 29, C_BLACK);
        for (int dy = -12; dy <= 12; dy += 4)
            draw_line(x0 + width + 5, y_beam + dy, x0 + width + 9, y_beam + dy - 3, C_BLACK);
    } else if (has_supB) {
        for (int dy = 0; dy <= 8; dy++)
            draw_line(pxB - dy / 2, y_beam + 3 + dy, pxB + dy / 2, y_beam + 3 + dy, C_BLUE);
        draw_line(pxB - 6, y_beam + 13, pxB + 6, y_beam + 13, C_BLUE);
    }

    if (show_reactions) {
        char rb[32], num[24];
        if (has_supA) {
            fmt_num(num, res_RA, 2);
            strcpy(rb, "RA="); str_cat(rb, num);
            int tx = pxA - tw_px(rb) / 2;
            if (tx < 10) tx = 10;
            if (tx + tw_px(rb) > SCREEN_W - 6) tx = SCREEN_W - 6 - tw_px(rb);
            draw_text(tx, y_beam + 15, rb, C_DGREEN, C_WHITE, 1);
        }
        if (has_supB) {
            fmt_num(num, res_RB, 2);
            strcpy(rb, "RB="); str_cat(rb, num);
            int tx = pxB - tw_px(rb) / 2;
            if (tx < 10) tx = 10;
            if (tx + tw_px(rb) > SCREEN_W - 6) tx = SCREEN_W - 6 - tw_px(rb);
            draw_text(tx, y_beam + 15, rb, C_DGREEN, C_WHITE, 1);
        }
    }
}

/* Cabecera superior y menu inferior F1-F6 */
static void draw_chrome(void) {
    fill_rect(0, 0, SCREEN_W, SCREEN_H, C_BG);

    fill_rect(0, 0, SCREEN_W, TOPH, C_NAVY);
    draw_text_bold(8, 4, "VIGAS", C_WHITE, C_NAVY, 1);
    char info[48], num[24];
    fmt_num(num, beam_L, 2);
    strcpy(info, "L=");
    str_cat(info, num);
    str_cat(info, " m   ");
    fmt_num(num, (double)num_loads, 0);
    str_cat(info, num);
    str_cat(info, " cargas");
    draw_text(SCREEN_W - 8 - tw_px(info), 4, info, C_LBLUE, C_NAVY, 1);

    const char *tabs[6] = { "DATOS", "CARGA", "REACC", "V y M", "ECUAC", "NUEVO" };
    fill_rect(0, TABY - 1, SCREEN_W, 1, C_SLATE);
    for (int i = 0; i < 6; i++) {
        int x = i * 64;
        int w = (i == 5) ? (SCREEN_W - x) : 64;
        int act = (i == active_tab);
        unsigned short bg = act ? C_BLUE : C_LGRAY;
        unsigned short fg = act ? C_WHITE : C_SLATE;
        fill_rect(x, TABY, w, SCREEN_H - TABY, bg);
        if (i > 0) fill_rect(x, TABY, 1, SCREEN_H - TABY, C_GRAY);
        char lb[8];
        strcpy(lb, tabs[i]);
        while (lb[0] && tw_px(lb) > w - 2) lb[strlen(lb) - 1] = '\0';
        int tx = x + (w - tw_px(lb)) / 2;
        if (tx < x + 1) tx = x + 1;
        draw_text(tx, TABY + 5, lb, fg, bg, 1);
    }
}

static const char *beam_type_name(int t) {
    switch (t) {
        case BEAM_SIMPLE:     return "Simple";
        case BEAM_OVERHANG:   return "Con voladizos";
        case BEAM_CANT_L:     return "Empotrada en A";
        case BEAM_CANT_R:     return "Empotrada en B";
        case BEAM_FIXED_BOTH: return "Doble empotrada";
        case BEAM_PROP_L:     return "Empot.-articulada";
        default:              return "Simple";
    }
}

/* Pantalla 1 (F1): INGRESO DE DATOS */
static void render_tab_datos(void) {
    int n = (beam_type == BEAM_OVERHANG) ? 5 : 3;
    if (cursor_datos >= n) cursor_datos = n - 1;
    if (cursor_datos < 0) cursor_datos = 0;

    int ch = HDR + 6 + n * ROW;
    if (CY0 + ch > CY1) ch = CY1 - CY0;
    card(8, CY0, 368, ch, "1. LA VIGA");
    draw_text(370 - tw_px("^v elegir  EXE cambiar"), CY0 + 1,
              "^v elegir  EXE cambiar", C_LBLUE, C_SLATE, 1);

    int y = CY0 + HDR + 4;
    char v[48], num[24];
    for (int i = 0; i < n; i++) {
        int sel = (i == cursor_datos);
        unsigned short bg = sel ? C_BLUE : C_WHITE;
        unsigned short cl = sel ? C_LBLUE : C_SLATE;
        unsigned short cv = sel ? C_WHITE : C_BLUE;
        fill_rect(11, y - 3, 362, ROW, bg);
        if (i == 0) {
            fmt_num(num, beam_L, 2);
            strcpy(v, num); str_cat(v, " m");
            row2(y, 16, 352, "Longitud L", v, cl, cv, bg);
        } else if (i == 1) {
            strcpy(v, "< ");
            str_cat(v, beam_type_name(beam_type));
            str_cat(v, " >");
            row2(y, 16, 352, "Apoyos", v, cl, sel ? C_WHITE : C_NAVY, bg);
        } else if (beam_type == BEAM_OVERHANG && i == 2) {
            fmt_num(num, sup_xA, 2);
            strcpy(v, num); str_cat(v, " m");
            row2(y, 16, 352, "Apoyo A en xA", v, cl, cv, bg);
        } else if (beam_type == BEAM_OVERHANG && i == 3) {
            fmt_num(num, sup_xB, 2);
            strcpy(v, num); str_cat(v, " m");
            row2(y, 16, 352, "Apoyo B en xB", v, cl, cv, bg);
        } else {
            fmt_num(num, (double)num_loads, 0);
            strcpy(v, num); 
            row2(y, 16, 352, "Cargas", v, cl, sel ? C_WHITE : C_DGREEN, bg);
        }
        y += ROW;
    }

    int y2 = CY0 + ch + 5;
    if (CY1 - y2 > 44) {
        fill_rect(8, y2, 368, CY1 - y2, C_WHITE);
        draw_rect(8, y2, 368, CY1 - y2, C_BORDER);
        draw_beam_schematic(50, y2 + (CY1 - y2) / 2, 284, 0);
    }
}

/* Formulario para agregar o editar una carga */
static void add_or_edit_load(int load_type, int edit_idx) {
    Load tmp;
    if (edit_idx >= 0) {
        tmp = loads[edit_idx];
        load_type = tmp.type;
    } else {
        tmp.type = load_type;
        tmp.val1 = 10.0;
        tmp.val2 = 10.0;
        tmp.pos1 = (load_type == LOAD_DIST) ? 0.0 : (beam_L * 0.5);
        tmp.pos2 = beam_L;
    }

    if (load_type == LOAD_POINT) {
        if (!edit_number("CARGA PUNTUAL P (+abajo)", "kN", &tmp.val1, 1)) return;
        if (!edit_number("POSICION x DE LA CARGA", "m", &tmp.pos1, 0)) return;
        if (tmp.pos1 > beam_L) tmp.pos1 = beam_L;
    } else if (load_type == LOAD_DIST) {
        if (!edit_number("CARGA DISTRIB. INICIO w1", "kN/m", &tmp.val1, 1)) return;
        if (!edit_number("CARGA DISTRIB. FIN w2", "kN/m", &tmp.val2, 1)) return;
        if (!edit_number("POSICION INICIAL x1", "m", &tmp.pos1, 0)) return;
        if (!edit_number("POSICION FINAL x2", "m", &tmp.pos2, 0)) return;
        if (tmp.pos1 > beam_L) tmp.pos1 = beam_L;
        if (tmp.pos2 > beam_L) tmp.pos2 = beam_L;
        if (tmp.pos2 < tmp.pos1) {
            double sw = tmp.pos1; tmp.pos1 = tmp.pos2; tmp.pos2 = sw;
        }
    } else if (load_type == LOAD_MOMENT) {
        if (!edit_number("MOMENTO PUNTUAL M (+hor)", "kNm", &tmp.val1, 1)) return;
        if (!edit_number("POSICION x DEL MOMENTO", "m", &tmp.pos1, 0)) return;
        if (tmp.pos1 > beam_L) tmp.pos1 = beam_L;
    }

    if (edit_idx >= 0) {
        loads[edit_idx] = tmp;
    } else if (num_loads < MAX_LOADS) {
        loads[num_loads++] = tmp;
    }
    solve_beam();
}

/* Pantalla 2 (F2): CARGAS */
static void render_tab_carga(void) {
    int total = 3 + num_loads;
    if (cursor_carga >= total) cursor_carga = total - 1;
    if (cursor_carga < 0) cursor_carga = 0;

    int vis = list_vis();
    if (cursor_carga < scroll_carga) scroll_carga = cursor_carga;
    if (cursor_carga >= scroll_carga + vis) scroll_carga = cursor_carga - vis + 1;
    if (scroll_carga > total - vis) scroll_carga = total - vis;
    if (scroll_carga < 0) scroll_carga = 0;

    card(8, CY0, 368, CY1 - CY0, "2. CARGAS");
    draw_text(370 - tw_px("EXE anade/edita  DEL borra"), CY0 + 1,
              "EXE anade/edita  DEL borra", C_LBLUE, C_SLATE, 1);

    int bar = (total > vis) ? 14 : 0;
    int y = CY0 + HDR + 4;
    for (int r = scroll_carga; r < scroll_carga + vis && r < total; r++) {
        int sel = (r == cursor_carga);
        unsigned short bg = sel ? C_BLUE : C_WHITE;
        fill_rect(11, y - 3, 362 - bar, ROW, bg);
        char line[64], n1[24], n2[24];
        if (r < 3) {
            const char *t = (r == 0) ? "+ Carga puntual  P"
                          : (r == 1) ? "+ Carga distribuida  w"
                                     : "+ Momento puntual  M";
            unsigned short c = sel ? C_WHITE : (r == 0 ? C_RED : (r == 1 ? C_ORANGE : C_DGREEN));
            draw_text_bold(16, y, t, c, bg, 1);
        } else {
            Load *ld = &loads[r - 3];
            fmt_num(n1, (double)(r - 2), 0);
            strcpy(line, "#"); str_cat(line, n1); str_cat(line, " ");
            if (ld->type == LOAD_POINT) {
                fmt_num(n1, ld->val1, 2);
                fmt_num(n2, ld->pos1, 2);
                str_cat(line, "P="); str_cat(line, n1); str_cat(line, " kN");
                str_cat(line, "  x="); str_cat(line, n2); str_cat(line, " m");
            } else if (ld->type == LOAD_DIST) {
                fmt_num(n1, ld->val1, 1);
                fmt_num(n2, ld->val2, 1);
                str_cat(line, "w="); str_cat(line, n1);
                str_cat(line, ">"); str_cat(line, n2);
                fmt_num(n1, ld->pos1, 1);
                fmt_num(n2, ld->pos2, 1);
                str_cat(line, " ["); str_cat(line, n1);
                str_cat(line, "-"); str_cat(line, n2); str_cat(line, "]");
            } else {
                fmt_num(n1, ld->val1, 2);
                fmt_num(n2, ld->pos1, 2);
                str_cat(line, "M="); str_cat(line, n1); str_cat(line, " kNm");
                str_cat(line, "  x="); str_cat(line, n2);
            }
            draw_text(16, y, line, sel ? C_WHITE : C_BLACK, bg, 1);
        }
        y += ROW;
    }
    scroll_bar(total, vis, scroll_carga, CY0 + HDR + 4);
}

/* Pantalla 3 (F3): REACCIONES */
static void render_tab_reacc(void) {
    int nf = (has_supA ? 1 : 0) + (has_supB ? 1 : 0) + (has_momA ? 1 : 0) + (has_momB ? 1 : 0);
    int hf = HDR + 6 + (nf > 0 ? nf : 1) * ROW;
    int hs = CY1 - CY0 - hf - 5;
    if (hs < 40) { hs = 40; hf = CY1 - CY0 - hs - 5; }

    fill_rect(8, CY0, 368, hs, C_WHITE);
    draw_rect(8, CY0, 368, hs, C_BORDER);
    draw_text(14, CY0 + 2, "cuerpo libre", C_GRAY, C_WHITE, 1);
    draw_beam_schematic(50, CY0 + hs / 2, 284, 1);

    card(8, CY0 + hs + 5, 368, hf, "3. REACCIONES");
    int y = CY0 + hs + 5 + HDR + 4;
    char v[48], num[24];
    if (nf == 0) {
        draw_text(16, y, "Sin apoyos definidos.", C_GRAY, C_WHITE, 1);
        return;
    }
    if (has_supA) {
        fmt_num(num, res_RA, 2);
        strcpy(v, num); str_cat(v, " kN");
        row2(y, 16, 352, "Reaccion RA", v, C_SLATE, C_BLUE, C_WHITE);
        y += ROW;
    }
    if (has_supB) {
        fmt_num(num, res_RB, 2);
        strcpy(v, num); str_cat(v, " kN");
        row2(y, 16, 352, "Reaccion RB", v, C_SLATE, C_BLUE, C_WHITE);
        y += ROW;
    }
    if (has_momA) {
        fmt_num(num, -res_MA, 2);
        strcpy(v, num); str_cat(v, " kNm");
        row2(y, 16, 352, "Momento MA", v, C_SLATE, C_RED, C_WHITE);
        y += ROW;
    }
    if (has_momB) {
        fmt_num(num, res_MB, 2);
        strcpy(v, num); str_cat(v, " kNm");
        row2(y, 16, 352, "Momento MB", v, C_SLATE, C_RED, C_WHITE);
    }
}

/* Pantalla 4 (F4): DIAGRAMAS V(x) y M(x) con cursor */
static void render_tab_vm(void) {
    double L = beam_L > 0.01 ? beam_L : 1.0;
    int N = 120;
    double V_arr[121], M_arr[121];
    double vmax = 0.0, vmin = 0.0, mmax = 0.0, mmin = 0.0;

    for (int i = 0; i <= N; i++) {
        double x = (L * (double)i) / (double)N;
        eval_VM(x, &V_arr[i], &M_arr[i]);
        if (V_arr[i] > vmax) vmax = V_arr[i];
        if (V_arr[i] < vmin) vmin = V_arr[i];
        if (M_arr[i] > mmax) mmax = M_arr[i];
        if (M_arr[i] < mmin) mmin = M_arr[i];
    }
    double vabs = my_fabs(vmax) > my_fabs(vmin) ? my_fabs(vmax) : my_fabs(vmin);
    double mabs = my_fabs(mmax) > my_fabs(mmin) ? my_fabs(mmax) : my_fabs(mmin);
    if (vabs < 1e-6) vabs = 1.0;
    if (mabs < 1e-6) mabs = 1.0;

    /* dos graficos arriba, dos lineas de datos abajo */
    int hinfo = 2 * ROW + 6;
    int hg = (CY1 - CY0 - hinfo - 4) / 2;
    if (hg < 30) hg = 30;
    int x0 = 16, w = 344;

    int ytopV = CY0, ycV = CY0 + hg / 2;
    fill_rect(8, ytopV, 368, hg, C_WHITE);
    draw_rect(8, ytopV, 368, hg, C_BORDER);
    draw_line(x0, ycV, x0 + w, ycV, C_GRAY);
    int amp = hg / 2 - 4;
    for (int i = 0; i < N; i++) {
        int px1 = x0 + (int)(((double)i * (double)w) / (double)N);
        int px2 = x0 + (int)(((double)(i + 1) * (double)w) / (double)N);
        int py1 = ycV - (int)my_round((V_arr[i] / vabs) * (double)amp);
        int py2 = ycV - (int)my_round((V_arr[i + 1] / vabs) * (double)amp);
        draw_line(px1, ycV, px1, py1, C_LBLUE);
        draw_line(px1, py1, px2, py2, C_BLUE);
    }
    draw_text_bold(12, ytopV + 2, "V(x)", C_BLUE, C_WHITE, 1);

    int ytopM = CY0 + hg + 4, ycM = ytopM + hg / 2;
    fill_rect(8, ytopM, 368, hg, C_WHITE);
    draw_rect(8, ytopM, 368, hg, C_BORDER);
    draw_line(x0, ycM, x0 + w, ycM, C_GRAY);
    for (int i = 0; i < N; i++) {
        int px1 = x0 + (int)(((double)i * (double)w) / (double)N);
        int px2 = x0 + (int)(((double)(i + 1) * (double)w) / (double)N);
        int py1 = ycM - (int)my_round((M_arr[i] / mabs) * (double)amp);
        int py2 = ycM - (int)my_round((M_arr[i + 1] / mabs) * (double)amp);
        draw_line(px1, ycM, px1, py1, C_LRED);
        draw_line(px1, py1, px2, py2, C_RED);
    }
    draw_text_bold(12, ytopM + 2, "M(x)", C_RED, C_WHITE, 1);

    /* cursor */
    int cx = x0 + (int)my_round((diag_cursor_x / L) * (double)w);
    draw_line(cx, ytopV + 1, cx, ytopM + hg - 2, C_DGREEN);

    double vc, mc;
    eval_VM(diag_cursor_x, &vc, &mc);
    char line[80], num[24];
    int yi = ytopM + hg + 4;
    fill_rect(8, yi, 368, hinfo, C_WHITE);
    draw_rect(8, yi, 368, hinfo, C_BORDER);

    /* x, V y M siempre visibles: si los tres no caben a lo ancho se
       bajan decimales y, si hace falta, se quitan las unidades. */
    char sx[40], sv[40], sm[40];
    int dec = 2, uni = 1;
    for (int intento = 0; intento < 4; intento++) {
        fmt_num(num, diag_cursor_x, 2);
        strcpy(sx, "x="); str_cat(sx, num); str_cat(sx, " m");
        fmt_num(num, vc, dec);
        strcpy(sv, "V="); str_cat(sv, num);
        if (uni) str_cat(sv, " kN");
        fmt_num(num, mc, dec);
        strcpy(sm, "M="); str_cat(sm, num);
        if (uni) str_cat(sm, " kNm");
        if (tw_px(sx) + tw_px(sv) + tw_px(sm) + 4 * FW <= 352) break;
        if (intento == 0) dec = 1;
        else if (intento == 1) uni = 0;
        else dec = 0;
    }
    int libre = 352 - tw_px(sx) - tw_px(sv) - tw_px(sm);
    int gap = libre / 2;
    if (gap < FW) gap = FW;
    int cxx = 14;
    draw_text_bold(cxx, yi + 3, sx, C_DGREEN, C_WHITE, 1);
    cxx += tw_px(sx) + gap;
    draw_text_bold(cxx, yi + 3, sv, C_BLUE, C_WHITE, 1);
    cxx += tw_px(sv) + gap;
    if (cxx + tw_px(sm) > 370) cxx = 370 - tw_px(sm);
    draw_text_bold(cxx, yi + 3, sm, C_RED, C_WHITE, 1);

    const char *enm[4] = { "V+", "V-", "M+", "M-" };
    double ev[4] = { vmax, vmin, mmax, mmin };
    unsigned short ec[4] = { C_BLUE, C_BLUE, C_RED, C_RED };
    cxx = 14;
    for (int i = 0; i < 4; i++) {
        fmt_num(num, ev[i], (my_fabs(ev[i]) < 100.0) ? 1 : 0);
        strcpy(line, enm[i]);
        str_cat(line, " ");
        str_cat(line, num);
        if (cxx + tw_px(line) > 368) break;
        draw_text(cxx, yi + 3 + ROW, line, ec[i], C_WHITE, 1);
        cxx += tw_px(line) + FW + FW / 2;
    }
}

/* Pantalla 5 (F5): ECUACIONES POR TRAMOS, un tramo por pagina */
static void render_tab_ecuac(void) {
    double pts[40];
    int np = 0;
    pts[np++] = 0.0;
    pts[np++] = beam_L;
    if (has_supA) pts[np++] = res_xA;
    if (has_supB) pts[np++] = res_xB;
    for (int i = 0; i < num_loads && np < 36; i++) {
        pts[np++] = loads[i].pos1;
        if (loads[i].type == LOAD_DIST) pts[np++] = loads[i].pos2;
    }
    for (int i = 0; i < np - 1; i++)
        for (int j = i + 1; j < np; j++)
            if (pts[j] < pts[i]) { double t = pts[i]; pts[i] = pts[j]; pts[j] = t; }
    double uniq[40];
    int nu = 0;
    for (int i = 0; i < np; i++)
        if (nu == 0 || pts[i] - uniq[nu - 1] > 1e-4) uniq[nu++] = pts[i];

    int nt = nu - 1;
    if (nt < 1) nt = 1;
    if (scroll_ecuac >= nt) scroll_ecuac = nt - 1;
    if (scroll_ecuac < 0) scroll_ecuac = 0;

    char hdr[48], n1[24], n2[24];
    fmt_num(n1, (double)(scroll_ecuac + 1), 0);
    fmt_num(n2, (double)nt, 0);
    strcpy(hdr, "5. TRAMO "); str_cat(hdr, n1);
    str_cat(hdr, " de "); str_cat(hdr, n2);
    card(8, CY0, 368, CY1 - CY0, hdr);
    if (nt > 1) draw_text(370 - tw_px("^v otro tramo"), CY0 + 1,
                          "^v otro tramo", C_LBLUE, C_SLATE, 1);

    if (nu < 2) {
        draw_text(16, CY0 + HDR + 6, "Define la viga y sus cargas.", C_GRAY, C_WHITE, 1);
        return;
    }

    double xa = uniq[scroll_ecuac], xb = uniq[scroll_ecuac + 1];
    double d = xb - xa;
    if (d < 1e-9) d = 1e-9;
    double v0, m0, v1, m1, v2, m2;
    eval_VM(xa + 1e-5 * d, &v0, &m0);
    eval_VM(0.5 * (xa + xb), &v1, &m1);
    eval_VM(xb - 1e-5 * d, &v2, &m2);
    double sv = (v2 - v0) / d;

    int y = CY0 + HDR + 6;
    char ln[80];
    fmt_num(n1, xa, 2);
    fmt_num(n2, xb, 2);
    strcpy(ln, n1); str_cat(ln, " <= x <= "); str_cat(ln, n2); str_cat(ln, " m");
    draw_text_bold(16, y, ln, C_NAVY, C_WHITE, 1);
    y += ROW + 4;

    fmt_num(n1, v0, 2);
    strcpy(ln, "V = "); str_cat(ln, n1);
    if (my_fabs(sv) > 1e-3) {
        fmt_num(n2, sv, 2);
        str_cat(ln, (sv >= 0) ? " + " : " ");
        str_cat(ln, n2); str_cat(ln, " u");
    }
    y = draw_wrapped(16, y, 340, ln, C_BLUE);

    fmt_num(n1, m0, 2);
    strcpy(ln, "M = "); str_cat(ln, n1);
    if (my_fabs(v0) > 1e-3) {
        fmt_num(n2, v0, 2);
        str_cat(ln, (v0 >= 0) ? " + " : " ");
        str_cat(ln, n2); str_cat(ln, " u");
    }
    if (my_fabs(sv) > 1e-3) {
        fmt_num(n2, 0.5 * sv, 2);
        str_cat(ln, (sv >= 0) ? " + " : " ");
        str_cat(ln, n2); str_cat(ln, " u2");
    }
    y = draw_wrapped(16, y, 340, ln, C_RED);
    y += 4;

    fmt_num(n1, xa, 2);
    strcpy(ln, "u = x - "); str_cat(ln, n1);
    if (y + FH < CY1) {
        draw_text_bold(16, y, ln, C_SLATE, C_WHITE, 1);
        y += ROW;
    }
    if (y + FH < CY1) {
        draw_text(16, y, "u se mide desde el inicio", C_GRAY, C_WHITE, 1);
        y += ROW;
    }
    if (y + FH < CY1)
        draw_text(16, y, "u2 = u al cuadrado", C_GRAY, C_WHITE, 1);
}

/* Pantalla 6 (F6): NUEVO / EJEMPLO */
static void render_tab_nuevo(void) {
    card(8, CY0, 368, HDR + 8 + 2 * ROW, "6. PROYECTO");
    int y = CY0 + HDR + 4;
    for (int i = 0; i < 2; i++) {
        int sel = (i == cursor_nuevo);
        unsigned short bg = sel ? C_BLUE : C_WHITE;
        fill_rect(11, y - 3, 362, ROW, bg);
        draw_text_bold(16, y, i == 0 ? "Viga nueva (borra todo)"
                                     : "Cargar viga de ejemplo",
                       sel ? C_WHITE : (i == 0 ? C_RED : C_BLUE), bg, 1);
        y += ROW;
    }
    int y2 = CY0 + HDR + 8 + 2 * ROW + 5;
    if (CY1 - y2 > ROW + 8) {
        fill_rect(8, y2, 368, CY1 - y2, C_WHITE);
        draw_rect(8, y2, 368, CY1 - y2, C_BORDER);
        draw_text(16, y2 + 5, "^v elegir y EXE para aplicar", C_SLATE, C_WHITE, 1);
    }
}

static void handle_key(int key) {
    if (key == KEY_CTRL_F1) { active_tab = 0; return; }
    if (key == KEY_CTRL_F2) { active_tab = 1; return; }
    if (key == KEY_CTRL_F3) { active_tab = 2; solve_beam(); return; }
    if (key == KEY_CTRL_F4) { active_tab = 3; solve_beam(); return; }
    if (key == KEY_CTRL_F5) { active_tab = 4; solve_beam(); return; }
    if (key == KEY_CTRL_F6) { active_tab = 5; return; }

    /* Sin % ni / de enteros con divisor variable: las rutinas del shim
       no respetan la convencion de registros de SH4. */
    if (active_tab == 0) {
        int n = (beam_type == BEAM_OVERHANG) ? 5 : 3;
        if (key == KEY_CTRL_UP) { cursor_datos--; if (cursor_datos < 0) cursor_datos = n - 1; }
        else if (key == KEY_CTRL_DOWN) { cursor_datos++; if (cursor_datos >= n) cursor_datos = 0; }
        else if (cursor_datos == 1 && (key == KEY_CTRL_LEFT || key == KEY_CTRL_RIGHT || key == KEY_CTRL_EXE)) {
            if (key == KEY_CTRL_LEFT) { beam_type--; if (beam_type < 0) beam_type = 5; }
            else { beam_type++; if (beam_type > 5) beam_type = 0; }
            if (beam_type == BEAM_OVERHANG) { sup_xA = 0.0; sup_xB = beam_L; }
            solve_beam();
        } else if (key == KEY_CTRL_EXE) {
            if (cursor_datos == 0) {
                if (edit_number("LONGITUD DE LA VIGA  L", "m", &beam_L, 0)) {
                    if (beam_L < 0.1) beam_L = 0.1;
                    if (sup_xB > beam_L) sup_xB = beam_L;
                    if (diag_cursor_x > beam_L) diag_cursor_x = beam_L;
                    solve_beam();
                }
            } else if (beam_type == BEAM_OVERHANG && cursor_datos == 2) {
                if (edit_number("APOYO A: POSICION xA", "m", &sup_xA, 0)) {
                    if (sup_xA > beam_L) sup_xA = beam_L;
                    solve_beam();
                }
            } else if (beam_type == BEAM_OVERHANG && cursor_datos == 3) {
                if (edit_number("APOYO B: POSICION xB", "m", &sup_xB, 0)) {
                    if (sup_xB > beam_L) sup_xB = beam_L;
                    solve_beam();
                }
            } else if (cursor_datos == n - 1) {
                active_tab = 1;
            }
        }
    } else if (active_tab == 1) {
        int total = 3 + num_loads;
        if (key == KEY_CTRL_UP) { cursor_carga--; if (cursor_carga < 0) cursor_carga = total - 1; }
        else if (key == KEY_CTRL_DOWN) { cursor_carga++; if (cursor_carga >= total) cursor_carga = 0; }
        else if (key == KEY_CTRL_EXE) {
            if (cursor_carga == 0) add_or_edit_load(LOAD_POINT, -1);
            else if (cursor_carga == 1) add_or_edit_load(LOAD_DIST, -1);
            else if (cursor_carga == 2) add_or_edit_load(LOAD_MOMENT, -1);
            else add_or_edit_load(0, cursor_carga - 3);
        } else if (key == KEY_CTRL_DEL && cursor_carga >= 3) {
            int idx = cursor_carga - 3;
            for (int i = idx; i < num_loads - 1; i++) loads[i] = loads[i + 1];
            num_loads--;
            if (cursor_carga >= 3 + num_loads) cursor_carga = 3 + num_loads - 1;
            if (cursor_carga < 0) cursor_carga = 0;
            solve_beam();
        }
    } else if (active_tab == 3) {
        double step = beam_L * 0.025;
        if (step < 1e-6) step = 1e-6;
        if (key == KEY_CTRL_LEFT) {
            diag_cursor_x -= step;
            if (diag_cursor_x < 0.0) diag_cursor_x = 0.0;
        } else if (key == KEY_CTRL_RIGHT) {
            diag_cursor_x += step;
            if (diag_cursor_x > beam_L) diag_cursor_x = beam_L;
        }
    } else if (active_tab == 4) {
        if (key == KEY_CTRL_UP) scroll_ecuac--;
        else if (key == KEY_CTRL_DOWN) scroll_ecuac++;
        else if (key == KEY_CTRL_EXIT) scroll_ecuac = 0;
    } else if (active_tab == 5) {
        if (key == KEY_CTRL_UP) { cursor_nuevo--; if (cursor_nuevo < 0) cursor_nuevo = 1; }
        else if (key == KEY_CTRL_DOWN) { cursor_nuevo++; if (cursor_nuevo > 1) cursor_nuevo = 0; }
        else if (key == KEY_CTRL_EXE) {
            if (cursor_nuevo == 0) {
                beam_L = 6.0;
                beam_type = BEAM_SIMPLE;
                num_loads = 0;
                cursor_datos = 0;
                cursor_carga = 0;
                scroll_carga = 0;
                scroll_ecuac = 0;
                diag_cursor_x = 3.0;
                active_tab = 0;
                solve_beam();
            } else {
                beam_L = 8.0;
                beam_type = BEAM_SIMPLE;
                num_loads = 2;
                loads[0].type = LOAD_DIST; loads[0].val1 = 12.0; loads[0].val2 = 12.0; loads[0].pos1 = 0.0; loads[0].pos2 = 5.0;
                loads[1].type = LOAD_POINT; loads[1].val1 = 25.0; loads[1].val2 = 0.0; loads[1].pos1 = 6.5; loads[1].pos2 = 0.0;
                diag_cursor_x = 4.0;
                cursor_carga = 0;
                scroll_carga = 0;
                scroll_ecuac = 0;
                active_tab = 0;
                solve_beam();
            }
        }
    }
}

int main(void) {
    EnableStatusArea(3);
    calibrate();
    solve_beam();

    while (1) {
        draw_chrome();
        if (active_tab == 0) render_tab_datos();
        else if (active_tab == 1) render_tab_carga();
        else if (active_tab == 2) render_tab_reacc();
        else if (active_tab == 3) render_tab_vm();
        else if (active_tab == 4) render_tab_ecuac();
        else render_tab_nuevo();
        Bdisp_PutDisp_DD();

        int key = 0;
        GetKey(&key);
        handle_key(key);
    }
    return 0;
}
