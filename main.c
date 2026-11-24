#include <fxcg/display.h>
#include <fxcg/keyboard.h>
#include <fxcg/misc.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_PT 20
#define MAX_DZ 15
#define MAX_MO 15
#define MAX_SP 12
#define MAX_TR 65

#define RGB565(r,g,b) (unsigned short)((((r)&0x1F)<<11)|(((g)&0x3F)<<5)|((b)&0x1F))

typedef struct { double p, a; } Puntual;
typedef struct { double w1, w2, c, d; } Distrib;
typedef struct { double m, a; } Momento;

/* Datos de entrada */
static int u_in = 0, u_out = 0;
static const char *UN[3] = {"kgf", "N", "kN"};
static const double FK[3] = {9.80665, 1.0, 1000.0};

static double L = 6.0;
static Puntual pt[MAX_PT]; static int n_pt = 0;
static Distrib dz[MAX_DZ]; static int n_dz = 0;
static Momento mo[MAX_MO]; static int n_mo = 0;
static double sp[MAX_SP];  static int n_sp = 0;
static int emp_E = 0, emp_F = 0;
static double ir[MAX_SP];

/* Resultados base y escalados */
static double Rs0[MAX_SP], Ms0[MAX_SP], G0, H0, P_tot0;
static double X_kp[MAX_TR]; static int n_kp = 0;
static double CF0[MAX_TR][4], CF[MAX_TR][4];
static double ex0[8], ex[8];
static double Rs[MAX_SP], Ms[MAX_SP], G_r, H_r, P_tot;

static const double GX[3] = {-0.7745966692414834, 0.0, 0.7745966692414834};
static const double GW[3] = {5.0/9.0, 8.0/9.0, 5.0/9.0};

/* --- PRIMITIVAS GRAFICAS SOBRE VRAM (384x216) --- */
static unsigned short *vram_ptr(void) {
    return (unsigned short *)GetVRAMAddress();
}

static void cls(unsigned short col) {
    unsigned short *v = vram_ptr();
    for (int i = 0; i < 384 * 216; i++) v[i] = col;
}

static void pset(int x, int y, unsigned short col) {
    if ((unsigned)x < 384 && (unsigned)y < 216)
        vram_ptr()[y * 384 + x] = col;
}

static void fill_rect(int x1, int y1, int x2, int y2, unsigned short col) {
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    for (int y = y1; y <= y2; y++)
        for (int x = x1; x <= x2; x++) pset(x, y, col);
}

static void box(int x1, int y1, int x2, int y2, unsigned short col) {
    for (int x = x1; x <= x2; x++) { pset(x, y1, col); pset(x, y2, col); }
    for (int y = y1; y <= y2; y++) { pset(x1, y, col); pset(x2, y, col); }
}

static void line(int x1, int y1, int x2, int y2, unsigned short col) {
    int dx = abs(x2 - x1), dy = abs(y2 - y1);
    int n = dx > dy ? dx : dy;
    if (n == 0) { pset(x1, y1, col); return; }
    for (int i = 0; i <= n; i++) {
        int x = x1 + (x2 - x1) * i / n;
        int y = y1 + (y2 - y1) * i / n;
        pset(x, y, col);
    }
}

/* Texto pequeno nativo (col_id: 0=Negro, 1=Azul, 2=Verde, 4=Rojo, 5=Morado, 7=Blanco) */
static void txt_s(int x, int y, const char *s, char col_id) {
    int px = x, py = y;
    PrintMiniMini(&px, &py, s, 0, col_id, 0);
}

static void fm(char *buf, double v) {
    if (fabs(v) < 1e-9) v = 0.0;
    sprintf(buf, "%.5g", v);
}

static void draw_fkey(int idx, const char *txt, int active) {
    int x = idx * 64;
    unsigned short bg = active ? RGB565(0, 0, 0) : RGB565(28, 56, 29);
    fill_rect(x + 1, 200, x + 62, 215, bg);
    box(x + 1, 200, x + 62, 215, RGB565(0, 0, 0));
    int tx = x + 32 - (int)strlen(txt) * 3;
    txt_s(tx, 204, txt, active ? TEXT_COLOR_WHITE : TEXT_COLOR_BLACK);
}

static void draw_bar(const char *k1, const char *k2, const char *k3,
                     const char *k4, const char *k5, const char *k6, int act) {
    const char *ks[6] = {k1, k2, k3, k4, k5, k6};
    for (int i = 0; i < 6; i++)
        if (ks[i][0]) draw_fkey(i, ks[i], i == act);
}

static void header(const char *title) {
    fill_rect(0, 0, 383, 15, RGB565(0, 12, 20));
    txt_s(6, 4, title, TEXT_COLOR_WHITE);
}

