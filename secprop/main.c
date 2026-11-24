/* ============================================================
   SECPROP  -  Propiedades geometricas de secciones de acero
   Casio fx-CG50 / fx-CG20 (add-in G3A, libfxcg)
   Vigas y columnas:  A, Ix, Iy, r, S, Z, ENP, J, Cw
   Origen de coordenadas: esquina inferior izquierda
   x -> derecha   y -> arriba
   ============================================================ */
#include <fxcg/display.h>
#include <fxcg/keyboard.h>
#include <string.h>

extern void EnableStatusArea(int opt);

#define SCREEN_W 384
#define SCREEN_H 216

/* ---------------- Colores RGB565 ---------------- */
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
#define C_GREEN   0x0480
#define C_DGREEN  0x0320
#define C_ORANGE  0xEC00

#define PI 3.14159265358979324

/* ============================================================
   1. Matematica propia (sin libm)
   ============================================================ */
static double my_fabs(double x) { return x < 0.0 ? -x : x; }

static double my_sqrt(double x) {
    if (x <= 0.0) return 0.0;
    double r = x > 1.0 ? x : 1.0;
    for (int i = 0; i < 40; i++) r = 0.5 * (r + x / r);
    return r;
}

/* atan por aproximacion polinomica con reduccion de argumento */
static double atan_core(double t) {
    double t2 = t * t;
    return t * (0.99997726 + t2 * (-0.33262347 + t2 * (0.19354346 +
               t2 * (-0.11643287 + t2 * (0.05265332 + t2 * (-0.01172120))))));
}

static double my_atan(double t) {
    int neg = 0;
    if (t < 0.0) { t = -t; neg = 1; }
    double r;
    if (t > 1.0) r = PI / 2.0 - atan_core(1.0 / t);
    else r = atan_core(t);
    return neg ? -r : r;
}

static double my_atan2(double y, double x) {
    if (x > 0.0) return my_atan(y / x);
    if (x < 0.0) {
        if (y >= 0.0) return my_atan(y / x) + PI;
        return my_atan(y / x) - PI;
    }
    if (y > 0.0) return PI / 2.0;
    if (y < 0.0) return -PI / 2.0;
    return 0.0;
}

static double my_asin(double t) {
    if (t >= 1.0) return PI / 2.0;
    if (t <= -1.0) return -PI / 2.0;
    return my_atan2(t, my_sqrt(1.0 - t * t));
}

/* ============================================================
   2. Texto y numeros
   ============================================================ */
static void str_cat(char *dst, const char *src) {
    int i = 0, j = 0;
    while (dst[i]) i++;
    while (src[j]) dst[i++] = src[j++];
    dst[i] = '\0';
}

static void fmt_num(char *buf, double val, int dec) {
    int idx = 0;
    double mult = 1.0;
    for (int i = 0; i < dec; i++) mult *= 10.0;
    double threshold = 0.5 / mult;
    if (my_fabs(val) < threshold) val = 0.0;
    if (val < 0.0) { buf[idx++] = '-'; val = -val; }
    long long scaled = (long long)(val * mult + 0.5);
    long long imult = (long long)(mult + 0.5);
    long long ipart = (dec > 0) ? (scaled / imult) : scaled;
    long long fpart = (dec > 0) ? (scaled % imult) : 0;
    char tmp[24];
    int tlen = 0;
    if (ipart == 0) tmp[tlen++] = '0';
    else while (ipart > 0 && tlen < 20) { tmp[tlen++] = (char)('0' + (ipart % 10)); ipart /= 10; }
    for (int i = tlen - 1; i >= 0; i--) buf[idx++] = tmp[i];
    if (dec > 0) {
        buf[idx++] = '.';
        for (int i = dec - 1; i >= 0; i--) { buf[idx + i] = (char)('0' + (fpart % 10)); fpart /= 10; }
        idx += dec;
    }
    buf[idx] = '\0';
}

/* formato con ~5 cifras significativas, cientifico si hace falta */
static void fmt_sig(char *buf, double v) {
    double a = my_fabs(v);
    buf[0] = '\0';
    if (a < 1e-9) { strcpy(buf, "0"); return; }
    if (a >= 1e9 || a < 1e-4) {
        int ex = 0;
        double m = a;
        while (m >= 10.0) { m /= 10.0; ex++; }
        while (m < 1.0) { m *= 10.0; ex--; }
        char mb[16], eb[8];
        fmt_num(mb, m, 3);
        if (v < 0.0) strcpy(buf, "-");
        str_cat(buf, mb);
        str_cat(buf, "e");
        if (ex < 0) { str_cat(buf, "-"); ex = -ex; } else str_cat(buf, "+");
        fmt_num(eb, (double)ex, 0);
        str_cat(buf, eb);
        return;
    }
    int dec = 3;
    if (a < 10.0) dec = 4;
    if (a >= 1000.0) dec = 1;
    if (a >= 100000.0) dec = 0;
    fmt_num(buf, v, dec);
}

/* formato compacto de respaldo: como maximo 8 caracteres */
static void fmt_short(char *buf, double v) {
    double a = my_fabs(v);
    buf[0] = '\0';
    if (a < 1e-9) { strcpy(buf, "0"); return; }
    int ex = 0;
    double m = a;
    while (m >= 10.0) { m /= 10.0; ex++; }
    while (m < 1.0) { m *= 10.0; ex--; }
    m = (double)(long long)(m * 100.0 + 0.5) / 100.0;
    if (m >= 10.0) { m /= 10.0; ex++; }
    char mb[12], eb[6];
    fmt_num(mb, m, 2);
    if (v < 0.0) strcpy(buf, "-");
    str_cat(buf, mb);
    str_cat(buf, "e");
    if (ex < 0) { str_cat(buf, "-"); ex = -ex; } else str_cat(buf, "+");
    fmt_num(eb, (double)ex, 0);
    str_cat(buf, eb);
}

static int parse_num(const char *s, double *out) {
    int i = 0, sign = 1, has = 0;
    double iv = 0.0, fv = 0.0, fd = 1.0;
    while (s[i] == ' ') i++;
    if (s[i] == '-') { sign = -1; i++; }
    else if (s[i] == '+') i++;
    while (s[i] >= '0' && s[i] <= '9') { has = 1; iv = iv * 10.0 + (s[i] - '0'); i++; }
    if (s[i] == '.' || s[i] == ',') {
        i++;
        while (s[i] >= '0' && s[i] <= '9') { has = 1; fd *= 10.0; fv += (s[i] - '0') / fd; i++; }
    }
    if (!has) return 0;
    *out = sign * (iv + fv);
    return 1;
}

/* ============================================================
   3. Primitivas graficas
   ============================================================ */
static unsigned short *vram_ptr(void) { return (unsigned short *)GetVRAMAddress(); }

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

static void put_px(int x, int y, unsigned short c) {
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;
    vram_ptr()[y * SCREEN_W + x] = c;
}

static unsigned short get_px(int x, int y) {
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return C_BG;
    return vram_ptr()[y * SCREEN_W + x];
}

static void dash_h(int xa, int xb, int y, unsigned short c) {
    for (int x = xa; x <= xb; x += 5) { put_px(x, y, c); put_px(x + 1, y, c); }
}

static void dash_v(int x, int ya, int yb, unsigned short c) {
    for (int y = ya; y <= yb; y += 5) { put_px(x, y, c); put_px(x, y + 1, c); }
}

