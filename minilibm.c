// Implementation minimale de libm pour VGMSXPlay
// Objectif: Satisfaire le linker pour l'initialisation des tables audio.
// Précision: Suffisante pour tables 16-bit, non optimisée pour la vitesse (usage à l'init).

#define M_PI 3.14159265358979323846
#define LN2 0.69314718056

double floor(double x) {
    long long i = (long long)x;
    return (double)(i - (i > x));
}

double ceil(double x) {
    long long i = (long long)x;
    return (double)(i + (i < x));
}

double trunc(double x) {
    return (double)((long long)x);
}

double fabs(double x) {
    return x < 0 ? -x : x;
}

double sin(double x) {
    // Ramener x dans [-PI, PI] approximativement
    int k = (int)(x / M_PI);
    x -= k * M_PI;
    if (k & 1) x = -x; // sin(x + k*PI) = (-1)^k * sin(x) ? non c'est sin(x+PI) = -sin(x)
    // En fait reduction simple dans [-PI, PI] suffit
    while (x > M_PI) x -= 2 * M_PI;
    while (x < -M_PI) x += 2 * M_PI;

    double x2 = x*x;
    double x3 = x2*x;
    double x5 = x3*x2;
    double x7 = x5*x2;
    // Taylor serie ordre 7: x - x^3/6 + x^5/120 - x^7/5040
    return x - x3/6.0 + x5/120.0 - x7/5040.0;
}

double cos(double x) {
    return sin(x + M_PI/2.0);
}

double sqrt(double x) {
    if (x <= 0) return 0;
    double r = x;
    // 6 itérations de Newton suffisent largement pour une bonne precision
    r = 0.5 * (r + x/r);
    r = 0.5 * (r + x/r);
    r = 0.5 * (r + x/r);
    r = 0.5 * (r + x/r);
    r = 0.5 * (r + x/r);
    r = 0.5 * (r + x/r);
    return r;
}

static double my_exp(double x) {
    // exp(x) via Taylor pour x petit, reduction via exp(k*ln2)
    int k = (int)(x / LN2);
    double r = x - k * LN2;
    double r2 = r*r;
    double y = 1.0 + r + r2/2.0 + (r2*r)/6.0 + (r2*r2)/24.0 + (r2*r2*r)/120.0;
    
    // Multiplication par 2^k
    if (k > 0) for(int i=0; i<k; i++) y *= 2.0;
    else for(int i=0; i<-k; i++) y *= 0.5;
    return y;
}

double log(double x) {
    if (x <= 0) return -1e308;
    int p = 0;
    while (x > 2.0) { x *= 0.5; p++; }
    while (x < 1.0) { x *= 2.0; p--; }
    
    double u = (x - 1.0) / (x + 1.0);
    double u2 = u*u;
    // 2 * (u + u^3/3 + u^5/5 + u^7/7 + u^9/9)
    double y = u * (2.0 + u2 * (2.0/3.0 + u2 * (2.0/5.0 + u2 * (2.0/7.0 + u2 * 2.0/9.0))));
    return y + p * LN2;
}

double pow(double x, double y) {
    if (x <= 0) return 0.0;
    if (y == 0) return 1.0;
    return my_exp(y * log(x));
}