/* Entrada numerica con teclado fisico de la fx-CG50 */
static double input_num(const char *prompt, double mn, double mx, int check_rng) {
    char buf[24] = "";
    int len = 0;
    while (1) {
        cls(RGB565(31, 63, 31));
        header("INGRESO DE DATOS - VIGAS");
        txt_s(10, 28, prompt, TEXT_COLOR_BLACK);
        if (check_rng) {
            char b1[16], b2[16], rng[56]; fm(b1, mn); fm(b2, mx);
            sprintf(rng, "Rango permitido: [%s .. %s]", b1, b2);
            txt_s(10, 42, rng, TEXT_COLOR_BLUE);
        }
        fill_rect(10, 62, 373, 86, RGB565(30, 61, 31));
        box(10, 62, 373, 86, RGB565(0, 16, 24));
        char disp[28]; sprintf(disp, "%s_", buf);
        txt_s(16, 70, disp, TEXT_COLOR_BLACK);
        txt_s(10, 100, "[0-9] [.] [(-)]   [DEL]: Borrar   [EXE]: Aceptar", TEXT_COLOR_BLACK);
        Bdisp_PutDisp_DD();

        int k = 0;
        GetKey(&k);
        if (k == KEY_CTRL_EXE && len > 0) {
            if (strcmp(buf, "-") == 0 || strcmp(buf, ".") == 0) continue;
            double val = atof(buf);
            if (!check_rng || (val >= mn - 1e-9 && val <= mx + 1e-9)) return val;
            txt_s(10, 122, "Valor fuera de rango!", TEXT_COLOR_RED);
            Bdisp_PutDisp_DD();
            GetKey(&k);
        } else if (k == KEY_CTRL_DEL && len > 0) {
            buf[--len] = '\0';
        } else if (k == KEY_CTRL_AC) {
            len = 0; buf[0] = '\0';
        } else if (len < 18) {
            char c = 0;
            if (k >= KEY_CHAR_0 && k <= KEY_CHAR_9) c = '0' + (k - KEY_CHAR_0);
            else if (k == KEY_CHAR_DP && !strchr(buf, '.')) c = '.';
            else if ((k == KEY_CHAR_PMINUS || k == KEY_CHAR_MINUS) && len == 0) c = '-';
            if (c) { buf[len++] = c; buf[len] = '\0'; }
        }
    }
}

static int select_unit(const char *title) {
    while (1) {
        cls(RGB565(31, 63, 31));
        header(title);
        txt_s(14, 38, "Selecciona la unidad de fuerza:", TEXT_COLOR_BLACK);
        txt_s(14, 62, "F1: kgf (Kilogramo-fuerza)", TEXT_COLOR_BLUE);
        txt_s(14, 78, "F2: N   (Newton)", TEXT_COLOR_BLUE);
        txt_s(14, 94, "F3: kN  (Kilonewton)", TEXT_COLOR_BLUE);
        draw_bar("kgf", "N", "kN", "", "", "", -1);
        Bdisp_PutDisp_DD();
        int k = 0; GetKey(&k);
        if (k == KEY_CTRL_F1) return 0;
        if (k == KEY_CTRL_F2) return 1;
        if (k == KEY_CTRL_F3) return 2;
    }
}

/* --- MOTOR MATEMATICO (GAUSS + 3 MOMENTOS + POLINOMIOS) --- */
static double eval_w(Distrib q, double x) {
    return q.w1 + (q.w2 - q.w1) / (q.d - q.c) * (x - q.c);
}

static double itg_m(Distrib q, double y, double D, double a, double b) {
    double h = (b - a) / 2.0, mid = (a + b) / 2.0, s = 0.0;
    for (int i = 0; i < 3; i++) {
        double x = mid + h * GX[i];
        s += GW[i] * eval_w(q, x) * (x - y) * (D*D - (x - y)*(x - y));
    }
    return h * s;
}

static double itg_t(Distrib q, double z, double D, double a, double b) {
    double h = (b - a) / 2.0, mid = (a + b) / 2.0, s = 0.0;
    for (int i = 0; i < 3; i++) {
        double x = mid + h * GX[i];
        s += GW[i] * eval_w(q, x) * (z - x) * (D*D - (z - x)*(z - x));
    }
    return h * s;
}

static double itg_o(Distrib q, double z, double a, double b) {
    double h = (b - a) / 2.0, mid = (a + b) / 2.0, s = 0.0;
    for (int i = 0; i < 3; i++) {
        double x = mid + h * GX[i];
        s += GW[i] * eval_w(q, x) * (z - x);
    }
    return h * s;
}

static double itg_vol(Distrib q, double ref, int sign, double a, double b) {
    double h = (b - a) / 2.0, mid = (a + b) / 2.0, s = 0.0;
    for (int i = 0; i < 3; i++) {
        double x = mid + h * GX[i];
        s += GW[i] * eval_w(q, x) * (sign > 0 ? (ref - x) : (x - ref));
    }
    return h * s;
}

static void solve_lin(int n, double A[MAX_SP][MAX_SP], double B[MAX_SP], double x[MAX_SP]) {
    for (int i = 0; i < n; i++) {
        int p = i;
        for (int r = i + 1; r < n; r++)
            if (fabs(A[r][i]) > fabs(A[p][i])) p = r;
        for (int c = 0; c < n; c++) { double tmp = A[i][c]; A[i][c] = A[p][c]; A[p][c] = tmp; }
        double tb = B[i]; B[i] = B[p]; B[p] = tb;
        for (int r = i + 1; r < n; r++) {
            double f = A[r][i] / A[i][i];
            for (int c = i; c < n; c++) A[r][c] -= f * A[i][c];
            B[r] -= f * B[i];
        }
    }
    for (int i = n - 1; i >= 0; i--) {
        double s = B[i];
        for (int c = i + 1; c < n; c++) s -= A[i][c] * x[c];
        x[i] = s / A[i][i];
    }
}