static void draw_text(int x, int y, const char *txt, unsigned short fg, unsigned short bg, int transp) {
    int px = x, py = y;
    PrintMini(&px, &py, txt, transp ? 0x02 : 0x00, 0xFFFFFFFF, 0, 0, fg, bg, 1, 0);
}

static void draw_text_bold(int x, int y, const char *txt, unsigned short fg, unsigned short bg, int transp) {
    draw_text(x, y, txt, fg, bg, transp);
    draw_text(x + 1, y, txt, fg, bg, 1);
}

/* ---- metricas reales de la fuente, medidas al arrancar ---- */
static int FW = 10;   /* ancho de caracter  */
static int FH = 16;   /* alto de caracter   */
static int TOPH = 22, TABY = 192, CY0 = 26, CY1 = 188, HDR = 19, ROW = 19;

static int tw_px(const char *s) { return FW * (int)strlen(s); }

static void calibrate(void) {
    fill_rect(0, 0, SCREEN_W, 60, C_WHITE);
    int x = 0, y = 0;
    PrintMini(&x, &y, "MMMMMMMMMM", 0x02, 0xFFFFFFFF, 0, 0, C_BLACK, C_WHITE, 1, 0);
    if (x >= 30 && x <= 200) FW = x / 10;
    int bot = -1;
    for (int py = 0; py < 60; py++) {
        for (int px = 0; px < SCREEN_W; px++)
            if (get_px(px, py) != C_WHITE) { bot = py; break; }
    }
    if (bot >= 6 && bot <= 40) FH = bot + 1;
    fill_rect(0, 0, SCREEN_W, 60, C_WHITE);
    TOPH = FH + 6;
    TABY = SCREEN_H - (FH + 8);
    CY0 = TOPH + 4;
    CY1 = TABY - 4;
    HDR = FH + 3;
    ROW = FH + 3;
    int avail = CY1 - CY0 - HDR - 8;
    if (ROW * 7 > avail && avail > 42) ROW = avail / 7;
}

/* tarjeta con titulo */
static void card(int x, int y, int w, int h, const char *title) {
    fill_rect(x, y, w, h, C_WHITE);
    draw_rect(x, y, w, h, C_BORDER);
    fill_rect(x, y, w, HDR, C_SLATE);
    draw_text(x + 5, y + 1, title, C_WHITE, C_SLATE, 1);
}

/* ============================================================
   4. Definicion de secciones
   ============================================================ */
enum {
    S_ISIM = 0, S_IASIM, S_CANAL, S_TE, S_ANG, S_2ANG,
    S_ZETA, S_GLAB, S_TUBR, S_TUBC, S_PLACA, S_BARRA, S_NTIPOS
};

typedef struct {
    const char *name;
    const char *shortn;
    int ndim;
    int fil_ok;
    const char *lbl[6];
    const char *dsc[6];
    double def[6];
    double rdef;
} SecDef;

static const SecDef SEC[S_NTIPOS] = {
 { "I simetrica", "I/H", 4, 1,
   {"h","b","tf","tw"},
   {"altura total","ancho del ala","espesor ala","espesor alma"},
   {300.0, 150.0, 10.7, 7.1}, 15.0 },
 { "I asimetrica", "I asim", 6, 0,
   {"d","bs","ts","bi","ti","tw"},
   {"altura total","ancho ala sup.","espesor ala sup","ancho ala inf.","espesor ala inf","espesor alma"},
   {400.0, 200.0, 12.0, 250.0, 15.0, 8.0}, 0.0 },
 { "Canal C / U", "C/U", 4, 1,
   {"d","bf","tf","tw"},
   {"altura total","ancho del ala","espesor ala","espesor alma"},
   {200.0, 75.0, 11.5, 6.0}, 10.0 },
 { "Perfil T", "T", 4, 1,
   {"d","bf","tf","tw"},
   {"altura total","ancho del ala","espesor ala","espesor alma"},
   {150.0, 200.0, 14.0, 9.0}, 10.0 },
 { "Angulo L", "L", 3, 1,
   {"h","b","t"},
   {"ala vertical","ala horizontal","espesor"},
   {100.0, 100.0, 10.0}, 12.0 },
 { "Doble angulo 2L", "2L", 4, 1,
   {"h","b","t","s"},
   {"ala vertical","ala horizontal","espesor","separacion"},
   {100.0, 100.0, 10.0, 10.0}, 12.0 },
 { "Perfil Z", "Z", 4, 1,
   {"d","bf","tf","tw"},
   {"altura total","ancho del ala","espesor ala","espesor alma"},
   {200.0, 75.0, 8.0, 6.0}, 8.0 },
 { "G con labios", "G", 4, 0,
   {"H","B","C","t"},
   {"altura total","ancho del ala","largo labio","espesor"},
   {200.0, 75.0, 20.0, 2.5}, 0.0 },
 { "Tubo rectang.", "TubR", 3, 0,
   {"B","H","t"},
   {"ancho exterior","altura exterior","espesor pared"},
   {150.0, 100.0, 6.0}, 0.0 },
 { "Tubo circular", "TubC", 2, 0,
   {"D","t"},
   {"diametro ext.","espesor pared"},
   {168.3, 5.0}, 0.0 },
 { "Placa rect.", "Placa", 2, 0,
   {"b","h"},
   {"ancho","altura"},
   {300.0, 20.0}, 0.0 },
 { "Barra circular", "Barra", 1, 0,
   {"D"},
   {"diametro"},
   {50.0}, 0.0 }
};

static const char *UN[3] = { "mm", "cm", "in" };

/* ------------- Estado ------------- */
static int sec_type = S_ISIM;
static int use_fil = 1;
static int uidx = 0;
static double dim[7];

static int active_tab = 0;
static int cur_tipo = 0;
static int cur_dato = 0;

/* ============================================================
   5. Elementos geometricos
   ============================================================ */
#define MAXE 24
typedef struct {
    int kind;                 /* 0 = rectangulo, 1 = filete */
    double x1, y1, x2, y2;    /* rectangulo */
    double r;                 /* radio del filete */
    int sx, sy;               /* direccion del filete */
    int sign;                 /* +1 solido, -1 hueco */
} Elem;

static Elem EL[MAXE];
static int NE = 0;
static int is_circ = 0;
static double cDe = 0.0, cDi = 0.0;
static double jv = 0.0, h0 = 0.0;
static int jv_ok = 0;

static void e_rect(double x1, double y1, double x2, double y2, int sign) {
    if (NE >= MAXE) return;
    EL[NE].kind = 0; EL[NE].x1 = x1; EL[NE].y1 = y1;
    EL[NE].x2 = x2; EL[NE].y2 = y2; EL[NE].sign = sign;
    EL[NE].r = 0.0; EL[NE].sx = 0; EL[NE].sy = 0;
    NE++;
}

static void e_fil(double x, double y, double r, int sx, int sy) {
    if (NE >= MAXE || r <= 0.0) return;
    EL[NE].kind = 1; EL[NE].x1 = x; EL[NE].y1 = y;
    EL[NE].x2 = 0.0; EL[NE].y2 = 0.0; EL[NE].r = r;
    EL[NE].sx = sx; EL[NE].sy = sy; EL[NE].sign = 1;
    NE++;
}

static void j_plate(double b, double t) {
    if (b > 0.0 && t > 0.0) jv += b * t * t * t / 3.0;
}

