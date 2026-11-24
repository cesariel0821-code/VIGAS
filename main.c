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
static double diag_cursor_x = 3.0;

/* Utilidades matematicas */
static double my_fabs(double x) { return x < 0.0 ? -x : x; }

static double my_round(double x) {
    return x >= 0.0 ? (double)(long long)(x + 0.5) : (double)(long long)(x - 0.5);
}

static void fmt_num(char *buf, double val, int dec) {
    int idx = 0;
    double mult = 1.0;
    for (int i = 0; i < dec; i++) mult *= 10.0;
    double threshold = 0.5 / mult;
    if (my_fabs(val) < threshold) val = 0.0;
    if (val < 0.0) {
        buf[idx++] = '-';
        val = -val;
    }
    long long scaled = (long long)(val * mult + 0.5);
    long long imult = (long long)(mult + 0.5);
    long long ipart = (dec > 0) ? (scaled / imult) : scaled;
    long long fpart = (dec > 0) ? (scaled % imult) : 0;

    char tmp[24];
    int tlen = 0;
    if (ipart == 0) {
        tmp[tlen++] = '0';
    } else {
        while (ipart > 0 && tlen < 20) {
            tmp[tlen++] = (char)('0' + (ipart % 10));
            ipart /= 10;
        }
    }
    for (int i = tlen - 1; i >= 0; i--) buf[idx++] = tmp[i];

    if (dec > 0) {
        buf[idx++] = '.';
        for (int i = dec - 1; i >= 0; i--) {
            buf[idx + i] = (char)('0' + (fpart % 10));
            fpart /= 10;
        }
        idx += dec;
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
static unsigned short *vram_ptr(void) {
    return (unsigned short *)GetVRAMAddress();
}

static void fill_rect(int x, int y, int w, int h, unsigned short color) {
    unsigned short *v = vram_ptr();
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

static void draw_line(int x0, int y0, int x1, int y1, unsigned short color) {
    unsigned short *v = vram_ptr();
    int dx = x1 > x0 ? (x1 - x0) : (x0 - x1);
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? (y0 - y1) : (y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        if (x0 >= 0 && x0 < SCREEN_W && y0 >= 0 && y0 < SCREEN_H)
            v[y0 * SCREEN_W + x0] = color;
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void draw_text(int x, int y, const char *txt, unsigned short fg, unsigned short bg, int transp) {
    int px = x, py = y;
    PrintMini(&px, &py, txt, transp ? 0x02 : 0x00, 0xFFFFFFFF, 0, 0, fg, bg, 1, 0);
}

static void draw_text_bold(int x, int y, const char *txt, unsigned short fg, unsigned short bg, int transp) {
    draw_text(x, y, txt, fg, bg, transp);
    draw_text(x + 1, y, txt, fg, bg, 1);
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
        res_RB = (12.0 / L4) * (0.5 * L * thetaL - deltaL);
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
        V += res_RB;
        M += res_RB * (res_xB - x);
    }
    if (has_momB && x < res_xB - 1e-9) {
        M += res_MB;
    }
    if (has_supA && x < res_xA - 1e-9) {
        V += res_RA;
        M += res_RA * (res_xA - x);
    }
    if (has_momA && x < res_xA - 1e-9) {
        M += res_MA;
    }
    *V_out = V;
    *M_out = M;
}

/* Editor numerico interactivo con soporte inmediato de teclado */
static int edit_number(const char *title, const char *unit, double *target, int allow_neg, char first_char) {
    char buf[20];
    int len = 0;

    if (first_char != 0) {
        if (first_char == '-' && !allow_neg) return 0;
        buf[0] = first_char;
        buf[1] = '\0';
        len = 1;
    } else {
        fmt_num(buf, *target, 2);
        len = (int)strlen(buf);
    }

    while (1) {
        /* Ventana modal centrada */
        int bx = 36, by = 46, bw = 312, bh = 112;
        fill_rect(bx + 3, by + 3, bw, bh, C_GRAY);
        fill_rect(bx, by, bw, bh, C_WHITE);
        draw_rect(bx, by, bw, bh, C_NAVY);
        fill_rect(bx, by, bw, 22, C_NAVY);
        draw_text_bold(bx + 8, by + 4, title, C_WHITE, C_NAVY, 1);

        /* Caja de entrada */
        fill_rect(bx + 14, by + 34, bw - 28, 28, C_LBLUE);
        draw_rect(bx + 14, by + 34, bw - 28, 28, C_BLUE);

        char disp[28];
        strcpy(disp, buf);
        str_cat(disp, "_");
        draw_text_bold(bx + 22, by + 40, disp, C_BLACK, C_LBLUE, 1);
        draw_text(bx + bw - 48, by + 40, unit, C_NAVY, C_LBLUE, 1);

        draw_text(bx + 14, by + 72, "[0-9 . -] Escribir   [DEL] Borrar", C_SLATE, C_WHITE, 1);
        draw_text(bx + 14, by + 89, "[EXE] Confirmar      [EXIT] Cancelar", C_BLUE, C_WHITE, 1);

        Bdisp_PutDisp_DD();

        int key = 0;
        GetKey(&key);

        if (key == KEY_CTRL_EXE) {
            double val;
            if (parse_num(buf, &val)) {
                if (!allow_neg && val < 0.0) val = -val;
                *target = val;
                return 1;
            }
        } else if (key == KEY_CTRL_EXIT) {
            return 0;
        } else if (key == KEY_CTRL_DEL) {
            if (len > 0) {
                len--;
                buf[len] = '\0';
            }
        } else if (key == KEY_CTRL_AC) {
            len = 0;
            buf[0] = '\0';
        } else if (key >= KEY_CHAR_0 && key <= KEY_CHAR_9) {
            if (len < 14) {
                buf[len++] = (char)('0' + (key - KEY_CHAR_0));
                buf[len] = '\0';
            }
        } else if (key == KEY_CHAR_DP || key == KEY_CHAR_COMMA) {
            int has_dot = 0;
            for (int i = 0; i < len; i++) if (buf[i] == '.') has_dot = 1;
            if (!has_dot && len < 14) {
                if (len == 0) buf[len++] = '0';
                buf[len++] = '.';
                buf[len] = '\0';
            }
        } else if (key == 0x87 || key == KEY_CHAR_MINUS || key == '-') {
            if (allow_neg) {
                if (len == 0) {
                    buf[len++] = '-';
                    buf[len] = '\0';
                } else if (buf[0] == '-') {
                    for (int i = 0; i < len; i++) buf[i] = buf[i + 1];
                    len--;
                } else if (len < 14) {
                    for (int i = len; i >= 0; i--) buf[i + 1] = buf[i];
                    buf[0] = '-';
                    len++;
                }
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
        char rb[20], num[14];
        if (has_supA) {
            fmt_num(num, res_RA, 2);
            strcpy(rb, "RA="); str_cat(rb, num);
            draw_text(pxA - 12, y_beam + 16, rb, C_GREEN, C_WHITE, 1);
        }
        if (has_supB) {
            fmt_num(num, res_RB, 2);
            strcpy(rb, "RB="); str_cat(rb, num);
            draw_text(pxB - 38, y_beam + 16, rb, C_GREEN, C_WHITE, 1);
        }
    }
}

/* Cabecera superior y menu inferior F1-F6 */
static void draw_chrome(void) {
    fill_rect(0, 0, SCREEN_W, SCREEN_H, C_BG);

    /* Barra superior */
    fill_rect(0, 0, SCREEN_W, 21, C_NAVY);
    draw_text_bold(8, 3, "VIGAS PRO", C_WHITE, C_NAVY, 1);

    char info[36], num[14];
    fmt_num(num, beam_L, 2);
    strcpy(info, "L=");
    str_cat(info, num);
    str_cat(info, "m | Cargas:");
    fmt_num(num, (double)num_loads, 0);
    str_cat(info, num);
    draw_text(215, 3, info, C_LBLUE, C_NAVY, 1);

    /* Menu inferior F1 - F6 */
    const char *tabs[6] = {"DATOS", "CARGA", "REACC", "V y M", "ECUAC", "NUEVO"};
    int y_bar = 192;
    fill_rect(0, y_bar - 1, SCREEN_W, 1, C_BLACK);
    for (int i = 0; i < 6; i++) {
        int x = i * 64;
        int w = (i == 5) ? (SCREEN_W - x) : 64;
        int active = (i == active_tab);
        unsigned short bg = active ? C_BLUE : C_SLATE;
        unsigned short fg = C_WHITE;
        fill_rect(x, y_bar, w, SCREEN_H - y_bar, bg);
        if (i > 0) fill_rect(x, y_bar, 1, SCREEN_H - y_bar, C_BLACK);
        if (active) {
            fill_rect(x + 2, y_bar + 1, w - 4, 2, C_LBLUE);
            draw_text_bold(x + 8, y_bar + 5, tabs[i], fg, bg, 1);
        } else {
            draw_text(x + 9, y_bar + 5, tabs[i], fg, bg, 1);
        }
    }
}

static const char *beam_type_name(int t) {
    switch (t) {
        case BEAM_SIMPLE:     return "Simple (Artic-Rodillo)";
        case BEAM_OVERHANG:   return "Con Voladizos (xA, xB)";
        case BEAM_CANT_L:     return "Voladizo Izq (Empot. A)";
        case BEAM_CANT_R:     return "Voladizo Der (Empot. B)";
        case BEAM_FIXED_BOTH: return "Doble Empotrada (Hiper)";
        case BEAM_PROP_L:     return "Empot-Artic (Hiper)";
        default:              return "Simple";
    }
}

/* Pantalla 1 (F1): INGRESO DE DATOS */
static void render_tab_datos(void) {
    /* Tarjeta esquema */
    fill_rect(8, 25, SCREEN_W - 16, 62, C_WHITE);
    draw_rect(8, 25, SCREEN_W - 16, 62, C_BORDER);
    draw_text(14, 28, "1. CONFIGURACION DE LA VIGA", C_NAVY, C_WHITE, 1);
    draw_beam_schematic(44, 62, 296, 0);

    /* Filas editables */
    int max_items = (beam_type == BEAM_OVERHANG) ? 5 : 3;
    if (cursor_datos >= max_items) cursor_datos = max_items - 1;

    int y = 92;
    int row_h = (beam_type == BEAM_OVERHANG) ? 18 : 24;

    for (int idx = 0; idx < max_items; idx++) {
        int sel = (idx == cursor_datos);
        unsigned short rbg = sel ? C_LBLUE : C_WHITE;
        unsigned short rbd = sel ? C_BLUE : C_BORDER;
        fill_rect(8, y, SCREEN_W - 16, row_h - 2, rbg);
        draw_rect(8, y, SCREEN_W - 16, row_h - 2, rbd);

        char val_str[36], num[16];
        int ty = y + (row_h - 16) / 2;

        if (idx == 0) {
            draw_text_bold(14, ty, "Longitud Viga (L):", C_BLACK, rbg, 1);
            fmt_num(num, beam_L, 2);
            strcpy(val_str, num); str_cat(val_str, " m");
            draw_text_bold(220, ty, val_str, C_BLUE, rbg, 1);
        } else if (idx == 1) {
            draw_text_bold(14, ty, "Tipo de Apoyo:", C_BLACK, rbg, 1);
            strcpy(val_str, "< ");
            str_cat(val_str, beam_type_name(beam_type));
            str_cat(val_str, " >");
            draw_text(158, ty, val_str, C_NAVY, rbg, 1);
        } else if (beam_type == BEAM_OVERHANG && idx == 2) {
            draw_text_bold(14, ty, "Pos. Apoyo A (xA):", C_BLACK, rbg, 1);
            fmt_num(num, sup_xA, 2);
            strcpy(val_str, num); str_cat(val_str, " m");
            draw_text_bold(220, ty, val_str, C_BLUE, rbg, 1);
        } else if (beam_type == BEAM_OVERHANG && idx == 3) {
            draw_text_bold(14, ty, "Pos. Apoyo B (xB):", C_BLACK, rbg, 1);
            fmt_num(num, sup_xB, 2);
            strcpy(val_str, num); str_cat(val_str, " m");
            draw_text_bold(220, ty, val_str, C_BLUE, rbg, 1);
        } else {
            draw_text_bold(14, ty, "Cargas Aplicadas:", C_BLACK, rbg, 1);
            fmt_num(num, (double)num_loads, 0);
            strcpy(val_str, num);
            str_cat(val_str, " cargas -> [F2 / EXE]");
            draw_text_bold(185, ty, val_str, C_GREEN, rbg, 1);
        }
        y += row_h;
    }

    draw_text(10, 174, "[0-9] Escribir valor   [</>] Apoyo   [F2] Cargas", C_SLATE, C_BG, 1);
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
        if (!edit_number("CARGA PUNTUAL P (+abajo)", "kN", &tmp.val1, 1, 0)) return;
        if (!edit_number("POSICION x DE LA CARGA", "m", &tmp.pos1, 0, 0)) return;
        if (tmp.pos1 > beam_L) tmp.pos1 = beam_L;
    } else if (load_type == LOAD_DIST) {
        if (!edit_number("CARGA DISTRIB. INICIO w1", "kN/m", &tmp.val1, 1, 0)) return;
        if (!edit_number("CARGA DISTRIB. FIN w2", "kN/m", &tmp.val2, 1, 0)) return;
        if (!edit_number("POSICION INICIAL x1", "m", &tmp.pos1, 0, 0)) return;
        if (!edit_number("POSICION FINAL x2", "m", &tmp.pos2, 0, 0)) return;
        if (tmp.pos1 > beam_L) tmp.pos1 = beam_L;
        if (tmp.pos2 > beam_L) tmp.pos2 = beam_L;
        if (tmp.pos2 < tmp.pos1) {
            double sw = tmp.pos1; tmp.pos1 = tmp.pos2; tmp.pos2 = sw;
        }
    } else if (load_type == LOAD_MOMENT) {
        if (!edit_number("MOMENTO PUNTUAL M (+hor)", "kNm", &tmp.val1, 1, 0)) return;
        if (!edit_number("POSICION x DEL MOMENTO", "m", &tmp.pos1, 0, 0)) return;
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
    int total_rows = 3 + num_loads;
    if (cursor_carga >= total_rows) cursor_carga = total_rows - 1;
    if (cursor_carga < 0) cursor_carga = 0;

    int vis = 7;
    if (cursor_carga < scroll_carga) scroll_carga = cursor_carga;
    if (cursor_carga >= scroll_carga + vis) scroll_carga = cursor_carga - vis + 1;

    int y = 25;
    for (int r = scroll_carga; r < scroll_carga + vis && r < total_rows; r++) {
        int sel = (r == cursor_carga);
        unsigned short rbg = sel ? C_LBLUE : C_WHITE;
        unsigned short rbd = sel ? C_BLUE : C_BORDER;
        fill_rect(8, y, SCREEN_W - 16, 19, rbg);
        draw_rect(8, y, SCREEN_W - 16, 19, rbd);

        if (r == 0) {
            draw_text_bold(14, y + 2, "+ Agregar Carga Puntual (P)", C_RED, rbg, 1);
        } else if (r == 1) {
            draw_text_bold(14, y + 2, "+ Agregar Carga Distribuida (w)", C_ORANGE, rbg, 1);
        } else if (r == 2) {
            draw_text_bold(14, y + 2, "+ Agregar Momento Puntual (M)", C_GREEN, rbg, 1);
        } else {
            int idx = r - 3;
            Load *ld = &loads[idx];
            char line[56], n1[12], n2[12], n3[12];
            fmt_num(n1, (double)(idx + 1), 0);
            strcpy(line, "#"); str_cat(line, n1);
            if (ld->type == LOAD_POINT) {
                fmt_num(n1, ld->val1, 2);
                fmt_num(n2, ld->pos1, 2);
                str_cat(line, " Puntual P="); str_cat(line, n1);
                str_cat(line, "kN  x="); str_cat(line, n2); str_cat(line, "m");
                draw_text(14, y + 2, line, C_BLACK, rbg, 1);
            } else if (ld->type == LOAD_DIST) {
                fmt_num(n1, ld->val1, 1);
                fmt_num(n2, ld->val2, 1);
                fmt_num(n3, ld->pos1, 1);
                str_cat(line, " Dist w="); str_cat(line, n1);
                str_cat(line, "->"); str_cat(line, n2);
                str_cat(line, " ["); str_cat(line, n3);
                fmt_num(n3, ld->pos2, 1);
                str_cat(line, "-"); str_cat(line, n3); str_cat(line, "m]");
                draw_text(14, y + 2, line, C_BLACK, rbg, 1);
            } else {
                fmt_num(n1, ld->val1, 2);
                fmt_num(n2, ld->pos1, 2);
                str_cat(line, " Momento M="); str_cat(line, n1);
                str_cat(line, "kNm  x="); str_cat(line, n2); str_cat(line, "m");
                draw_text(14, y + 2, line, C_BLACK, rbg, 1);
            }
        }
        y += 21;
    }

    draw_text(10, 174, "[EXE] Agregar/Editar   [DEL] Borrar   [F3] Reacciones", C_SLATE, C_BG, 1);
}

/* Pantalla 3 (F3): REACCIONES */
static void render_tab_reacc(void) {
    fill_rect(8, 25, SCREEN_W - 16, 74, C_WHITE);
    draw_rect(8, 25, SCREEN_W - 16, 74, C_BORDER);
    draw_text_bold(14, 28, "DIAGRAMA DE CUERPO LIBRE", C_NAVY, C_WHITE, 1);
    draw_beam_schematic(46, 64, 292, 1);

    fill_rect(8, 104, SCREEN_W - 16, 82, C_WHITE);
    draw_rect(8, 104, SCREEN_W - 16, 82, C_BORDER);

    char line[48], num[16];
    int y = 110;

    if (has_supA) {
        fmt_num(num, res_RA, 3);
        strcpy(line, "Reaccion Vertical RA = ");
        str_cat(line, num); str_cat(line, " kN");
        draw_text_bold(16, y, line, C_BLUE, C_WHITE, 1);
        y += 18;
    }
    if (has_supB) {
        fmt_num(num, res_RB, 3);
        strcpy(line, "Reaccion Vertical RB = ");
        str_cat(line, num); str_cat(line, " kN");
        draw_text_bold(16, y, line, C_BLUE, C_WHITE, 1);
        y += 18;
    }
    if (has_momA) {
        fmt_num(num, -res_MA, 3);
        strcpy(line, "Momento Empotram. MA = ");
        str_cat(line, num); str_cat(line, " kN*m");
        draw_text_bold(16, y, line, C_RED, C_WHITE, 1);
        y += 18;
    }
    if (has_momB) {
        fmt_num(num, res_MB, 3);
        strcpy(line, "Momento Empotram. MB = ");
        str_cat(line, num); str_cat(line, " kN*m");
        draw_text_bold(16, y, line, C_RED, C_WHITE, 1);
    }
}

/* Pantalla 4 (F4): DIAGRAMAS V(x) Y M(x) CON CURSOR */
static void render_tab_vm(void) {
    double L = beam_L > 0.01 ? beam_L : 1.0;
    int N = 120;
    double V_arr[121], M_arr[121];
    double vmax = 0.0, vmin = 0.0, mmax = 0.0, mmin = 0.0;

    for (int i = 0; i <= N; i++) {
        double x = (L * i) / N;
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

    int x0 = 16, w = 244;

    /* Grafico Cortante V(x) */
    fill_rect(8, 24, 260, 78, C_WHITE);
    draw_rect(8, 24, 260, 78, C_BORDER);
    int yv0 = 63;
    draw_line(x0, yv0, x0 + w, yv0, C_GRAY);
    draw_text_bold(12, 26, "V(x)", C_BLUE, C_WHITE, 1);

    for (int i = 0; i < N; i++) {
        int px1 = x0 + (i * w) / N;
        int px2 = x0 + ((i + 1) * w) / N;
        int py1 = yv0 - (int)my_round((V_arr[i] / vabs) * 30.0);
        int py2 = yv0 - (int)my_round((V_arr[i + 1] / vabs) * 30.0);
        draw_line(px1, yv0, px1, py1, C_LBLUE);
        draw_line(px1, py1, px2, py2, C_BLUE);
    }

    /* Grafico Momento M(x) */
    fill_rect(8, 105, 260, 82, C_WHITE);
    draw_rect(8, 105, 260, 82, C_BORDER);
    int ym0 = 146;
    draw_line(x0, ym0, x0 + w, ym0, C_GRAY);
    draw_text_bold(12, 107, "M(x)", C_RED, C_WHITE, 1);

    for (int i = 0; i < N; i++) {
        int px1 = x0 + (i * w) / N;
        int px2 = x0 + ((i + 1) * w) / N;
        int py1 = ym0 - (int)my_round((M_arr[i] / mabs) * 32.0);
        int py2 = ym0 - (int)my_round((M_arr[i + 1] / mabs) * 32.0);
        draw_line(px1, ym0, px1, py1, C_LRED);
        draw_line(px1, py1, px2, py2, C_RED);
    }

    /* Linea de cursor interactivo */
    int cx = x0 + (int)my_round((diag_cursor_x / L) * w);
    draw_line(cx, 26, cx, 185, C_GREEN);

    /* Panel derecho con valores */
    fill_rect(272, 24, 104, 163, C_WHITE);
    draw_rect(272, 24, 104, 163, C_BORDER);

    double vc, mc;
    eval_VM(diag_cursor_x, &vc, &mc);
    char buf[24], num[14];

    draw_text_bold(277, 28, "CURSOR [</>]", C_GREEN, C_WHITE, 1);
    fmt_num(num, diag_cursor_x, 2);
    strcpy(buf, "x="); str_cat(buf, num); str_cat(buf, "m");
    draw_text(277, 43, buf, C_BLACK, C_WHITE, 1);

    fmt_num(num, vc, 2);
    strcpy(buf, "V="); str_cat(buf, num);
    draw_text(277, 57, buf, C_BLUE, C_WHITE, 1);

    fmt_num(num, mc, 2);
    strcpy(buf, "M="); str_cat(buf, num);
    draw_text(277, 71, buf, C_RED, C_WHITE, 1);

    draw_line(276, 88, 372, 88, C_BORDER);
    draw_text_bold(277, 92, "EXTREMOS", C_NAVY, C_WHITE, 1);

    fmt_num(num, vmax, 1);
    strcpy(buf, "V+:"); str_cat(buf, num);
    draw_text(277, 107, buf, C_BLUE, C_WHITE, 1);

    fmt_num(num, vmin, 1);
    strcpy(buf, "V-:"); str_cat(buf, num);
    draw_text(277, 121, buf, C_BLUE, C_WHITE, 1);

    fmt_num(num, mmax, 1);
    strcpy(buf, "M+:"); str_cat(buf, num);
    draw_text(277, 137, buf, C_RED, C_WHITE, 1);

    fmt_num(num, mmin, 1);
    strcpy(buf, "M-:"); str_cat(buf, num);
    draw_text(277, 151, buf, C_RED, C_WHITE, 1);
}

/* Pantalla 5 (F5): ECUACIONES POR TRAMOS */
static void render_tab_ecuac(void) {
    double pts[40];
    int np = 0;
    pts[np++] = 0.0;
    pts[np++] = beam_L;
    if (has_supA) pts[np++] = res_xA;
    if (has_supB) pts[np++] = res_xB;
    for (int i = 0; i < num_loads; i++) {
        pts[np++] = loads[i].pos1;
        if (loads[i].type == LOAD_DIST) pts[np++] = loads[i].pos2;
    }
    /* Ordenar y eliminar duplicados */
    for (int i = 0; i < np - 1; i++) {
        for (int j = i + 1; j < np; j++) {
            if (pts[j] < pts[i]) {
                double t = pts[i]; pts[i] = pts[j]; pts[j] = t;
            }
        }
    }
    double uniq[40];
    int nu = 0;
    for (int i = 0; i < np; i++) {
        if (nu == 0 || pts[i] - uniq[nu - 1] > 1e-4) uniq[nu++] = pts[i];
    }

    int n_tramos = nu - 1;
    if (n_tramos < 1) n_tramos = 1;
    if (scroll_ecuac >= n_tramos) scroll_ecuac = n_tramos - 1;
    if (scroll_ecuac < 0) scroll_ecuac = 0;

    int y = 25;
    for (int t = scroll_ecuac; t < scroll_ecuac + 3 && t < n_tramos; t++) {
        double xa = uniq[t], xb = uniq[t + 1];
        double xm = 0.5 * (xa + xb);
        double d = xb - xa;

        /* Obtener coeficientes exactos de V(x) = c0 + c1*(x-xa) + c2*(x-xa)^2 */
        double v0, m0, v1, m1, v2, m2;
        eval_VM(xa + 1e-5 * d, &v0, &m0);
        eval_VM(xm, &v1, &m1);
        eval_VM(xb - 1e-5 * d, &v2, &m2);

        double slope_V = (v2 - v0) / d;

        fill_rect(8, y, SCREEN_W - 16, 48, C_WHITE);
        draw_rect(8, y, SCREEN_W - 16, 48, C_BORDER);

        char hdr[48], n1[12], n2[12];
        fmt_num(n1, (double)(t + 1), 0);
        fmt_num(n2, xa, 2);
        strcpy(hdr, "TRAMO "); str_cat(hdr, n1);
        str_cat(hdr, ":  "); str_cat(hdr, n2);
        fmt_num(n2, xb, 2);
        str_cat(hdr, " <= x <= "); str_cat(hdr, n2); str_cat(hdr, " m");
        draw_text_bold(14, y + 3, hdr, C_NAVY, C_WHITE, 1);

        char eqV[56], eqM[56];
        fmt_num(n1, v0, 2);
        strcpy(eqV, "V(x) = "); str_cat(eqV, n1);
        if (my_fabs(slope_V) > 1e-3) {
            fmt_num(n2, slope_V, 2);
            if (slope_V >= 0) str_cat(eqV, " + ");
            else str_cat(eqV, " ");
            str_cat(eqV, n2);
            str_cat(eqV, "(x-");
            fmt_num(n1, xa, 2); str_cat(eqV, n1); str_cat(eqV, ")");
        }
        draw_text(14, y + 18, eqV, C_BLUE, C_WHITE, 1);

        fmt_num(n1, m0, 2);
        strcpy(eqM, "M(x) = "); str_cat(eqM, n1);
        if (my_fabs(v0) > 1e-3) {
            fmt_num(n2, v0, 2);
            if (v0 >= 0) str_cat(eqM, " + ");
            else str_cat(eqM, " ");
            str_cat(eqM, n2);
            str_cat(eqM, "(x-");
            fmt_num(n1, xa, 2); str_cat(eqM, n1); str_cat(eqM, ")");
        }
        if (my_fabs(slope_V) > 1e-3) {
            fmt_num(n2, 0.5 * slope_V, 2);
            if (slope_V >= 0) str_cat(eqM, " + ");
            else str_cat(eqM, " ");
            str_cat(eqM, n2);
            str_cat(eqM, "(x-");
            fmt_num(n1, xa, 2); str_cat(eqM, n1); str_cat(eqM, ")^2");
        }
        draw_text(14, y + 32, eqM, C_RED, C_WHITE, 1);

        y += 51;
    }
    draw_text(10, 178, "[▲/▼] Desplazar tramos", C_SLATE, C_BG, 1);
}

/* Pantalla 6 (F6): NUEVO / EJEMPLO */
static void render_tab_nuevo(void) {
    fill_rect(24, 42, SCREEN_W - 48, 128, C_WHITE);
    draw_rect(24, 42, SCREEN_W - 48, 128, C_NAVY);
    draw_text_bold(38, 52, "OPCIONES DE PROYECTO", C_NAVY, C_WHITE, 1);
    draw_text(38, 76, "Presiona [1]: Limpiar todo (viga nueva)", C_BLACK, C_WHITE, 1);
    draw_text(38, 98, "Presiona [2]: Cargar viga de ejemplo", C_BLUE, C_WHITE, 1);
    draw_text(38, 120, "Presiona [F1]: Volver a Ingreso de Datos", C_SLATE, C_WHITE, 1);
}

int main(void) {
    EnableStatusArea(3);
    solve_beam();

    while (1) {
        draw_chrome();
        if (active_tab == 0) render_tab_datos();
        else if (active_tab == 1) render_tab_carga();
        else if (active_tab == 2) render_tab_reacc();
        else if (active_tab == 3) render_tab_vm();
        else if (active_tab == 4) render_tab_ecuac();
        else if (active_tab == 5) render_tab_nuevo();

        Bdisp_PutDisp_DD();

        int key = 0;
        GetKey(&key);

        /* Atajos globales del menu inferior F1 - F6 */
        if (key == KEY_CTRL_F1) { active_tab = 0; continue; }
        if (key == KEY_CTRL_F2) { active_tab = 1; continue; }
        if (key == KEY_CTRL_F3) { active_tab = 2; solve_beam(); continue; }
        if (key == KEY_CTRL_F4) { active_tab = 3; solve_beam(); continue; }
        if (key == KEY_CTRL_F5) { active_tab = 4; solve_beam(); continue; }
        if (key == KEY_CTRL_F6) { active_tab = 5; continue; }

        /* Controles por pestana */
        if (active_tab == 0) {
            int max_items = (beam_type == BEAM_OVERHANG) ? 5 : 3;
            char typed = 0;
            if (key >= KEY_CHAR_0 && key <= KEY_CHAR_9) typed = (char)('0' + (key - KEY_CHAR_0));
            else if (key == KEY_CHAR_DP || key == KEY_CHAR_COMMA) typed = '.';

            if (key == KEY_CTRL_UP) {
                cursor_datos = (cursor_datos + max_items - 1) % max_items;
            } else if (key == KEY_CTRL_DOWN) {
                cursor_datos = (cursor_datos + 1) % max_items;
            } else if (cursor_datos == 1 && (key == KEY_CTRL_LEFT || key == KEY_CTRL_RIGHT || key == KEY_CTRL_EXE)) {
                if (key == KEY_CTRL_LEFT) beam_type = (beam_type + 5) % 6;
                else beam_type = (beam_type + 1) % 6;
                if (beam_type == BEAM_OVERHANG) {
                    sup_xA = 0.0;
                    sup_xB = beam_L;
                }
                solve_beam();
            } else if (key == KEY_CTRL_EXE || typed != 0) {
                if (cursor_datos == 0) {
                    if (edit_number("LONGITUD DE LA VIGA (L)", "m", &beam_L, 0, typed)) {
                        if (beam_L < 0.1) beam_L = 0.1;
                        if (sup_xB > beam_L) sup_xB = beam_L;
                        solve_beam();
                    }
                } else if (beam_type == BEAM_OVERHANG && cursor_datos == 2) {
                    if (edit_number("POSICION APOYO A (xA)", "m", &sup_xA, 0, typed)) {
                        if (sup_xA > beam_L) sup_xA = beam_L;
                        solve_beam();
                    }
                } else if (beam_type == BEAM_OVERHANG && cursor_datos == 3) {
                    if (edit_number("POSICION APOYO B (xB)", "m", &sup_xB, 0, typed)) {
                        if (sup_xB > beam_L) sup_xB = beam_L;
                        solve_beam();
                    }
                } else if (cursor_datos == max_items - 1) {
                    active_tab = 1;
                }
            }
        } else if (active_tab == 1) {
            int total_rows = 3 + num_loads;
            if (key == KEY_CTRL_UP) {
                cursor_carga = (cursor_carga + total_rows - 1) % total_rows;
            } else if (key == KEY_CTRL_DOWN) {
                cursor_carga = (cursor_carga + 1) % total_rows;
            } else if (key == KEY_CTRL_EXE) {
                if (cursor_carga == 0) add_or_edit_load(LOAD_POINT, -1);
                else if (cursor_carga == 1) add_or_edit_load(LOAD_DIST, -1);
                else if (cursor_carga == 2) add_or_edit_load(LOAD_MOMENT, -1);
                else add_or_edit_load(0, cursor_carga - 3);
            } else if (key == KEY_CTRL_DEL && cursor_carga >= 3) {
                int idx = cursor_carga - 3;
                for (int i = idx; i < num_loads - 1; i++) loads[i] = loads[i + 1];
                num_loads--;
                solve_beam();
            }
        } else if (active_tab == 3) {
            double step = beam_L / 40.0;
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
        } else if (active_tab == 5) {
            if (key == KEY_CHAR_1) {
                beam_L = 6.0;
                beam_type = BEAM_SIMPLE;
                num_loads = 0;
                cursor_datos = 0;
                active_tab = 0;
                solve_beam();
            } else if (key == KEY_CHAR_2) {
                beam_L = 8.0;
                beam_type = BEAM_SIMPLE;
                num_loads = 2;
                loads[0].type = LOAD_DIST; loads[0].val1 = 12.0; loads[0].val2 = 12.0; loads[0].pos1 = 0.0; loads[0].pos2 = 5.0;
                loads[1].type = LOAD_POINT; loads[1].val1 = 25.0; loads[1].val2 = 0.0; loads[1].pos1 = 6.5; loads[1].pos2 = 0.0;
                diag_cursor_x = 4.0;
                active_tab = 0;
                solve_beam();
            }
        }
    }
    return 0;
}