static int cmp_dbl(const void *a, const void *b) {
    double da = *(const double*)a, db = *(const double*)b;
    return (da > db) - (da < db);
}

static void calc(void) {
    double P = 0, Q = 0, R = 0;
    G0 = 0; H0 = 0;
    for (int i = 0; i < n_pt; i++) { P += pt[i].p; Q += pt[i].p * pt[i].a; }
    for (int i = 0; i < n_dz; i++) {
        double c = dz[i].c, d = dz[i].d, w1 = dz[i].w1, w2 = dz[i].w2;
        P += (w1 + w2) * (d - c) / 2.0;
        Q += (d - c) * (w1 * (2*c + d) + w2 * (c + 2*d)) / 6.0;
    }
    for (int i = 0; i < n_mo; i++) R += mo[i].m;
    P_tot0 = P;

    if (n_sp == 1) {
        Rs0[0] = P; Ms0[0] = 0.0;
        if (emp_E) G0 = -(Q + R);
        else H0 = P * L - Q - R;
    } else {
        int N = n_sp - 1;
        double LT[MAX_SP], RT[MAX_SP], R0[MAX_SP], TT[MAX_SP];
        for (int j = 0; j < N; j++) {
            double y = sp[j], z = sp[j+1], D = z - y;
            double t = 0, m = 0, o = 0, k = 0;
            for (int i = 0; i < n_pt; i++) {
                double p = pt[i].p, a = pt[i].a;
                if (a > y && a < z) {
                    double b = a - y, c = z - a;
                    m += p * b * (D*D - b*b) / D;
                    t += p * c * (D*D - c*c) / D;
                    o += p * c / D; k += p;
                }
            }
            for (int i = 0; i < n_dz; i++) {
                double g = dz[i].c > y ? dz[i].c : y;
                double h = dz[i].d < z ? dz[i].d : z;
                if (h > g) {
                    m += itg_m(dz[i], y, D, g, h) / D;
                    t += itg_t(dz[i], z, D, g, h) / D;
                    o += itg_o(dz[i], z, g, h) / D;
                    k += (eval_w(dz[i], g) + eval_w(dz[i], h)) * (h - g) / 2.0;
                }
            }
            for (int i = 0; i < n_mo; i++) {
                double c0 = mo[i].m, a = mo[i].a;
                if (a >= y && a < z) {
                    double b = a - y, c = z - a;
                    m += c0 * (D*D - 3*b*b) / D;
                    t -= c0 * (D*D - 3*c*c) / D;
                    o -= c0 / D;
                }
            }
            LT[j] = t; RT[j] = m; R0[j] = o; TT[j] = k;
        }
        double y = sp[0], z = sp[n_sp-1], ml = 0, mr = 0, vl = 0, vr = 0;
        for (int i = 0; i < n_pt; i++) {
            if (pt[i].a < y) { ml -= pt[i].p * (y - pt[i].a); vl += pt[i].p; }
            if (pt[i].a > z) { mr -= pt[i].p * (pt[i].a - z); vr += pt[i].p; }
        }
        for (int i = 0; i < n_mo; i++) {
            if (mo[i].a < y) ml += mo[i].m;
            if (mo[i].a >= z) mr -= mo[i].m;
        }
        for (int i = 0; i < n_dz; i++) {
            double c = dz[i].c, d = dz[i].d;
            if (c < y) {
                double h = d < y ? d : y;
                ml -= itg_vol(dz[i], y, 1, c, h);
                vl += (eval_w(dz[i], c) + eval_w(dz[i], h)) * (h - c) / 2.0;
            }
            if (d > z) {
                double g = c > z ? c : z;
                mr -= itg_vol(dz[i], z, -1, g, d);
                vr += (eval_w(dz[i], g) + eval_w(dz[i], d)) * (d - g) / 2.0;
            }
        }
        double A[MAX_SP][MAX_SP] = {{0}}, B[MAX_SP] = {0};
        if (emp_E) { double D = (sp[1]-sp[0])/ir[0]; A[0][0] = 2*D; A[0][1] = D; B[0] = -LT[0]/ir[0]; }
        else { A[0][0] = 1.0; B[0] = ml; }
        if (emp_F) { double D = (sp[n_sp-1]-sp[n_sp-2])/ir[N-1]; A[n_sp-1][n_sp-2] = D; A[n_sp-1][n_sp-1] = 2*D; B[n_sp-1] = -RT[N-1]/ir[N-1]; }
        else { A[n_sp-1][n_sp-1] = 1.0; B[n_sp-1] = mr; }
        for (int i = 1; i < n_sp - 1; i++) {
            double c = (sp[i]-sp[i-1])/ir[i-1], D = (sp[i+1]-sp[i])/ir[i];
            A[i][i-1] = c; A[i][i] = 2*(c+D); A[i][i+1] = D;
            B[i] = -RT[i-1]/ir[i-1] - LT[i]/ir[i];
        }
        solve_lin(n_sp, A, B, Ms0);
        for (int j = 0; j < n_sp; j++) {
            double va = (j == 0) ? -vl : R0[j-1] - TT[j-1] + (Ms0[j] - Ms0[j-1]) / (sp[j] - sp[j-1]);
            double vb = (j == n_sp - 1) ? vr : R0[j] + (Ms0[j+1] - Ms0[j]) / (sp[j+1] - sp[j]);
            double pa = 0.0;
            for (int i = 0; i < n_pt; i++)
                if (fabs(pt[i].a - sp[j]) < 1e-9) pa += pt[i].p;
            Rs0[j] = vb - va + pa;
        }
        if (emp_E) G0 = Ms0[0];
        if (emp_F) {
            H0 = -G0 - Q - R;
            for (int i = 0; i < n_sp; i++) H0 += Rs0[i] * sp[i];
        }
    }

    double raw_kp[MAX_TR]; int nr = 0;
    raw_kp[nr++] = 0.0; raw_kp[nr++] = L;
    for (int i = 0; i < n_sp; i++) raw_kp[nr++] = sp[i];
    for (int i = 0; i < n_pt; i++) raw_kp[nr++] = pt[i].a;
    for (int i = 0; i < n_dz; i++) { raw_kp[nr++] = dz[i].c; raw_kp[nr++] = dz[i].d; }
    for (int i = 0; i < n_mo; i++) raw_kp[nr++] = mo[i].a;
    qsort(raw_kp, nr, sizeof(double), cmp_dbl);

    n_kp = 0; X_kp[n_kp++] = raw_kp[0];
    for (int i = 1; i < nr; i++)
        if (raw_kp[i] - X_kp[n_kp-1] > 1e-9) X_kp[n_kp++] = raw_kp[i];

    ex0[0] = -1e99; ex0[2] = 1e99; ex0[4] = -1e99; ex0[6] = 1e99;
    for (int j = 0; j < n_kp - 1; j++) {
        double y = X_kp[j] + 1e-9, a0 = G0, a1 = 0, a2 = 0, a3 = 0;
        for (int i = 0; i < n_sp; i++) if (sp[i] <= y) { a0 -= Rs0[i]*sp[i]; a1 += Rs0[i]; }
        for (int i = 0; i < n_pt; i++) if (pt[i].a <= y) { a0 += pt[i].p*pt[i].a; a1 -= pt[i].p; }
        for (int i = 0; i < n_mo; i++) if (mo[i].a <= y) a0 += mo[i].m;
        for (int i = 0; i < n_dz; i++) {
            double c = dz[i].c, d = dz[i].d, w1 = dz[i].w1, w2 = dz[i].w2;
            double k = (w2 - w1) / (d - c);
            double xs[2] = {c, d}, ts[2] = {w1, w2}, ss[2] = {1.0, -1.0};
            for (int m = 0; m < 2; m++) {
                if (xs[m] <= y) {
                    double x = xs[m], t = ts[m], s = ss[m];
                    a0 += s * (-t*x*x/2.0 + k*x*x*x/6.0);
                    a1 += s * (t*x - k*x*x/2.0);
                    a2 += s * (-t/2.0 + k*x/2.0);
                    a3 -= s * k / 6.0;
                }
            }
        }
        CF0[j][0] = fabs(a0)<1e-9 ? 0 : a0;
        CF0[j][1] = fabs(a1)<1e-9 ? 0 : a1;
        CF0[j][2] = fabs(a2)<1e-9 ? 0 : a2;
        CF0[j][3] = fabs(a3)<1e-9 ? 0 : a3;
        a0 = CF0[j][0]; a1 = CF0[j][1]; a2 = CF0[j][2]; a3 = CF0[j][3];

        double u = X_kp[j], v = X_kp[j+1], cand[6] = {u, v};
        int nc = 2;
        if (fabs(a3) > 1e-12) {
            double z = 4*a2*a2 - 12*a3*a1;
            if (z >= 0) {
                cand[nc++] = (-2*a2 + sqrt(z)) / (6*a3);
                cand[nc++] = (-2*a2 - sqrt(z)) / (6*a3);
            }
            cand[nc++] = -a2 / (3*a3);
        } else if (fabs(a2) > 1e-12) {
            cand[nc++] = -a1 / (2*a2);
        }
        for (int c = 0; c < nc; c++) {
            double x = cand[c];
            if (x >= u - 1e-9 && x <= v + 1e-9) {
                double m = ((a3*x + a2)*x + a1)*x + a0;
                double t = (3*a3*x + 2*a2)*x + a1;
                if (m > ex0[0] + 1e-12) { ex0[0] = m; ex0[1] = x; }
                if (m < ex0[2] - 1e-12) { ex0[2] = m; ex0[3] = x; }
                if (t > ex0[4] + 1e-12) { ex0[4] = t; ex0[5] = x; }
                if (t < ex0[6] - 1e-12) { ex0[6] = t; ex0[7] = x; }
            }
        }
    }
}