static double clampd(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ajusta las dimensiones a valores geometricamente posibles */
static void sanitize(void) {
    const SecDef *s = &SEC[sec_type];
    for (int i = 0; i < s->ndim; i++) if (dim[i] < 0.0) dim[i] = -dim[i];
    for (int i = 0; i < s->ndim; i++) if (dim[i] < 0.01 && !(sec_type == S_2ANG && i == 3)) dim[i] = 0.01;
    double rmax = 0.0;
    if (sec_type == S_ISIM) {
        dim[2] = clampd(dim[2], 0.01, dim[0] / 2.0);
        dim[3] = clampd(dim[3], 0.01, dim[1]);
        rmax = (dim[1] - dim[3]) / 2.0;
        if (dim[0] / 2.0 - dim[2] < rmax) rmax = dim[0] / 2.0 - dim[2];
    } else if (sec_type == S_IASIM) {
        dim[2] = clampd(dim[2], 0.01, dim[0] / 2.0);
        dim[4] = clampd(dim[4], 0.01, dim[0] - dim[2]);
        double bmin = dim[1] < dim[3] ? dim[1] : dim[3];
        dim[5] = clampd(dim[5], 0.01, bmin);
    } else if (sec_type == S_CANAL || sec_type == S_ZETA) {
        dim[2] = clampd(dim[2], 0.01, dim[0] / 2.0);
        dim[3] = clampd(dim[3], 0.01, dim[1]);
        rmax = dim[1] - dim[3];
        double v2 = (sec_type == S_CANAL) ? (dim[0] / 2.0 - dim[2]) : (dim[0] - 2.0 * dim[2]);
        if (v2 < rmax) rmax = v2;
    } else if (sec_type == S_TE) {
        dim[2] = clampd(dim[2], 0.01, dim[0]);
        dim[3] = clampd(dim[3], 0.01, dim[1]);
        rmax = (dim[1] - dim[3]) / 2.0;
        if (dim[0] - dim[2] < rmax) rmax = dim[0] - dim[2];
    } else if (sec_type == S_ANG || sec_type == S_2ANG) {
        double mn = dim[0] < dim[1] ? dim[0] : dim[1];
        dim[2] = clampd(dim[2], 0.01, mn);
        rmax = dim[0] - dim[2];
        if (dim[1] - dim[2] < rmax) rmax = dim[1] - dim[2];
        if (sec_type == S_2ANG && dim[3] < 0.0) dim[3] = 0.0;
    } else if (sec_type == S_GLAB) {
        dim[2] = clampd(dim[2], 0.01, dim[0] / 2.0);
        double mn = dim[1] < dim[0] / 2.0 ? dim[1] : dim[0] / 2.0;
        dim[3] = clampd(dim[3], 0.01, mn);
    } else if (sec_type == S_TUBR) {
        double mn = dim[0] < dim[1] ? dim[0] : dim[1];
        dim[2] = clampd(dim[2], 0.01, mn / 2.0);
    } else if (sec_type == S_TUBC) {
        dim[1] = clampd(dim[1], 0.01, dim[0] / 2.0);
    }
    if (rmax < 0.0) rmax = 0.0;
    if (SEC[sec_type].fil_ok) dim[SEC[sec_type].ndim] = clampd(dim[SEC[sec_type].ndim], 0.0, rmax);
}

static void load_defaults(void) {
    const SecDef *s = &SEC[sec_type];
    for (int i = 0; i < 6; i++) dim[i] = s->def[i];
    dim[s->ndim] = s->rdef;
    if (!s->fil_ok) use_fil = 0;
    sanitize();
}

static void build_section(void) {
    const SecDef *s = &SEC[sec_type];
    NE = 0; is_circ = 0; jv = 0.0; h0 = 0.0; jv_ok = 1;
    sanitize();
    double r = (s->fil_ok && use_fil) ? dim[s->ndim] : 0.0;

    if (sec_type == S_ISIM) {
        double h = dim[0], b = dim[1], tf = dim[2], tw = dim[3];
        e_rect(0, 0, b, tf, 1);
        e_rect((b - tw) / 2, tf, (b + tw) / 2, h - tf, 1);
        e_rect(0, h - tf, b, h, 1);
        if (r > 0.0) {
            double xl = (b - tw) / 2, xr = (b + tw) / 2;
            e_fil(xl, tf, r, -1, 1);  e_fil(xr, tf, r, 1, 1);
            e_fil(xl, h - tf, r, -1, -1); e_fil(xr, h - tf, r, 1, -1);
        }
        j_plate(b, tf); j_plate(b, tf); j_plate(h - 2 * tf, tw);
        h0 = h - tf;
    } else if (sec_type == S_IASIM) {
        double d = dim[0], bs = dim[1], ts = dim[2], bi = dim[3], ti = dim[4], tw = dim[5];
        double W = bs > bi ? bs : bi;
        e_rect((W - bi) / 2, 0, (W + bi) / 2, ti, 1);
        e_rect((W - tw) / 2, ti, (W + tw) / 2, d - ts, 1);
        e_rect((W - bs) / 2, d - ts, (W + bs) / 2, d, 1);
        j_plate(bs, ts); j_plate(bi, ti); j_plate(d - ts - ti, tw);
    } else if (sec_type == S_CANAL) {
        double d = dim[0], bf = dim[1], tf = dim[2], tw = dim[3];
        e_rect(0, 0, tw, d, 1);
        e_rect(tw, 0, bf, tf, 1);
        e_rect(tw, d - tf, bf, d, 1);
        if (r > 0.0) { e_fil(tw, tf, r, 1, 1); e_fil(tw, d - tf, r, 1, -1); }
        j_plate(bf - tw, tf); j_plate(bf - tw, tf); j_plate(d, tw);
    } else if (sec_type == S_TE) {
        double d = dim[0], bf = dim[1], tf = dim[2], tw = dim[3];
        e_rect(0, d - tf, bf, d, 1);
        e_rect((bf - tw) / 2, 0, (bf + tw) / 2, d - tf, 1);
        if (r > 0.0) {
            e_fil((bf - tw) / 2, d - tf, r, -1, -1);
            e_fil((bf + tw) / 2, d - tf, r, 1, -1);
        }
        j_plate(bf, tf); j_plate(d - tf, tw);
    } else if (sec_type == S_ANG) {
        double h = dim[0], b = dim[1], t = dim[2];
        e_rect(0, 0, t, h, 1);
        e_rect(t, 0, b, t, 1);
        if (r > 0.0) e_fil(t, t, r, 1, 1);
        j_plate(h, t); j_plate(b - t, t);
    } else if (sec_type == S_2ANG) {
        double h = dim[0], b = dim[1], t = dim[2], g = dim[3];
        e_rect(0, 0, b - t, t, 1);
        e_rect(b - t, 0, b, h, 1);
        e_rect(b + g, 0, b + g + t, h, 1);
        e_rect(b + g + t, 0, 2 * b + g, t, 1);
        if (r > 0.0) { e_fil(b - t, t, r, -1, 1); e_fil(b + g + t, t, r, 1, 1); }
        j_plate(h, t); j_plate(b - t, t); j_plate(h, t); j_plate(b - t, t);
    } else if (sec_type == S_ZETA) {
        double d = dim[0], bf = dim[1], tf = dim[2], tw = dim[3];
        e_rect(0, 0, bf - tw, tf, 1);
        e_rect(bf - tw, 0, bf, d, 1);
        e_rect(bf, d - tf, 2 * bf - tw, d, 1);
        if (r > 0.0) { e_fil(bf - tw, tf, r, -1, 1); e_fil(bf, d - tf, r, 1, -1); }
        j_plate(bf - tw, tf); j_plate(bf - tw, tf); j_plate(d, tw);
    } else if (sec_type == S_GLAB) {
        double H = dim[0], B = dim[1], C = dim[2], t = dim[3];
        e_rect(0, 0, t, H, 1);
        e_rect(t, 0, B, t, 1);
        e_rect(t, H - t, B, H, 1);
        e_rect(B - t, t, B, C, 1);
        e_rect(B - t, H - C, B, H - t, 1);
        j_plate(H, t); j_plate(B, t); j_plate(B, t); j_plate(C, t); j_plate(C, t);
    } else if (sec_type == S_TUBR) {
        double B = dim[0], H = dim[1], t = dim[2];
        e_rect(0, 0, B, H, 1);
        e_rect(t, t, B - t, H - t, -1);
        double bm = B - t, hm = H - t;
        jv = 2.0 * t * bm * bm * hm * hm / (bm + hm);
    } else if (sec_type == S_TUBC) {
        is_circ = 1; cDe = dim[0]; cDi = dim[0] - 2.0 * dim[1];
        jv = 0.0; jv_ok = 0;   /* J = 2*Ix, se calcula luego */
    } else if (sec_type == S_PLACA) {
        double b = dim[0], h = dim[1];
        e_rect(0, 0, b, h, 1);
        double L = b > h ? b : h, e = b > h ? h : b;
        jv = L * e * e * e * (1.0 / 3.0 - 0.21 * (e / L) * (1.0 - e * e * e * e / (12.0 * L * L * L * L)));
    } else { /* barra circular */
        is_circ = 1; cDe = dim[0]; cDi = 0.0;
        jv = 0.0; jv_ok = 0;
    }
}

/* ============================================================
   6. Propiedades de la seccion
   ============================================================ */
static double ww_fil(double r, double u) {
    double q = r - u;
    double z = r * r - q * q;
    return r - my_sqrt(z < 0.0 ? 0.0 : z);
}

/* area del filete contenida hasta la profundidad T */
static double Wf(double r, double T) {
    double q = r - T;
    double z = r * r - q * q;
    if (z < 0.0) z = 0.0;
    double t = q / r;
    if (t > 1.0) t = 1.0;
    if (t < -1.0) t = -1.0;
    return r * T - PI * r * r / 4.0 + (q * my_sqrt(z) + r * r * my_asin(t)) / 2.0;
}

typedef struct { double a, cx, cy, ix, iy, ixy; } EP;

static EP ep_of(const Elem *e) {
    EP p;
    if (e->kind == 1) {
        double r = e->r;
        double a = r * r * (1.0 - PI / 4.0);
        double c = r * (10.0 - 3.0 * PI) / (12.0 - 3.0 * PI);
        double r4 = r * r * r * r;
        double i = r4 * (1.0 - 5.0 * PI / 16.0) - a * c * c;
        p.a = a;
        p.cx = e->x1 + e->sx * c;
        p.cy = e->y1 + e->sy * c;
        p.ix = i; p.iy = i;
        p.ixy = e->sx * e->sy * (r4 * (19.0 / 24.0 - PI / 4.0) - a * c * c);
        return p;
    }
    double b = e->x2 - e->x1, h = e->y2 - e->y1;
    p.a = e->sign * b * h;
    p.cx = (e->x1 + e->x2) / 2.0;
    p.cy = (e->y1 + e->y2) / 2.0;
    p.ix = e->sign * b * h * h * h / 12.0;
    p.iy = e->sign * h * b * b * b / 12.0;
    p.ixy = 0.0;
    return p;
}

/* area del elemento con coordenada k (0=x, 1=y) menor que p */
static double below(const Elem *e, int k, double pos) {
    if (e->kind == 1) {
        double g = (k == 0) ? e->x1 : e->y1;
        int s = (k == 0) ? e->sx : e->sy;
        double r = e->r;
        if (s > 0) {
            double T = pos - g;
            if (T < 0.0) T = 0.0;
            if (T > r) T = r;
            return Wf(r, T);
        }
        double T = g - pos;
        if (T < 0.0) T = 0.0;
        if (T > r) T = r;
        return Wf(r, r) - Wf(r, T);
    }
    double lo = (k == 0) ? e->x1 : e->y1;
    double hi = (k == 0) ? e->x2 : e->y2;
    double w = ((k == 0) ? (e->y2 - e->y1) : (e->x2 - e->x1)) * e->sign;
    double t = pos - lo;
    if (t < 0.0) t = 0.0;
    if (t > hi - lo) t = hi - lo;
    return w * t;
}

/* integral de |coord - pos| dA */
static double absm(const Elem *e, int k, double pos) {
    if (e->kind == 1) {
        double g = (k == 0) ? e->x1 : e->y1;
        int s = (k == 0) ? e->sx : e->sy;
        double r = e->r;
        EP p = ep_of(e);
        double c = (k == 0) ? p.cx : p.cy;
        if ((pos - g) * (pos - g - s * r) >= 0.0) return p.a * my_fabs(c - pos);
        double z = 0.0, du = r / 40.0;
        for (int i = 0; i < 40; i++) {
            double u = (i + 0.5) * du;
            z += ww_fil(r, u) * my_fabs(g + s * u - pos) * du;
        }
        return z;
    }
    double lo = (k == 0) ? e->x1 : e->y1;
    double hi = (k == 0) ? e->x2 : e->y2;
    double w = ((k == 0) ? (e->y2 - e->y1) : (e->x2 - e->x1)) * e->sign;
    double f;
    if (pos <= lo) f = ((hi - pos) * (hi - pos) - (lo - pos) * (lo - pos)) / 2.0;
    else if (pos >= hi) f = ((pos - lo) * (pos - lo) - (pos - hi) * (pos - hi)) / 2.0;
    else f = ((pos - lo) * (pos - lo) + (hi - pos) * (hi - pos)) / 2.0;
    return w * f;
}

/* ---- resultados ---- */
static double R_A, R_xc, R_yc, R_Ix, R_Iy, R_Ixy;
static double R_x0, R_x9, R_y0, R_y9, R_xp, R_yp, R_Zx, R_Zy;
static double R_rx, R_ry, R_Sxs, R_Sxi, R_Syl, R_Syr;
static double R_Imax, R_Imin, R_ang, R_rmin, R_J, R_Cw;
static int R_asim = 0, R_ok = 0;

static double pna(int k, double a, double b) {
    for (int it = 0; it < 48; it++) {
        double p = (a + b) / 2.0, s = 0.0;
        for (int i = 0; i < NE; i++) s += below(&EL[i], k, p);
        if (s < R_A / 2.0) a = p; else b = p;
    }
    return (a + b) / 2.0;
}

static void recompute(void) {
    build_section();
    R_ok = 0;
    if (is_circ) {
        double D = cDe, d = cDi, c = D / 2.0;
        double D2 = D * D, d2 = d * d;
        R_A = PI * (D2 - d2) / 4.0;
        R_Ix = PI * (D2 * D2 - d2 * d2) / 64.0;
        R_Iy = R_Ix; R_Ixy = 0.0;
        R_xc = c; R_yc = c;
        R_x0 = 0.0; R_x9 = D; R_y0 = 0.0; R_y9 = D;
        R_xp = c; R_yp = c;
        R_Zx = (D * D * D - d * d * d) / 6.0;
        R_Zy = R_Zx;
        R_J = 2.0 * R_Ix;
    } else {
        if (NE == 0) return;
        double A = 0.0, qx = 0.0, qy = 0.0;
        for (int i = 0; i < NE; i++) {
            EP p = ep_of(&EL[i]);
            A += p.a; qx += p.a * p.cy; qy += p.a * p.cx;
        }
        if (A <= 0.0) return;
        R_A = A; R_xc = qy / A; R_yc = qx / A;
        R_Ix = 0.0; R_Iy = 0.0; R_Ixy = 0.0;
        for (int i = 0; i < NE; i++) {
            EP p = ep_of(&EL[i]);
            double dx = p.cx - R_xc, dy = p.cy - R_yc;
            R_Ix += p.ix + p.a * dy * dy;
            R_Iy += p.iy + p.a * dx * dx;
            R_Ixy += p.ixy + p.a * dx * dy;
        }
        int first = 1;
        for (int i = 0; i < NE; i++) {
            if (EL[i].kind != 0 || EL[i].sign < 0) continue;
            if (first) {
                R_x0 = EL[i].x1; R_x9 = EL[i].x2;
                R_y0 = EL[i].y1; R_y9 = EL[i].y2;
                first = 0;
            } else {
                if (EL[i].x1 < R_x0) R_x0 = EL[i].x1;
                if (EL[i].x2 > R_x9) R_x9 = EL[i].x2;
                if (EL[i].y1 < R_y0) R_y0 = EL[i].y1;
                if (EL[i].y2 > R_y9) R_y9 = EL[i].y2;
            }
        }
        if (first) return;
        R_xp = pna(0, R_x0, R_x9);
        R_yp = pna(1, R_y0, R_y9);
        R_Zx = 0.0; R_Zy = 0.0;
        for (int i = 0; i < NE; i++) {
            R_Zx += absm(&EL[i], 1, R_yp);
            R_Zy += absm(&EL[i], 0, R_xp);
        }
        R_J = jv;
    }
    R_rx = my_sqrt(R_Ix / R_A);
    R_ry = my_sqrt(R_Iy / R_A);
    double ds = R_y9 - R_yc, di = R_yc - R_y0;
    double dl = R_xc - R_x0, dr = R_x9 - R_xc;
    R_Sxs = ds > 1e-9 ? R_Ix / ds : 0.0;
    R_Sxi = di > 1e-9 ? R_Ix / di : 0.0;
    R_Syl = dl > 1e-9 ? R_Iy / dl : 0.0;
    R_Syr = dr > 1e-9 ? R_Iy / dr : 0.0;
    R_asim = (my_fabs(R_Ixy) > 1e-9 * (R_Ix + R_Iy)) ? 1 : 0;
    double m = (R_Ix + R_Iy) / 2.0;
    double q = my_sqrt(((R_Ix - R_Iy) / 2.0) * ((R_Ix - R_Iy) / 2.0) + R_Ixy * R_Ixy);
    R_Imax = m + q; R_Imin = m - q;
    R_ang = 0.5 * my_atan2(-2.0 * R_Ixy, R_Ix - R_Iy) * 180.0 / PI;
    R_rmin = my_sqrt((R_Imin > 0.0 ? R_Imin : 0.0) / R_A);
    R_Cw = (h0 > 0.0) ? R_Iy * h0 * h0 / 4.0 : 0.0;
    R_ok = 1;
}

/* ============================================================
   7. Dibujo de la seccion
   ============================================================ */
static int g_bx, g_by, g_bw, g_bh;
static double g_sc, g_ox, g_oy;

static int GX(double x) { return g_ox + (x - R_x0) * g_sc; }
static int GY(double y) { return g_oy - (y - R_y0) * g_sc; }

/* contorno = pixel de relleno vecino al fondo (silueta, sin lineas internas) */
static void silhouette(int x, int y, int w, int h) {
    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            if (get_px(px, py) != C_LBLUE) continue;
            if (get_px(px - 1, py) == C_WHITE || get_px(px + 1, py) == C_WHITE ||
                get_px(px, py - 1) == C_WHITE || get_px(px, py + 1) == C_WHITE)
                put_px(px, py, C_NAVY);
        }
    }
}

static void draw_section(int bx, int by, int bw, int bh, int full) {
    g_bx = bx; g_by = by; g_bw = bw; g_bh = bh;
    fill_rect(bx, by, bw, bh, C_WHITE);
    if (!R_ok) return;

    double dx = R_x9 - R_x0, dy = R_y9 - R_y0;
    if (dx < 1e-9) dx = 1e-9;
    if (dy < 1e-9) dy = 1e-9;
    int mg = full ? 14 : 8;
    double s1 = (double)(bw - 2 * mg) / dx;
    double s2 = (double)(bh - 2 * mg) / dy;
    g_sc = s1 < s2 ? s1 : s2;
    g_ox = bx + (bw - dx * g_sc) / 2.0;
    g_oy = by + (bh + dy * g_sc) / 2.0;

    if (is_circ) {
        double ro = cDe / 2.0 * g_sc, ri = cDi / 2.0 * g_sc;
        int cx = GX(R_xc), cy = GY(R_yc);
        int iro = (int)ro;
        for (int j = -iro; j <= iro; j++) {
            double wq = ro * ro - (double)j * j;
            int w = (int)my_sqrt(wq < 0.0 ? 0.0 : wq);
            double wiq = ri * ri - (double)j * j;
            int wi = (ri > 0.0 && wiq > 0.0) ? (int)my_sqrt(wiq) : -1;
            if (wi >= 0) {
                fill_rect(cx - w, cy + j, w - wi, 1, C_LBLUE);
                fill_rect(cx + wi + 1, cy + j, w - wi, 1, C_LBLUE);
            } else {
                fill_rect(cx - w, cy + j, 2 * w + 1, 1, C_LBLUE);
            }
        }
    } else {
        for (int i = 0; i < NE; i++)
            if (EL[i].kind == 0 && EL[i].sign > 0)
                fill_rect(GX(EL[i].x1), GY(EL[i].y2),
                          GX(EL[i].x2) - GX(EL[i].x1),
                          GY(EL[i].y1) - GY(EL[i].y2), C_LBLUE);
        for (int i = 0; i < NE; i++) {
            if (EL[i].kind != 1) continue;
            double fx = EL[i].x1, fy = EL[i].y1, r = EL[i].r;
            int sx = EL[i].sx, sy = EL[i].sy;
            int ya = GY(fy + (sy > 0 ? r : 0.0));
            int yb = GY(fy - (sy > 0 ? 0.0 : r));
            for (int py = ya; py <= yb; py++) {
                double yr = R_y0 + (g_oy - (double)py) / g_sc;
                double v = my_fabs(yr - fy);
                if (v > r) continue;
                int w = (int)(ww_fil(r, v) * g_sc);
                if (w <= 0) continue;
                int xa = GX(fx);
                if (sx > 0) fill_rect(xa, py, w, 1, C_LBLUE);
                else fill_rect(xa - w, py, w, 1, C_LBLUE);
            }
        }
        for (int i = 0; i < NE; i++)
            if (EL[i].kind == 0 && EL[i].sign < 0)
                fill_rect(GX(EL[i].x1), GY(EL[i].y2),
                          GX(EL[i].x2) - GX(EL[i].x1),
                          GY(EL[i].y1) - GY(EL[i].y2), C_WHITE);
    }
    silhouette(bx, by, bw, bh);

    if (!full) return;
    /* ejes neutros plasticos */
    dash_h(GX(R_x0) - 8, GX(R_x9) + 8, GY(R_yp), C_GREEN);
    dash_v(GX(R_xp), GY(R_y9) - 8, GY(R_y0) + 8, C_GREEN);
    /* centro de gravedad */
    int cx = GX(R_xc), cy = GY(R_yc);
    fill_rect(cx - 6, cy, 13, 1, C_RED);
    fill_rect(cx, cy - 6, 1, 13, C_RED);
    draw_rect(cx - 2, cy - 2, 5, 5, C_RED);
}