static void apply_scale(void) {
    double k = FK[u_in] / FK[u_out];
    for (int i = 0; i < n_sp; i++) { Rs[i] = Rs0[i]*k; Ms[i] = Ms0[i]*k; }
    G_r = G0*k; H_r = H0*k; P_tot = P_tot0*k;
    for (int j = 0; j < n_kp - 1; j++)
        for (int c = 0; c < 4; c++) CF[j][c] = CF0[j][c]*k;
    for (int i = 0; i < 8; i++) ex[i] = (i % 2 == 0) ? ex0[i]*k : ex0[i];
}

static void eval_vm(double x, double *v, double *m) {
    int j = 0;
    while (j < n_kp - 2 && x >= X_kp[j+1]) j++;
    double a0 = CF[j][0], a1 = CF[j][1], a2 = CF[j][2], a3 = CF[j][3];
    *v = (3*a3*x + 2*a2)*x + a1;
    *m = ((a3*x + a2)*x + a1)*x + a0;
}

static void poly_str(char *out, const double *c, int deg) {
    out[0] = '\0';
    char tmp[24];
    for (int i = 0; i <= deg; i++) {
        double v = c[i]; int e = deg - i;
        if (fabs(v) < 1e-9) continue;
        fm(tmp, fabs(v));
        if (e > 0 && strcmp(tmp, "1") == 0) tmp[0] = '\0';
        strcat(out, v < 0 ? "-" : (out[0] ? "+" : ""));
        strcat(out, tmp);
        if (e > 1) { char exs[12]; sprintf(exs, "x^%d", e); strcat(out, exs); }
        else if (e == 1) strcat(out, "x");
    }
    if (!out[0]) strcpy(out, "0");
}