/* ============================================================
   8. Ventana de edicion numerica
   ============================================================ */
static int edit_number(const char *title, const char *dsc, const char *unit,
                       double *target, char first_char) {
    char buf[20];
    int len = 0;
    if (first_char != 0) {
        buf[0] = first_char; buf[1] = '\0'; len = 1;
    } else {
        fmt_num(buf, *target, 2);
        len = (int)strlen(buf);
    }
    while (1) {
        int bx = 16, by = 40, bw = 352, bh = 124;
        fill_rect(bx + 3, by + 3, bw, bh, C_GRAY);
        fill_rect(bx, by, bw, bh, C_WHITE);
        draw_rect(bx, by, bw, bh, C_NAVY);
        fill_rect(bx, by, bw, 22, C_NAVY);
        draw_text_bold(bx + 8, by + 4, title, C_WHITE, C_NAVY, 1);
        draw_text(bx + 14, by + 26, dsc, C_SLATE, C_WHITE, 1);

        fill_rect(bx + 14, by + 42, bw - 28, 28, C_LBLUE);
        draw_rect(bx + 14, by + 42, bw - 28, 28, C_BLUE);
        char disp[28];
        strcpy(disp, buf);
        str_cat(disp, "_");
        draw_text_bold(bx + 22, by + 48, disp, C_BLACK, C_LBLUE, 1);
        draw_text(bx + bw - 28 - tw_px(unit), by + 48, unit, C_NAVY, C_LBLUE, 1);

        draw_text(bx + 14, by + 78, "0-9 . escribe   DEL borra", C_SLATE, C_WHITE, 1);
        draw_text(bx + 14, by + 78 + ROW, "EXE acepta   EXIT cancela", C_BLUE, C_WHITE, 1);
        Bdisp_PutDisp_DD();

        int key = 0;
        GetKey(&key);
        if (key == KEY_CTRL_EXE) {
            double v;
            if (parse_num(buf, &v)) {
                if (v < 0.0) v = -v;
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
            if (len < 14) { buf[len++] = (char)('0' + (key - KEY_CHAR_0)); buf[len] = '\0'; }
        } else if (key == KEY_CHAR_DP || key == KEY_CHAR_COMMA) {
            int hd = 0;
            for (int i = 0; i < len; i++) if (buf[i] == '.') hd = 1;
            if (!hd && len < 14) {
                if (len == 0) buf[len++] = '0';
                buf[len++] = '.'; buf[len] = '\0';
            }
        }
    }
}

/* ============================================================
   9. Pantallas
   ============================================================ */
static void u_suf(char *dst, int pw) {
    strcpy(dst, UN[uidx]);
    if (pw == 2) str_cat(dst, "2");
    else if (pw == 3) str_cat(dst, "3");
    else if (pw == 4) str_cat(dst, "4");
    else if (pw == 6) str_cat(dst, "6");
}

/* fila: etiqueta a la izquierda, valor alineado a la derecha.
   Si no cabe con unidad, la quita; si aun no cabe, usa notacion corta. */
static void row_val(int x, int y, int w, const char *lbl, double v, int pw, unsigned short vc) {
    char s[32], num[24], u[8];
    draw_text(x, y, lbl, C_SLATE, C_WHITE, 1);
    int lw = tw_px(lbl) + FW;
    fmt_sig(num, v);
    strcpy(s, num);
    if (pw > 0) {
        u_suf(u, pw);
        str_cat(s, " ");
        str_cat(s, u);
    }
    if (lw + tw_px(s) > w) strcpy(s, num);
    if (lw + tw_px(s) > w) fmt_short(s, v);
    int tx = x + w - tw_px(s);
    if (tx < x + lw) tx = x + lw;
    draw_text_bold(tx, y, s, vc, C_WHITE, 1);
}

static void row_txt(int x, int y, int w, const char *lbl, const char *val, unsigned short vc) {
    draw_text(x, y, lbl, C_SLATE, C_WHITE, 1);
    int lw = tw_px(lbl) + FW;
    int tx = x + w - tw_px(val);
    if (tx < x + lw) tx = x + lw;
    draw_text_bold(tx, y, val, vc, C_WHITE, 1);
}

static void draw_chrome(void) {
    fill_rect(0, 0, SCREEN_W, SCREEN_H, C_BG);

    /* barra superior */
    fill_rect(0, 0, SCREEN_W, TOPH, C_NAVY);
    draw_text_bold(8, 3, "SECPROP", C_WHITE, C_NAVY, 1);
    char info[32];
    strcpy(info, SEC[sec_type].shortn);
    if (SEC[sec_type].fil_ok && use_fil) str_cat(info, " +r");
    str_cat(info, " ");
    str_cat(info, UN[uidx]);
    draw_text(SCREEN_W - 8 - tw_px(info), 3, info, C_LBLUE, C_NAVY, 1);

    /* barra de pestanas: clara, con la activa en azul */
    const char *tabs[6] = { "TIPO", "DATOS", "GEOM", "FLEX", "TORS", "PLANO" };
    fill_rect(0, TABY - 1, SCREEN_W, 1, C_SLATE);
    for (int i = 0; i < 6; i++) {
        int x = i * 64;
        int w = (i == 5) ? (SCREEN_W - x) : 64;
        int act = (i == active_tab);
        unsigned short bg = act ? C_BLUE : C_LGRAY;
        unsigned short fg = act ? C_WHITE : C_SLATE;
        fill_rect(x, TABY, w, SCREEN_H - TABY, bg);
        if (i > 0) fill_rect(x, TABY, 1, SCREEN_H - TABY, C_GRAY);
        int tx = x + (w - tw_px(tabs[i])) / 2;
        if (tx < x + 1) tx = x + 1;
        if (act) {
            fill_rect(x + 2, TABY + 1, w - 4, 2, C_LBLUE);
            draw_text_bold(tx, TABY + 4, tabs[i], fg, bg, 1);
        } else {
            draw_text(tx, TABY + 4, tabs[i], fg, bg, 1);
        }
    }
}

static int tipo_vx(const char *v) {
    int x = 16 + 8 * FW;
    if (x + tw_px(v) > 368) x = 368 - tw_px(v);
    if (x < 16 + 8 * FW / 2) x = 16 + 8 * FW / 2;
    return x;
}

static void render_tipo(void) {
    int h1 = HDR + 6 + 3 * ROW;
    card(8, CY0, 368, h1, "1. LA SECCION");
    int y = CY0 + HDR + 4;
    for (int i = 0; i < 3; i++) {
        int sel = (i == cur_tipo);
        unsigned short bg = sel ? C_LBLUE : C_WHITE;
        fill_rect(11, y - 1, 362, ROW, bg);
        if (sel) draw_rect(11, y - 1, 362, ROW, C_BLUE);
        char v[40];
        if (i == 0) {
            draw_text(16, y, "Perfil", C_SLATE, bg, 1);
            strcpy(v, "< ");
            str_cat(v, SEC[sec_type].name);
            str_cat(v, " >");
            draw_text_bold(tipo_vx(v), y, v, C_NAVY, bg, 1);
        } else if (i == 1) {
            draw_text(16, y, "Filetes", C_SLATE, bg, 1);
            if (!SEC[sec_type].fil_ok) {
                draw_text(tipo_vx("no aplica"), y, "no aplica", C_GRAY, bg, 1);
            } else {
                strcpy(v, use_fil ? "< SI, con r >" : "< NO, sin r >");
                draw_text_bold(tipo_vx(v), y, v, use_fil ? C_DGREEN : C_RED, bg, 1);
            }
        } else {
            draw_text(16, y, "Unidad", C_SLATE, bg, 1);
            strcpy(v, "< ");
            str_cat(v, UN[uidx]);
            str_cat(v, " >");
            draw_text_bold(tipo_vx(v), y, v, C_NAVY, bg, 1);
        }
        y += ROW;
    }
    int y2 = CY0 + h1 + 4;
    card(8, y2, 184, CY1 - y2, "VISTA PREVIA");
    draw_section(10, y2 + HDR + 2, 180, CY1 - y2 - HDR - 4, 0);
    card(196, y2, 180, CY1 - y2, "TECLAS");
    int yk = y2 + HDR + 4;
    draw_text(202, yk, "< >  cambiar", C_SLATE, C_WHITE, 1);
    draw_text(202, yk + ROW, "^ v  mover", C_SLATE, C_WHITE, 1);
    draw_text(202, yk + 2 * ROW, "F2  medidas", C_BLUE, C_WHITE, 1);
}

static void render_datos(void) {
    const SecDef *s = &SEC[sec_type];
    int nd = s->ndim + ((s->fil_ok && use_fil) ? 1 : 0);
    if (cur_dato >= nd) cur_dato = nd - 1;
    if (cur_dato < 0) cur_dato = 0;

    char t[40];
    strcpy(t, "2. MEDIDAS - ");
    str_cat(t, s->name);
    card(8, CY0, 368, CY1 - CY0, t);

    int y = CY0 + HDR + 4;
    for (int i = 0; i < nd; i++) {
        int sel = (i == cur_dato);
        unsigned short bg = sel ? C_LBLUE : C_WHITE;
        fill_rect(11, y - 1, 362, ROW, bg);
        if (sel) draw_rect(11, y - 1, 362, ROW, C_BLUE);
        const char *lbl = (i < s->ndim) ? s->lbl[i] : "r";
        const char *dsc = (i < s->ndim) ? s->dsc[i] : "radio acuerdo";
        char v[24], u[8];
        draw_text_bold(16, y, lbl, C_BLUE, bg, 1);
        draw_text(16 + 4 * FW, y, dsc, C_SLATE, bg, 1);
        fmt_num(v, dim[i], 2);
        u_suf(u, 1);
        str_cat(v, " ");
        str_cat(v, u);
        draw_text_bold(370 - tw_px(v), y, v, C_BLACK, bg, 1);
        y += ROW;
    }
    if (nd < 6) {
        draw_text(16, y + 4, "Teclea un numero o EXE", C_GRAY, C_WHITE, 1);
        draw_text(16, y + 4 + ROW, "para editar la medida.", C_GRAY, C_WHITE, 1);
    }
}

static void render_geom(void) {
    int H = CY1 - CY0;
    card(8, CY0, 184, H, "GEOMETRIA");
    int y = CY0 + HDR + 4;
    row_val(14, y, 172, "Ag", R_A, 2, C_BLUE);           y += ROW;
    row_val(14, y, 172, "d", R_y9 - R_y0, 1, C_BLACK);   y += ROW;
    row_val(14, y, 172, "bf", R_x9 - R_x0, 1, C_BLACK);  y += ROW;
    row_val(14, y, 172, "xc izq", R_xc - R_x0, 1, C_RED);   y += ROW;
    row_val(14, y, 172, "yc base", R_yc - R_y0, 1, C_RED);  y += ROW;
    row_val(14, y, 172, "rx", R_rx, 1, C_DGREEN);        y += ROW;
    row_val(14, y, 172, "ry", R_ry, 1, C_DGREEN);

    card(196, CY0, 180, H, "INERCIA CG");
    y = CY0 + HDR + 4;
    row_val(202, y, 168, "Ix", R_Ix, 4, C_BLUE);   y += ROW;
    row_val(202, y, 168, "Iy", R_Iy, 4, C_BLUE);   y += ROW;
    row_val(202, y, 168, "Ixy", R_Ixy, 4, C_BLACK); y += ROW;
    row_val(202, y, 168, "Imax", R_Imax, 4, C_DGREEN); y += ROW;
    row_val(202, y, 168, "Imin", R_Imin, 4, C_DGREEN); y += ROW;
    draw_text(202, y, "xc,yc desde", C_GRAY, C_WHITE, 1); y += ROW;
    draw_text(202, y, "esq. inf.izq.", C_GRAY, C_WHITE, 1);
}

static void render_flex(void) {
    int H = CY1 - CY0;
    char t[24];
    strcpy(t, "MODULO S ");
    char u[8]; u_suf(u, 3);
    str_cat(t, u);
    card(8, CY0, 184, H, t);
    int y = CY0 + HDR + 4;
    row_val(14, y, 172, "Sx sup", R_Sxs, 0, C_BLUE);  y += ROW;
    row_val(14, y, 172, "Sx inf", R_Sxi, 0, C_BLUE);  y += ROW;
    row_val(14, y, 172, "Sy izq", R_Syl, 0, C_BLACK); y += ROW;
    row_val(14, y, 172, "Sy der", R_Syr, 0, C_BLACK); y += ROW;
    double Sx = R_Sxs < R_Sxi ? R_Sxs : R_Sxi;
    double Sy = R_Syl < R_Syr ? R_Syl : R_Syr;
    row_val(14, y, 172, "Sx min", Sx, 0, C_RED); y += ROW;
    row_val(14, y, 172, "Sy min", Sy, 0, C_RED); y += ROW;
    draw_text(14, y, "min = diseno", C_GRAY, C_WHITE, 1);

    card(196, CY0, 180, H, "PLASTICO");
    y = CY0 + HDR + 4;
    row_val(202, y, 168, "Zx", R_Zx, 3, C_BLUE); y += ROW;
    row_val(202, y, 168, "Zy", R_Zy, 3, C_BLUE); y += ROW;
    row_val(202, y, 168, "yp base", R_yp - R_y0, 1, C_DGREEN); y += ROW;
    row_val(202, y, 168, "xp izq", R_xp - R_x0, 1, C_DGREEN);  y += ROW;
    char s[16];
    fmt_num(s, (Sx > 1e-9) ? R_Zx / Sx : 0.0, 3);
    row_txt(202, y, 168, "Zx/Sx", s, C_RED); y += ROW;
    fmt_num(s, (Sy > 1e-9) ? R_Zy / Sy : 0.0, 3);
    row_txt(202, y, 168, "Zy/Sy", s, C_RED); y += ROW;
    draw_text(202, y, "yp,xp = eje", C_GRAY, C_WHITE, 1);
}

static void render_tors(void) {
    int H = CY1 - CY0;
    card(8, CY0, 184, H, "TORSION (aprox.)");
    int y = CY0 + HDR + 4;
    if (R_J > 0.0) row_val(14, y, 172, "J", R_J, 4, C_BLUE);
    else row_txt(14, y, 172, "J", "n/d", C_GRAY);
    y += ROW;
    if (R_Cw > 0.0) {
        row_val(14, y, 172, "Cw", R_Cw, 6, C_BLUE); y += ROW;
        row_val(14, y, 172, "ho", h0, 1, C_BLACK);  y += ROW;
    } else {
        row_txt(14, y, 172, "Cw", "solo I sim.", C_GRAY); y += ROW;
        y += ROW;
    }
    draw_text(14, y, "J aproximado", C_GRAY, C_WHITE, 1); y += ROW;
    draw_text(14, y, "abierto b t3/3", C_GRAY, C_WHITE, 1);  y += ROW;
    draw_text(14, y, "cerrado Bredt", C_GRAY, C_WHITE, 1);   y += ROW;
    draw_text(14, y, "circular 2 Ix", C_GRAY, C_WHITE, 1);

    card(196, CY0, 180, H, "EJES PRINCIP.");
    y = CY0 + HDR + 4;
    if (R_asim) {
        char s[16];
        fmt_num(s, R_ang, 2);
        str_cat(s, " g");
        row_txt(202, y, 168, "ang", s, C_RED); y += ROW;
        draw_text(202, y, "desde x, antih", C_GRAY, C_WHITE, 1); y += ROW;
        row_val(202, y, 168, "rmin", R_rmin, 1, C_DGREEN); y += ROW;
        y += 4;
        draw_text(202, y, "Ixy no es 0:", C_ORANGE, C_WHITE, 1); y += ROW;
        draw_text(202, y, "el eje debil", C_ORANGE, C_WHITE, 1); y += ROW;
        draw_text(202, y, "inclinado:", C_ORANGE, C_WHITE, 1);  y += ROW;
        draw_text(202, y, "rmin: pandeo", C_ORANGE, C_WHITE, 1);
    } else {
        draw_text(202, y, "Ixy = 0", C_NAVY, C_WHITE, 1); y += ROW;
        y += 4;
        draw_text(202, y, "Los ejes x,y", C_SLATE, C_WHITE, 1); y += ROW;
        draw_text(202, y, "en el CG son", C_SLATE, C_WHITE, 1); y += ROW;
        draw_text(202, y, "los principa-", C_SLATE, C_WHITE, 1); y += ROW;
        draw_text(202, y, "les:", C_SLATE, C_WHITE, 1); y += ROW;
        draw_text(202, y, "Imax = Ix", C_BLUE, C_WHITE, 1); y += ROW;
        draw_text(202, y, "Imin = Iy", C_BLUE, C_WHITE, 1);
    }
}

static void render_dibujo(void) {
    char t[32], u[8];
    u_suf(u, 1);
    strcpy(t, "SECCION A ESCALA (");
    str_cat(t, u);
    str_cat(t, ")");
    card(8, CY0, 368, CY1 - CY0, t);
    int ytop = CY0 + HDR + 2;
    int ybot = CY1 - ROW - 4;
    draw_section(10, ytop, 364, ybot - ytop, 1);

    int yl = ybot + 4;
    fill_rect(20, yl + FH / 2, 13, 1, C_RED);
    fill_rect(26, yl + FH / 2 - 6, 1, 13, C_RED);
    draw_text(42, yl, "CG", C_RED, C_WHITE, 1);
    int x2 = 42 + 3 * FW;
    dash_h(x2, x2 + 22, yl + FH / 2, C_GREEN);
    draw_text(x2 + 30, yl, "eje neutro plastico", C_DGREEN, C_WHITE, 1);
}

/* ============================================================
   10. Programa principal
   ============================================================ */
int main(void) {
    EnableStatusArea(3);
    calibrate();
    load_defaults();
    recompute();

    while (1) {
        draw_chrome();
        if (active_tab == 0) render_tipo();
        else if (active_tab == 1) render_datos();
        else if (active_tab == 2) render_geom();
        else if (active_tab == 3) render_flex();
        else if (active_tab == 4) render_tors();
        else render_dibujo();
        Bdisp_PutDisp_DD();

        int key = 0;
        GetKey(&key);

        if (key == KEY_CTRL_F1) { active_tab = 0; continue; }
        if (key == KEY_CTRL_F2) { active_tab = 1; continue; }
        if (key == KEY_CTRL_F3) { active_tab = 2; continue; }
        if (key == KEY_CTRL_F4) { active_tab = 3; continue; }
        if (key == KEY_CTRL_F5) { active_tab = 4; continue; }
        if (key == KEY_CTRL_F6) { active_tab = 5; continue; }

        if (active_tab == 0) {
            if (key == KEY_CTRL_UP) cur_tipo = (cur_tipo + 2) % 3;
            else if (key == KEY_CTRL_DOWN) cur_tipo = (cur_tipo + 1) % 3;
            else if (key == KEY_CTRL_LEFT || key == KEY_CTRL_RIGHT) {
                int d = (key == KEY_CTRL_RIGHT) ? 1 : -1;
                if (cur_tipo == 0) {
                    sec_type = (sec_type + S_NTIPOS + d) % S_NTIPOS;
                    use_fil = SEC[sec_type].fil_ok ? 1 : 0;
                    cur_dato = 0;
                    load_defaults();
                    recompute();
                } else if (cur_tipo == 1) {
                    if (SEC[sec_type].fil_ok) { use_fil = !use_fil; recompute(); }
                } else {
                    uidx = (uidx + 3 + d) % 3;
                }
            } else if (key == KEY_CTRL_EXE) {
                active_tab = 1;
            }
        } else if (active_tab == 1) {
            const SecDef *s = &SEC[sec_type];
            int nd = s->ndim + ((s->fil_ok && use_fil) ? 1 : 0);
            char typed = 0;
            if (key >= KEY_CHAR_0 && key <= KEY_CHAR_9) typed = (char)('0' + (key - KEY_CHAR_0));
            else if (key == KEY_CHAR_DP || key == KEY_CHAR_COMMA) typed = '.';

            if (key == KEY_CTRL_UP) cur_dato = (cur_dato + nd - 1) % nd;
            else if (key == KEY_CTRL_DOWN) cur_dato = (cur_dato + 1) % nd;
            else if (key == KEY_CTRL_EXE || typed != 0) {
                const char *lbl = (cur_dato < s->ndim) ? s->lbl[cur_dato] : "r";
                const char *dsc = (cur_dato < s->ndim) ? s->dsc[cur_dato] : "radio de acuerdo";
                char ttl[40];
                strcpy(ttl, lbl);
                str_cat(ttl, " : ");
                str_cat(ttl, dsc);
                if (edit_number(ttl, SEC[sec_type].name, UN[uidx], &dim[cur_dato], typed))
                    recompute();
            }
        }
    }
    return 0;
}