/* --- VISTAS DE INTERFAZ GRAFICA --- */
static void view_reacc(void) {
    cls(RGB565(31, 63, 31));
    char hdr[56], line_b[64], b1[20], b2[20];
    sprintf(hdr, "REACCIONES (%s)  |  Grado Hiperest: %d", UN[u_out], n_sp + emp_E + emp_F - 2);
    header(hdr);

    int y = 20;
    txt_s(8, y, "Fuerzas verticales (+arriba):", TEXT_COLOR_BLUE); y += 12;
    for (int i = 0; i < n_sp; i++) {
        fm(b1, sp[i]); fm(b2, Rs[i]);
        sprintf(line_b, "R%d (x=%sm) = %s %s", i+1, b1, b2, UN[u_out]);
        txt_s((i % 2) * 190 + 8, y, line_b, TEXT_COLOR_BLACK);
        if (i % 2 == 1 || i == n_sp - 1) y += 11;
    }
    if (emp_E || emp_F) {
        y += 4;
        txt_s(8, y, "Momentos de empotramiento (+antihor):", TEXT_COLOR_BLUE); y += 12;
        if (emp_E) { fm(b1, -G_r); sprintf(line_b, "MA (x=0) = %s %s.m", b1, UN[u_out]); txt_s(8, y, line_b, TEXT_COLOR_BLACK); }
        if (emp_F) { fm(b1, -H_r); sprintf(line_b, "MB (x=L) = %s %s.m", b1, UN[u_out]); txt_s(198, y, line_b, TEXT_COLOR_BLACK); }
        y += 12;
    }
    if (n_sp >= 2) {
        y += 4;
        txt_s(8, y, "Momentos internos en apoyos (3 Mom.):", TEXT_COLOR_BLUE); y += 12;
        for (int i = 0; i < n_sp; i++) {
            fm(b1, Ms[i]);
            sprintf(line_b, "M%d = %s %s.m", i+1, b1, UN[u_out]);
            txt_s((i % 2) * 190 + 8, y, line_b, TEXT_COLOR_BLACK);
            if (i % 2 == 1 || i == n_sp - 1) y += 11;
        }
    }
    double sumR = 0; for (int i = 0; i < n_sp; i++) sumR += Rs[i];
    fm(b1, sumR - P_tot);
    sprintf(line_b, "Comprobacion Suma Fy = %s %s", b1, UN[u_out]);
    txt_s(8, 184, line_b, TEXT_COLOR_GREEN);
}

static void view_ecuac(int *tramo) {
    if (*tramo >= n_kp - 1) *tramo = 0;
    if (*tramo < 0) *tramo = n_kp - 2;
    int j = *tramo;
    double u = X_kp[j], v = X_kp[j+1];
    double a0 = CF[j][0], a1 = CF[j][1], a2 = CF[j][2], a3 = CF[j][3];
    char hdr[56], b1[20], b2[20], pv[64], pm[64], buf[80];
    fm(b1, u); fm(b2, v);
    sprintf(hdr, "ECUACIONES TRAMO %d/%d   [%s .. %s] m", j+1, n_kp-1, b1, b2);
    cls(RGB565(31, 63, 31));
    header(hdr);

    double cv[3] = {3*a3, 2*a2, a1};
    double cm[4] = {a3, a2, a1, a0};
    poly_str(pv, cv, 2);
    poly_str(pm, cm, 3);

    sprintf(buf, "Cortante V(x) [%s]:", UN[u_out]);
    txt_s(8, 24, buf, TEXT_COLOR_BLUE);
    sprintf(buf, "V(x) = %s", pv);
    txt_s(14, 38, buf, TEXT_COLOR_BLACK);
    fm(b1, (3*a3*u + 2*a2)*u + a1); fm(b2, (3*a3*v + 2*a2)*v + a1);
    sprintf(buf, "V_ini = %s    |    V_fin = %s", b1, b2);
    txt_s(14, 52, buf, TEXT_COLOR_BLACK);

    line(8, 70, 375, 70, RGB565(20, 40, 20));

    sprintf(buf, "Momento Flector M(x) [%s.m]:", UN[u_out]);
    txt_s(8, 80, buf, TEXT_COLOR_RED);
    sprintf(buf, "M(x) = %s", pm);
    txt_s(14, 94, buf, TEXT_COLOR_BLACK);
    fm(b1, a0 + u*(a1 + u*(a2 + u*a3))); fm(b2, a0 + v*(a1 + v*(a2 + v*a3)));
    sprintf(buf, "M_ini = %s    |    M_fin = %s", b1, b2);
    txt_s(14, 108, buf, TEXT_COLOR_BLACK);

    txt_s(8, 182, "Usa [<-] y [->] para cambiar de tramo", TEXT_COLOR_BLACK);
}

static void view_max(void) {
    cls(RGB565(31, 63, 31));
    header("VALORES EXTREMOS GLOBALES");
    char v[8][20], buf[64];
    for (int i = 0; i < 8; i++) fm(v[i], ex[i]);

    fill_rect(10, 24, 373, 92, RGB565(31, 58, 29));
    box(10, 24, 373, 92, RGB565(24, 0, 0));
    sprintf(buf, "MOMENTO FLECTOR (%s.m)", UN[u_out]);
    txt_s(18, 32, buf, TEXT_COLOR_RED);
    sprintf(buf, "M max = %s   (en x = %s m)", v[0], v[1]);
    txt_s(18, 50, buf, TEXT_COLOR_BLACK);
    sprintf(buf, "M min = %s   (en x = %s m)", v[2], v[3]);
    txt_s(18, 66, buf, TEXT_COLOR_BLACK);

    fill_rect(10, 104, 373, 172, RGB565(29, 60, 31));
    box(10, 104, 373, 172, RGB565(0, 12, 24));
    sprintf(buf, "FUERZA CORTANTE (%s)", UN[u_out]);
    txt_s(18, 112, buf, TEXT_COLOR_BLUE);
    sprintf(buf, "V max = %s   (en x = %s m)", v[4], v[5]);
    txt_s(18, 130, buf, TEXT_COLOR_BLACK);
    sprintf(buf, "V min = %s   (en x = %s m)", v[6], v[7]);
    txt_s(18, 146, buf, TEXT_COLOR_BLACK);
}

static void view_diag(int cur_px) {
    cls(RGB565(31, 63, 31));
    double x_cur = (double)cur_px / 356.0 * L;
    double vc, mc; eval_vm(x_cur, &vc, &mc);
    char bx[16], bv[16], bm[16], hdr[64];
    fm(bx, x_cur); fm(bv, vc); fm(bm, mc);
    sprintf(hdr, "x=%sm | V=%s%s | M=%s%s.m", bx, bv, UN[u_out], bm, UN[u_out]);
    header(hdr);

    for (int k = 0; k < 2; k++) {
        int y0 = 18 + 90 * k;
        double lo = (k == 0) ? (ex[6] < 0 ? ex[6] : 0) : (ex[2] < 0 ? ex[2] : 0);
        double hi = (k == 0) ? (ex[4] > 0 ? ex[4] : 0) : (ex[0] > 0 ? ex[0] : 0);
        if (hi - lo < 1e-9) hi = lo + 1.0;
        double sc = 62.0 / (hi - lo);
        int yb = y0 + 14 + (int)round(hi * sc);
        unsigned short col = (k == 0) ? RGB565(0, 16, 26) : RGB565(26, 4, 2);
        unsigned short fil = (k == 0) ? RGB565(22, 48, 31) : RGB565(31, 46, 23);

        char blo[16], bhi[16], lbl[48]; fm(blo, lo); fm(bhi, hi);
        sprintf(lbl, "%s [%s .. %s]", k == 0 ? "V" : "M", blo, bhi);
        txt_s(14, y0, lbl, k == 0 ? TEXT_COLOR_BLUE : TEXT_COLOR_RED);

        int px = 14, py = yb;
        for (int j = 0; j < n_kp - 1; j++) {
            double u = X_kp[j], v = X_kp[j+1];
            double a0 = CF[j][0], a1 = CF[j][1], a2 = CF[j][2], a3 = CF[j][3];
            for (int i = 0; i <= 28; i++) {
                double x = u + (v - u) * i / 28.0;
                double f = (k == 0) ? ((3*a3*x + 2*a2)*x + a1) : (((a3*x + a2)*x + a1)*x + a0);
                int qx = 14 + (int)round(x / L * 356.0);
                int qy = yb - (int)round(f * sc);
                if (i % 2 == 0) line(qx, yb, qx, qy, fil);
                line(px, py, qx, qy, col);
                px = qx; py = qy;
            }
        }
        line(px, py, px, yb, col);
        line(14, yb, 370, yb, RGB565(8, 16, 8));
        for (int i = 0; i < n_sp; i++) {
            int sx = 14 + (int)round(sp[i] / L * 356.0);
            line(sx, yb - 2, sx, yb + 2, RGB565(0, 0, 0));
        }
    }
    int cx = 14 + cur_px;
    line(cx, 18, cx, 196, RGB565(0, 36, 0));
}

static void arrow(int x, int y1, int y2, unsigned short col) {
    line(x, y1, x, y2, col);
    int d = (y2 > y1) ? -1 : 1;
    line(x, y2, x - 3, y2 + 4*d, col);
    line(x, y2, x + 3, y2 + 4*d, col);
}

static void view_viga(void) {
    cls(RGB565(31, 63, 31));
    char bL[16], hdr[56], foot[64]; fm(bL, L);
    sprintf(hdr, "ESQUEMA ESTRUCTURAL  L=%sm  (%s)", bL, UN[u_in]);
    header(hdr);

    int yb = 108, x0 = 24, W = 336;
    fill_rect(x0, yb - 1, x0 + W, yb + 1, RGB565(6, 12, 6));

    for (int i = 0; i < n_sp; i++) {
        int px = x0 + (int)round(sp[i] / L * W);
        if ((i == 0 && emp_E) || (i == n_sp - 1 && emp_F)) {
            int s = (i == 0) ? -1 : 1;
            line(px, yb - 22, px, yb + 22, RGB565(0, 0, 0));
            for (int k = -20; k <= 20; k += 5) line(px, yb + k, px + 6*s, yb + k + 4, RGB565(12, 24, 12));
        } else {
            line(px, yb + 2, px - 6, yb + 13, RGB565(0, 34, 0));
            line(px, yb + 2, px + 6, yb + 13, RGB565(0, 34, 0));
            line(px - 7, yb + 13, px + 7, yb + 13, RGB565(0, 34, 0));
        }
    }
    double wm = 1e-9;
    for (int i = 0; i < n_dz; i++) {
        if (fabs(dz[i].w1) > wm) wm = fabs(dz[i].w1);
        if (fabs(dz[i].w2) > wm) wm = fabs(dz[i].w2);
    }
    for (int i = 0; i < n_dz; i++) {
        int xa = x0 + (int)round(dz[i].c / L * W), xb = x0 + (int)round(dz[i].d / L * W);
        int ya = yb - (int)round(dz[i].w1 / wm * 38.0), yb2 = yb - (int)round(dz[i].w2 / wm * 38.0);
        line(xa, ya, xb, yb2, RGB565(0, 24, 28));
        int n = (xb - xa) / 14; if (n < 1) n = 1;
        for (int k = 0; k <= n; k++) {
            int xi = xa + (xb - xa) * k / n;
            int yi = ya + (yb2 - ya) * k / n;
            if (abs(yi - yb) > 4) arrow(xi, yi, yb - (yi < yb ? 3 : -3), RGB565(0, 24, 28));
        }
    }
    for (int i = 0; i < n_pt; i++) {
        int px = x0 + (int)round(pt[i].a / L * W);
        if (pt[i].p > 0) arrow(px, yb - 46, yb - 3, RGB565(26, 0, 0));
        else arrow(px, yb + 46, yb + 3, RGB565(26, 0, 0));
    }
    for (int i = 0; i < n_mo; i++) {
        int px = x0 + (int)round(mo[i].a / L * W);
        box(px - 6, yb - 10, px + 6, yb + 10, RGB565(22, 0, 22));
        txt_s(px - 2, yb - 22, mo[i].m < 0 ? "+" : "-", TEXT_COLOR_PURPLE);
    }
    sprintf(foot, "Apoyos:%d | Puntuales:%d | Distribuidas:%d | Momentos:%d", n_sp, n_pt, n_dz, n_mo);
    txt_s(8, 180, foot, TEXT_COLOR_BLACK);
}

/* --- FLUJO DE INGRESO DE DATOS --- */
static void capturar_viga(void) {
    u_in = u_out = select_unit("UNIDAD DE FUERZA");
    char msg[64];
    L = input_num("Longitud total de la viga L (m):", 1e-4, 1e6, 1);

    n_pt = (int)input_num("Cantidad de cargas puntuales (0..20):", 0, MAX_PT, 1);
    for (int i = 0; i < n_pt; i++) {
        sprintf(msg, "Puntual %d - Fuerza P (%s) [+arriba -abajo]:", i+1, UN[u_in]);
        pt[i].p = -input_num(msg, 0, 0, 0);
        sprintf(msg, "Puntual %d - Posicion x (m):", i+1);
        pt[i].a = input_num(msg, 0, L, 1);
    }

    n_dz = (int)input_num("Cantidad de cargas distribuidas (0..15):", 0, MAX_DZ, 1);
    for (int i = 0; i < n_dz; i++) {
        sprintf(msg, "Distrib. %d - Carga inicial w1 (%s/m):", i+1, UN[u_in]);
        dz[i].w1 = -input_num(msg, 0, 0, 0);
        sprintf(msg, "Distrib. %d - Carga final w2 (%s/m):", i+1, UN[u_in]);
        dz[i].w2 = -input_num(msg, 0, 0, 0);
        while (1) {
            sprintf(msg, "Distrib. %d - Posicion inicial x_ini (m):", i+1);
            dz[i].c = input_num(msg, 0, L, 1);
            sprintf(msg, "Distrib. %d - Posicion final x_fin (m):", i+1);
            dz[i].d = input_num(msg, 0, L, 1);
            if (dz[i].d > dz[i].c + 1e-7) break;
        }
    }

    n_mo = (int)input_num("Cantidad de momentos concentrados (0..15):", 0, MAX_MO, 1);
    for (int i = 0; i < n_mo; i++) {
        sprintf(msg, "Momento %d - Valor M (%s.m) [+antihor -hor]:", i+1, UN[u_in]);
        mo[i].m = -input_num(msg, 0, 0, 0);
        sprintf(msg, "Momento %d - Posicion x (m):", i+1);
        mo[i].a = input_num(msg, 0, L, 1);
    }

    while (1) {
        n_sp = (int)input_num("Cantidad de apoyos (1..12):", 1, MAX_SP, 1);
        for (int i = 0; i < n_sp; i++) {
            sprintf(msg, "Posicion del apoyo %d en x (m):", i+1);
            sp[i] = input_num(msg, 0, L, 1);
        }
        qsort(sp, n_sp, sizeof(double), cmp_dbl);
        emp_E = (fabs(sp[0]) < 1e-9) ? (int)input_num("Apoyo en x=0 es empotrado? (1=Si, 0=No):", 0, 1, 1) : 0;
        emp_F = (fabs(sp[n_sp-1] - L) < 1e-9) ? (int)input_num("Apoyo en x=L es empotrado? (1=Si, 0=No):", 0, 1, 1) : 0;
        if (!(n_sp == 1 && emp_E + emp_F == 0)) break;
        cls(RGB565(31, 63, 31)); header("ERROR ESTRUCTURAL");
        txt_s(10, 48, "Viga inestable (1 apoyo simple sin empotramiento).", TEXT_COLOR_RED);
        txt_s(10, 68, "Presiona cualquier tecla para reingresar apoyos...", TEXT_COLOR_BLACK);
        Bdisp_PutDisp_DD(); int tmp; GetKey(&tmp);
    }

    for (int i = 0; i < MAX_SP; i++) ir[i] = 1.0;
    if (n_sp >= 3) {
        int cte = (int)input_num("Rigidez EI constante en todos los tramos? (1=Si, 0=No):", 0, 1, 1);
        if (!cte) {
            for (int j = 0; j < n_sp - 1; j++) {
                sprintf(msg, "Inercia relativa I del tramo %d:", j+1);
                ir[j] = input_num(msg, 1e-4, 1e6, 1);
            }
        }
    }
}

/* --- MAIN --- */
int main(void) {
    EnableStatusArea(3);
    Bdisp_EnableColor(1);

    while (1) {
        capturar_viga();
        calc();
        apply_scale();

        int tab = 4;
        int tramo = 0;
        int cur_px = 178;

        while (1) {
            if (tab == 0) view_reacc();
            else if (tab == 1) view_ecuac(&tramo);
            else if (tab == 2) view_max();
            else if (tab == 3) view_diag(cur_px);
            else if (tab == 4) view_viga();

            draw_bar("REACC", "ECUA", "MAX", "DIAG", "VIGA", "UNID+", tab);
            Bdisp_PutDisp_DD();

            int k = 0;
            GetKey(&k);
            if (k == KEY_CTRL_F1) tab = 0;
            else if (k == KEY_CTRL_F2) tab = 1;
            else if (k == KEY_CTRL_F3) tab = 2;
            else if (k == KEY_CTRL_F4) tab = 3;
            else if (k == KEY_CTRL_F5) tab = 4;
            else if (k == KEY_CTRL_F6) {
                fill_rect(240, 145, 382, 198, RGB565(31, 63, 31));
                box(240, 145, 382, 198, RGB565(0, 0, 0));
                txt_s(246, 152, "[1] Cambiar unidad", TEXT_COLOR_BLACK);
                txt_s(246, 166, "[2] Nueva viga", TEXT_COLOR_BLACK);
                txt_s(246, 180, "[EXIT] Salir", TEXT_COLOR_BLACK);
                Bdisp_PutDisp_DD();
                int k2 = 0; GetKey(&k2);
                if (k2 == KEY_CHAR_1) { u_out = select_unit("UNIDADES DE SALIDA"); apply_scale(); }
                else if (k2 == KEY_CHAR_2) break;
                else if (k2 == KEY_CTRL_EXIT) return 0;
            } else if (k == KEY_CTRL_LEFT) {
                if (tab == 1) tramo--;
                else if (tab == 3 && cur_px > 0) cur_px -= 3;
            } else if (k == KEY_CTRL_RIGHT) {
                if (tab == 1) tramo++;
                else if (tab == 3 && cur_px < 356) cur_px += 3;
            } else if (k == KEY_CTRL_EXIT) {
                return 0;
            }
        }
    }
}
